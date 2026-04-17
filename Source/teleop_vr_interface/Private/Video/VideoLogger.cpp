#include "Video/VideoLogger.h"
#include "Video/VideoEncoderWrapper.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/Canvas.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"
#include "RenderingThread.h"
#include "RHICommandList.h"

// ----------------------------------------------------------------

AVideoLogger::AVideoLogger()
{
    PrimaryActorTick.bCanEverTick = true;
}

AVideoLogger::~AVideoLogger()
{
    // EncoderState is a raw void* — cleaned up in FinalizeEncoder().
}

void AVideoLogger::BeginPlay() { Super::BeginPlay(); }
void AVideoLogger::Tick(float DeltaTime) { Super::Tick(DeltaTime); }

void AVideoLogger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopLogging(TEXT("EndPlay"));
    Super::EndPlay(EndPlayReason);
}

// ----------------------------------------------------------------
// StartLogging
// ----------------------------------------------------------------

void AVideoLogger::StartLogging()
{
    if (bIsLogging || LayerSources.Num() == 0) {
        UE_LOG(LogTemp, Warning, TEXT("Video logging failed: bIsLogging=%d Sources=%d"),
            bIsLogging, LayerSources.Num());
        return;
    }

    FrameIndex      = 0;
    AccumulatedTime = 0.0;
    SecondsPerFrame = CaptureFPS > 0.f ? 1.0 / CaptureFPS : 0.0;
    StartTimeUtc    = FDateTime::UtcNow();

    if (!LoggingRT)
    {
        LoggingRT = UKismetRenderingLibrary::CreateRenderTarget2D(this, LogW, LogH, RTF_RGBA8);
        if (LoggingRT) { LoggingRT->Filter = TF_Bilinear; LoggingRT->UpdateResource(); }
    }
    if (!LoggingRT) { UE_LOG(LogTemp, Error, TEXT("VideoLogger: RT creation failed")); return; }

    InitSessionDirectory();
    WriteInitialMetadata();

    // Create ffmpeg-based H.264 encoder — output written to session.mp4 in real time.
    EncoderState = VideoEncoder_Create(LogW, LogH, FMath::RoundToInt(CaptureFPS), VideoPath);
    if (!EncoderState)
    {
        UE_LOG(LogTemp, Error, TEXT("VideoLogger: encoder init failed"));
        return;
    }

    EncoderRunning = true;
    EncoderFuture  = Async(EAsyncExecution::ThreadPool, [this]()
    {
        while (EncoderRunning || !EncodeQueue.IsEmpty())
        {
            FLogFrame Frame;
            if (!EncodeQueue.Dequeue(Frame)) { FPlatformProcess::Sleep(0.001f); continue; }

            ApplyGazeSpotlight(Frame.Pixels, Frame.Bundle.GazeUV,
                               Frame.Bundle.bGazeValid, Frame.Bundle.GazeConfidence);

            // BGRA → YUV420P
            const int32 NumPx = LogW * LogH;
            TArray<uint8> I420;
            I420.SetNumUninitialized(NumPx + NumPx / 2);
            uint8* Yp = I420.GetData();
            uint8* Up = Yp + NumPx;
            uint8* Vp = Up + NumPx / 4;

            for (int32 Row = 0; Row < LogH; ++Row)
            for (int32 Col = 0; Col < LogW; ++Col)
            {
                const FColor& C = Frame.Pixels[Row * LogW + Col];
                Yp[Row * LogW + Col] = (uint8)FMath::Clamp(
                    0.257f*C.R + 0.504f*C.G + 0.098f*C.B + 16.f, 0.f, 255.f);
                if (Row % 2 == 0 && Col % 2 == 0)
                {
                    int32 UV = (Row/2)*(LogW/2) + Col/2;
                    Up[UV] = (uint8)FMath::Clamp(-0.148f*C.R - 0.291f*C.G + 0.439f*C.B + 128.f, 0.f, 255.f);
                    Vp[UV] = (uint8)FMath::Clamp( 0.439f*C.R - 0.368f*C.G - 0.071f*C.B + 128.f, 0.f, 255.f);
                }
            }

            VideoEncoder_SubmitYUV420P(
                static_cast<FEncoderHandle*>(EncoderState),
                Yp, Up, Vp, LogW, LogH);

            AppendFrameJsonl(Frame.Bundle);
        }
    });

    bIsLogging = true;
    UE_LOG(LogTemp, Log, TEXT("VideoLogger: started → %s"), *SessionDir);
}

// ----------------------------------------------------------------
// StopLogging
// ----------------------------------------------------------------

void AVideoLogger::StopLogging(const FString& Notes)
{
    if (!bIsLogging) return;
    bIsLogging = false;

    // Signal the encode thread to stop FIRST.
    EncoderRunning = false;

    // Drain with a timeout — if the thread is stuck in WritePipe,
    // FinalizeEncoder (which closes the pipe) will unblock it.
    if (EncoderFuture.IsValid())
    {
        // Give the thread a short window to drain naturally.
        const double Start = FPlatformTime::Seconds();
        while (!EncoderFuture.WaitFor(FTimespan::FromMilliseconds(100)))
        {
            if (FPlatformTime::Seconds() - Start > 2.0)
            {
                // Thread is stuck (likely blocked on pipe write).
                // Close the pipe to unblock it, then wait again.
                UE_LOG(LogTemp, Warning,
                    TEXT("VideoLogger: encode thread stuck, closing pipe to unblock"));
                FinalizeEncoder();
                EncoderFuture.Wait();
                break;
            }
        }
    }

    // FinalizeEncoder is safe to call twice (checks for nullptr).
    FinalizeEncoder();
    UpdateFinalMetadata(Notes);

    UE_LOG(LogTemp, Log, TEXT("VideoLogger: stopped. Frames=%lld → %s"),
        FrameIndex, *VideoPath);
}

// ----------------------------------------------------------------
// SubmitFrame
// ----------------------------------------------------------------

void AVideoLogger::SubmitFrame(const FFrameBundle& InBundle)
{
    if (!bIsLogging || !LoggingRT) return;

    if (CaptureFPS > 0.f)
    {
        AccumulatedTime += GetWorld()->GetDeltaSeconds();
        if (AccumulatedTime < SecondsPerFrame) return;
        AccumulatedTime -= SecondsPerFrame;
    }

    FFrameBundle Bundle = InBundle;
    Bundle.FrameIdx = FrameIndex++;
    CaptureFrame(Bundle);
}

// ----------------------------------------------------------------
// CaptureFrame
// ----------------------------------------------------------------

void AVideoLogger::CaptureFrame(const FFrameBundle& Bundle)
{
    FDrawToRenderTargetContext Ctx;
    UCanvas* Canvas = nullptr;
    FVector2D Size;
    UKismetRenderingLibrary::ClearRenderTarget2D(this, LoggingRT, FLinearColor::Black);
    UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, LoggingRT, Canvas, Size, Ctx);

    if (Canvas)
    {
        for (const FLayerSource& Src : LayerSources)
        {
            UTexture* Tex = Src.GetTexture ? Src.GetTexture() : nullptr;
            if (!Tex) continue;

            // Draw each layer fullscreen with alpha blending.
            // Video feed (priority 0) is opaque, UI layers blend on top.
            EBlendMode Blend = (Src.Priority == 0) ? BLEND_Opaque : BLEND_Translucent;
            Canvas->K2_DrawTexture(Tex, FVector2D::ZeroVector,
                FVector2D((float)LogW, (float)LogH),
                FVector2D::ZeroVector, FVector2D::UnitVector,
                FLinearColor::White, Blend);
        }
    }

    UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Ctx);

    const FFrameBundle Snap = Bundle;
    const int32 W = LogW, H = LogH;

    FRHITexture2D* RHITex =
        LoggingRT->GameThread_GetRenderTargetResource()->GetRenderTargetTexture();
    if (!RHITex) return;

    ENQUEUE_RENDER_COMMAND(VideoLoggerReadback)(
        [RHITex, this, Snap, W, H](FRHICommandListImmediate& RHICmdList)
        {
            TArray<FColor> Pixels;
            Pixels.SetNumUninitialized(W * H);
            RHICmdList.ReadSurfaceData(RHITex, FIntRect(0, 0, W, H), Pixels,
                FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX));
            FLogFrame LF; LF.Bundle = Snap; LF.Pixels = MoveTemp(Pixels);
            EncodeQueue.Enqueue(MoveTemp(LF));
        });
}

// ----------------------------------------------------------------
// ApplyGazeSpotlight
// ----------------------------------------------------------------

void AVideoLogger::ApplyGazeSpotlight(TArray<FColor>& Pixels,
    FVector2D GazeUV, bool bValid, float Confidence) const
{
    const float Dark  = FMath::Clamp(PeripheralBrightness, 0.f, 1.f);
    const float RadPx = GazeRadiusFraction * (float)LogW;
    const float SigSq = RadPx * RadPx;
    const float GX    = GazeUV.X * (float)LogW;
    const float GY    = GazeUV.Y * (float)LogH;

    for (int32 Y = 0; Y < LogH; ++Y)
    for (int32 X = 0; X < LogW; ++X)
    {
        FColor& P = Pixels[Y * LogW + X];
        float W = Dark;
        if (bValid && Confidence > 0.f)
        {
            float dx = (float)X - GX, dy = (float)Y - GY;
            W = Dark + (1.f-Dark) * FMath::Pow(
                FMath::Exp(-(dx*dx + dy*dy) / (2.f*SigSq)), 0.45f);
        }
        P.R = (uint8)FMath::Clamp((float)P.R * W, 0.f, 255.f);
        P.G = (uint8)FMath::Clamp((float)P.G * W, 0.f, 255.f);
        P.B = (uint8)FMath::Clamp((float)P.B * W, 0.f, 255.f);
    }

    if (bValid && bDrawGazeDot)
    {
        const int32 CX = (int32)GX, CY = (int32)GY;
        const int32 R2 = GazeDotRadiusPx * GazeDotRadiusPx;
        for (int32 Y = CY-GazeDotRadiusPx; Y <= CY+GazeDotRadiusPx; ++Y)
        for (int32 X = CX-GazeDotRadiusPx; X <= CX+GazeDotRadiusPx; ++X)
        {
            if (X < 0 || X >= LogW || Y < 0 || Y >= LogH) continue;
            if ((X-CX)*(X-CX)+(Y-CY)*(Y-CY) <= R2)
                Pixels[Y*LogW+X] = FColor(255, 50, 50, 255);
        }
    }
}

// ----------------------------------------------------------------
// Encoder lifecycle
// ----------------------------------------------------------------

void AVideoLogger::FinalizeEncoder()
{
    if (!EncoderState) return;
    VideoEncoder_Destroy(static_cast<FEncoderHandle*>(EncoderState));
    EncoderState = nullptr;
}
// ----------------------------------------------------------------
// Session IO
// ----------------------------------------------------------------

void AVideoLogger::InitSessionDirectory()
{
    FString Base = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("OperatorViewLogs"));
    IFileManager::Get().MakeDirectory(*Base, true);

    SessionDir      = Base / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    FramesJsonlPath = SessionDir / TEXT("frames.jsonl");
    MetaJsonPath    = SessionDir / TEXT("metadata.json");
    VideoPath       = SessionDir / TEXT("session.mp4");
    IFileManager::Get().MakeDirectory(*SessionDir, true);
    UE_LOG(LogTemp, Log, TEXT("Video Logging: Writing to %s"), *SessionDir);
}

void AVideoLogger::WriteInitialMetadata()
{
    TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
    R->SetStringField(TEXT("start_time_utc"), StartTimeUtc.ToIso8601());
    R->SetStringField(TEXT("end_time_utc"),   TEXT(""));
    R->SetNumberField(TEXT("duration_sec"),   0.0);
    R->SetNumberField(TEXT("width"),          LogW);
    R->SetNumberField(TEXT("height"),         LogH);
    R->SetNumberField(TEXT("target_fps"),     CaptureFPS);
    R->SetNumberField(TEXT("quad_width_ue"),  QuadWidth);
    R->SetNumberField(TEXT("quad_height_ue"), QuadHeight);
    R->SetNumberField(TEXT("plane_dist_ue"),  PlaneDistance);

    FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(R, W);
    FFileHelper::SaveStringToFile(Out, *MetaJsonPath);
}

void AVideoLogger::UpdateFinalMetadata(const FString& Notes)
{
    FString Raw; TSharedPtr<FJsonObject> R;
    if (FFileHelper::LoadFileToString(Raw, *MetaJsonPath))
    {
        TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Raw);
        FJsonSerializer::Deserialize(Rd, R);
    }
    if (!R.IsValid()) R = MakeShared<FJsonObject>();

    FDateTime End = FDateTime::UtcNow();
    R->SetStringField(TEXT("end_time_utc"), End.ToIso8601());
    R->SetNumberField(TEXT("duration_sec"), (End - StartTimeUtc).GetTotalSeconds());
    R->SetNumberField(TEXT("frame_count"),  (double)FrameIndex);
    R->SetStringField(TEXT("notes"),        Notes);
    R->SetStringField(TEXT("video_file"),   TEXT("session.mp4"));

    FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(R.ToSharedRef(), W);
    FFileHelper::SaveStringToFile(Out, *MetaJsonPath);
}

void AVideoLogger::AppendFrameJsonl(const FFrameBundle& B)
{
    TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
    O->SetNumberField(TEXT("frame_idx"),       (double)B.FrameIdx);
    O->SetNumberField(TEXT("ts"),              B.UnixTime);
    O->SetNumberField(TEXT("sender_time_ns"),  (double)B.SenderTimeNs);
    O->SetNumberField(TEXT("gaze_u"),          B.GazeUV.X);
    O->SetNumberField(TEXT("gaze_v"),          B.GazeUV.Y);
    O->SetNumberField(TEXT("gaze_confidence"), B.GazeConfidence);
    O->SetBoolField  (TEXT("gaze_valid"),      B.bGazeValid);

    FString Line; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Line);
    FJsonSerializer::Serialize(O, W);
    Line.AppendChar('\n');
    FFileHelper::SaveStringToFile(Line, *FramesJsonlPath,
        FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
}

void AVideoLogger::AddLayerSource(TFunction<UTexture* ()> Getter, int32 Priority)
{
    FLayerSource Src;
    Src.GetTexture = MoveTemp(Getter);
    Src.Priority = Priority;
    LayerSources.Add(MoveTemp(Src));
    LayerSources.Sort([](const FLayerSource& A, const FLayerSource& B)
        {
            return A.Priority < B.Priority;
        });
}
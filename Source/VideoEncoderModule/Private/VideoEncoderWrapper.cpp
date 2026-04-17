#include "VideoEncoderWrapper.h"

#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformFile.h"

THIRD_PARTY_INCLUDES_START
#include "VideoEncoderFactory.h"
#include "VideoEncoder.h"
#include "VideoEncoderInput.h"
THIRD_PARTY_INCLUDES_END

PRAGMA_DISABLE_DEPRECATION_WARNINGS
using namespace AVEncoder;

struct FEncoderHandle
{
    TSharedPtr<FVideoEncoderInput> Input;
    TUniquePtr<FVideoEncoder>      Encoder;
    IFileHandle* File = nullptr;
    FCriticalSection               FileLock;

    void OnPacket(uint32, const TSharedPtr<FVideoEncoderInputFrame>&,
        const FCodecPacket& Packet)
    {
        static const uint8 StartCode[] = { 0x00, 0x00, 0x00, 0x01 };
        FScopeLock L(&FileLock);
        if (File) { File->Write(StartCode, 4); File->Write(Packet.Data.Get(), Packet.DataSize); }
    }
};

FEncoderHandle* VideoEncoder_Create(int32 Width, int32 Height, int32 FPS,
    const FString& OutputPath)
{
    const TArray<FVideoEncoderInfo>& Available = FVideoEncoderFactory::Get().GetAvailable();

    UE_LOG(LogTemp, Log, TEXT("VideoEncoderWrapper: %d encoder(s) registered"), Available.Num());
    for (const FVideoEncoderInfo& Info : Available)
        UE_LOG(LogTemp, Log, TEXT("  ID=%u CodecType=%d"), Info.ID, (int32)Info.CodecType);

    TSharedPtr<FVideoEncoderInput> Input = FVideoEncoderInput::CreateForYUV420P(Width, Height);
    if (!Input) { UE_LOG(LogTemp, Error, TEXT("VideoEncoderWrapper: CreateForYUV420P failed")); return nullptr; }

    const FVideoEncoderInfo* Chosen = nullptr;
    for (const FVideoEncoderInfo& Info : Available)
        if (Info.CodecType == ECodecType::H264) { Chosen = &Info; break; }

    if (!Chosen) { UE_LOG(LogTemp, Error, TEXT("VideoEncoderWrapper: no H.264 encoder found")); return nullptr; }

    FVideoEncoder::FLayerConfig Cfg;
    Cfg.Width = Width; Cfg.Height = Height; Cfg.MaxFramerate = (uint32)FPS;
    Cfg.TargetBitrate = 8'000'000; Cfg.MaxBitrate = 12'000'000;

    TUniquePtr<FVideoEncoder> Enc = FVideoEncoderFactory::Get().Create(Chosen->ID, Input, Cfg);
    if (!Enc) { UE_LOG(LogTemp, Error, TEXT("VideoEncoderWrapper: creation failed (ID=%u)"), Chosen->ID); return nullptr; }

    IFileHandle* File = IPlatformFile::GetPlatformPhysical().OpenWrite(*OutputPath);
    if (!File) { UE_LOG(LogTemp, Error, TEXT("VideoEncoderWrapper: cannot open %s"), *OutputPath); return nullptr; }

    FEncoderHandle* H = new FEncoderHandle();
    H->Input = MoveTemp(Input); H->Encoder = MoveTemp(Enc); H->File = File;

    H->Encoder->SetOnEncodedPacket(
        [H](uint32 Layer, const TSharedPtr<FVideoEncoderInputFrame>& Frame, const FCodecPacket& Packet)
        { H->OnPacket(Layer, Frame, Packet); });

    UE_LOG(LogTemp, Log, TEXT("VideoEncoderWrapper: ready (ID=%u) → %s"), Chosen->ID, *OutputPath);
    return H;
}

void VideoEncoder_SubmitYUV420P(FEncoderHandle* Handle,
    const uint8* Y, const uint8* U, const uint8* V,
    int32 Width, int32 Height, uint32 FrameID, int64 TimestampUs)
{
    if (!Handle || !Handle->Encoder || !Handle->Input) return;

    TSharedPtr<FVideoEncoderInputFrame> Frame = Handle->Input->ObtainInputFrame();
    if (!Frame) return;

    Frame->SetFrameID(FrameID);
    Frame->SetTimestampUs(TimestampUs);
    Frame->SetWidth(Width);
    Frame->SetHeight(Height);
    Frame->SetYUV420P(Y, U, V, Width, Width / 2, Width / 2);

    FVideoEncoder::FEncodeOptions Opts;
    Handle->Encoder->Encode(Frame, Opts);
}

void VideoEncoder_Destroy(FEncoderHandle* Handle)
{
    if (!Handle) return;
    if (Handle->Encoder) Handle->Encoder->Shutdown();
    if (Handle->File) { delete Handle->File; Handle->File = nullptr; }
    delete Handle;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS
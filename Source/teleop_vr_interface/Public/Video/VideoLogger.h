#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UI/GazeComponent.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"
#include "VideoLogger.generated.h"

class UTextureRenderTarget2D;
class UTexture2D;

// Snapshotted atomically on the game thread — keeps pixels, gaze, and timestamps in sync.
struct FFrameBundle
{
    int64     FrameIdx       = 0;
    double    UnixTime       = 0.0;
    int64     SenderTimeNs   = 0;
    FVector2D GazeUV         = FVector2D(0.5f, 0.5f);
    float     GazeConfidence = 0.0f;
    bool      bGazeValid     = false;
};

struct FLogFrame
{
    FFrameBundle   Bundle;
    TArray<FColor> Pixels;
};

UCLASS()
class TELEOP_VR_INTERFACE_API AVideoLogger : public AActor
{
    GENERATED_BODY()

public:
    AVideoLogger();
    // Destructor defined in .cpp where FEncoderState is complete.
    virtual ~AVideoLogger() override;

    UPROPERTY() UTexture2D* SourceTexture = nullptr;

    UPROPERTY(EditAnywhere, Category = "VideoLogger") float CaptureFPS = 30.0f;
    UPROPERTY(EditAnywhere, Category = "VideoLogger") int32 LogW       = 1280;
    UPROPERTY(EditAnywhere, Category = "VideoLogger") int32 LogH       = 720;

    UPROPERTY(EditAnywhere, Category = "VideoLogger|Gaze") float GazeRadiusFraction   = 0.15f;
    UPROPERTY(EditAnywhere, Category = "VideoLogger|Gaze") float PeripheralBrightness = 0.20f;
    UPROPERTY(EditAnywhere, Category = "VideoLogger|Gaze") bool  bDrawGazeDot         = true;
    UPROPERTY(EditAnywhere, Category = "VideoLogger|Gaze") int32 GazeDotRadiusPx      = 6;

    float QuadWidth     = 1475.4f;
    float QuadHeight    =  830.0f;
    float PlaneDistance =  700.0f;

    UFUNCTION(BlueprintCallable, Category = "VideoLogger") void StartLogging();
    UFUNCTION(BlueprintCallable, Category = "VideoLogger") void StopLogging(const FString& Notes = TEXT(""));

    void SubmitFrame(const FFrameBundle& Bundle);
    bool IsLogging() const { return bIsLogging; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

private:
    void CaptureFrame(const FFrameBundle& Bundle);
    void ApplyGazeSpotlight(TArray<FColor>& Pixels, FVector2D GazeUV, bool bValid, float Confidence) const;

    void InitSessionDirectory();
    void WriteInitialMetadata();
    void UpdateFinalMetadata(const FString& Notes);
    void AppendFrameJsonl(const FFrameBundle& Bundle);

    bool InitEncoder();
    void FinalizeEncoder();

    bool   bIsLogging      = false;
    int64  FrameIndex      = 0;
    double AccumulatedTime = 0.0;
    double SecondsPerFrame = 0.0;

    FString   SessionDir;
    FString   FramesJsonlPath;
    FString   MetaJsonPath;
    FString   VideoPath;
    FDateTime StartTimeUtc;

    UPROPERTY() UTextureRenderTarget2D* LoggingRT = nullptr;

    TQueue<FLogFrame, EQueueMode::Mpsc> EncodeQueue;
    FThreadSafeBool EncoderRunning = false;
    TFuture<void>   EncoderFuture;

    // Raw pointer to opaque encoder state defined only in VideoLogger.cpp.
    // Must be raw: TUniquePtr with a forward-declared type fails in UHT-generated code.
    void* EncoderState = nullptr;
};

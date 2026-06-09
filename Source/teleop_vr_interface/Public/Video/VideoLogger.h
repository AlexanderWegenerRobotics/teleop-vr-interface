#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UI/GazeComponent.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"
#include "VideoLogger.generated.h"

class UTextureRenderTarget2D;
class UTexture2D;

struct FFrameBundle
{
    int64     FrameIdx = 0;
    double    UnixTime = 0.0;
    int64     SenderTimeNs = 0;
    FVector2D GazeUV = FVector2D(0.5f, 0.5f);
    float     GazeConfidence = 0.0f;
    bool      bGazeValid = false;
};

struct FLogFrame
{
    FFrameBundle   Bundle;
    TArray<FColor> Pixels;
};

struct FLayerSource
{
    TFunction<UTexture* ()> GetTexture;
    int32     Priority  = 0;
    FVector2D UVOffset  = FVector2D::ZeroVector;
    FVector2D UVSize    = FVector2D::UnitVector;
};

UCLASS()
class TELEOP_VR_INTERFACE_API AVideoLogger : public AActor
{
    GENERATED_BODY()

public:
    AVideoLogger();
    virtual ~AVideoLogger() override;

    TArray<FLayerSource> LayerSources;
    void AddLayerSource(TFunction<UTexture* ()> Getter, int32 Priority,
                        FVector2D UVOffset = FVector2D::ZeroVector,
                        FVector2D UVSize   = FVector2D::UnitVector);

    // Output control — set before StartLogging.
    UPROPERTY(EditAnywhere, Category = "VideoLogger") bool bEnableRawVideo = true;
    UPROPERTY(EditAnywhere, Category = "VideoLogger") bool bEnableAttentionVideo = true;

    UPROPERTY(EditAnywhere, Category = "VideoLogger") float CaptureFPS = 30.0f;
    UPROPERTY(EditAnywhere, Category = "VideoLogger") int32 LogW = 1280;
    UPROPERTY(EditAnywhere, Category = "VideoLogger") int32 LogH = 720;

    UPROPERTY(EditAnywhere, Category = "VideoLogger|Gaze") float GazeRadiusFraction = 0.10f;
    UPROPERTY(EditAnywhere, Category = "VideoLogger|Gaze") float PeripheralBrightness = 0.20f;
    UPROPERTY(EditAnywhere, Category = "VideoLogger|Gaze") bool  bDrawGazeDot = true;
    UPROPERTY(EditAnywhere, Category = "VideoLogger|Gaze") int32 GazeDotRadiusPx = 6;

    float QuadWidth = 1475.4f;
    float QuadHeight = 830.0f;
    float PlaneDistance = 700.0f;

    UFUNCTION(BlueprintCallable, Category = "VideoLogger") void StartLogging();
    UFUNCTION(BlueprintCallable, Category = "VideoLogger") void StopLogging(const FString& Notes = TEXT(""));

    void SubmitFrame(const FFrameBundle& Bundle);
    bool IsLogging() const { return bIsLogging; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

private:
    void CaptureFrame(const FFrameBundle& Bundle, int32 NumFrames = 1);
    void ApplyGazeSpotlight(TArray<FColor>& Pixels, FVector2D GazeUV, bool bValid, float Confidence) const;
    void ConvertBGRAtoYUV420P(const TArray<FColor>& Pixels, TArray<uint8>& OutI420) const;

    void InitSessionDirectory();
    void WriteInitialMetadata();
    void UpdateFinalMetadata(const FString& Notes);
    void AppendFrameJsonl(const FFrameBundle& Bundle);

    void FinalizeEncoders();

    bool   bIsLogging = false;
    int64  FrameIndex = 0;
    double AccumulatedTime = 0.0;
    double SecondsPerFrame = 0.0;

    FString   SessionDir;
    FString   FramesJsonlPath;
    FString   MetaJsonPath;
    FString   RawVideoPath;
    FString   AttentionVideoPath;
    FDateTime StartTimeUtc;

    UPROPERTY() UTextureRenderTarget2D* LoggingRT = nullptr;

    TQueue<FLogFrame, EQueueMode::Mpsc> EncodeQueue;
    FThreadSafeBool EncoderRunning = false;
    TFuture<void>   EncoderFuture;

    void* EncoderRaw = nullptr;
    void* EncoderAttention = nullptr;
};
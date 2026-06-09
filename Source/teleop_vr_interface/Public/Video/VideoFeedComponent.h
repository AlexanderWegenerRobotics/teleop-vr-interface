#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/StereoLayerComponent.h"
#include "Video/IVideoSource.h"
#include "VideoFeedComponent.generated.h"

class UCameraComponent;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UVideoFeedComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UVideoFeedComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void RegisterSource(const FString& Name, TUniquePtr<IVideoSource> Source);
    bool SetActiveSource(const FString& Name);
    void SetStereoMode(bool bStereo);

    FString GetActiveSourceName() const;
    FVideoSourceStats GetStreamStats() const;
    bool IsReceiving() const;
    bool IsStereoMode() const { return bStereo_; }
    UTexture2D* GetVideoTexture() const { return VideoTexture; }
    int64  GetSenderTimeNs() const { return ActiveSource_ ? ActiveSource_->GetLastSenderTimeNs() : 0; }
    uint64 GetLastFrameId() const  { return ActiveSource_ ? ActiveSource_->GetLastFrameId()      : 0; }

    // Called after both VideoFeed and GhostOverlay have initialized (post Super::BeginPlay).
    // Binds the ghost eye RTs to the video post-process material so ghost compositing
    // happens inside the single working PP pass rather than a separate (dropped) blendable.
    void SetGhostTextures(UTextureRenderTarget2D* Left, UTextureRenderTarget2D* Right);

    UPROPERTY(EditAnywhere, Category = "VideoFeed")
    float PlaneDistance = 700.0f;

    UPROPERTY(EditAnywhere, Category = "VideoFeed", meta = (ClampMin = "0.5", ClampMax = "1.0"))
    float FOVCoverage = 0.85f;

private:
    void CreateStereoLayer();
    void UpdateLayerSize(int32 Width, int32 Height);

    UPROPERTY(EditAnywhere, Category = "VideoFeed")
    TObjectPtr<UMaterialInterface> StereoVideoMaterial;

    UPROPERTY()
    TObjectPtr<UStereoLayerComponent> StereoLayer;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> PostProcessMID_;

    UPROPERTY()
    TObjectPtr<UTexture2D> VideoTexture;

    TMap<FString, TUniquePtr<IVideoSource>> Sources_;
    FString       ActiveSourceName_;
    IVideoSource* ActiveSource_      = nullptr;
    IVideoSource* RightEyeSource_    = nullptr;
    bool          bStereo_           = false;
    double        LastSyncedUpdateTimeSec_ = 0.0; // for stereo frame sync timeout

    // Auto-reconnect: restart the pipeline if no frames arrive for this long.
    static constexpr float kReconnectTimeoutSec = 3.0f;
    float NotReceivingAccumSec_ = 0.0f;

    UPROPERTY()
    TObjectPtr<UTexture2D> RightTexture_;

    UPROPERTY()
    TObjectPtr<UCameraComponent> CameraRef;
};
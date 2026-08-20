#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
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

    // Registers a source and Initialize()s+Start()s it immediately if
    // BeginPlay has already run (or defers to BeginPlay, which starts every
    // registered source). All registered sources decode continuously
    // regardless of which is active -- see SetActiveSource.
    void RegisterSource(const FString& Name, TUniquePtr<IVideoSource> Source);

    // Switches which already-running source feeds the stereo layer / PP
    // material. Deliberately just a pointer/name swap -- no Stop()/
    // Initialize()/Start() -- so this is cheap and safe to call repeatedly
    // at runtime (e.g. from a future autonomous view-selection policy), with
    // no GStreamer pipeline teardown/reconnect hitch. Returns false if Name
    // isn't a registered source.
    bool SetActiveSource(const FString& Name);
    void SetStereoMode(bool bStereo);

    FString GetActiveSourceName() const;
    FVideoSourceStats GetStreamStats() const;
    bool IsReceiving() const;

    // Lets a second display surface (e.g. the PiP overlay) show an
    // already-registered source's video without opening its own receiver --
    // one decode, shown in two places. Safe to call even when that source
    // isn't the active main-view one; every registered source decodes
    // continuously regardless (see RegisterSource). Returns false if Name
    // isn't registered.
    bool UpdateSourceTexture(const FString& Name, UTexture2D*& OutTexture);
    FVideoSourceStats GetSourceStats(const FString& Name) const;
    bool IsStereoMode() const { return bStereo_; }
    UTexture2D* GetVideoTexture() const { return VideoTexture; }
    int64  GetSenderTimeNs() const { return (ActiveSource_ && !IsSourceBusy(ActiveSourceName_)) ? ActiveSource_->GetLastSenderTimeNs() : 0; }
    uint64 GetLastFrameId() const  { return (ActiveSource_ && !IsSourceBusy(ActiveSourceName_)) ? ActiveSource_->GetLastFrameId()      : 0; }

    // Called after both VideoFeed and GhostOverlay have initialized (post Super::BeginPlay).
    // Binds the ghost eye RTs to the video post-process material so ghost compositing
    // happens inside the single working PP pass rather than a separate (dropped) blendable.
    void SetGhostTextures(UTextureRenderTarget2D* Left, UTextureRenderTarget2D* Right);

    UPROPERTY(EditAnywhere, Category = "VideoFeed")
    float PlaneDistance = 700.0f;

    UPROPERTY(EditAnywhere, Category = "VideoFeed", meta = (ClampMin = "0.5", ClampMax = "1.0"))
    float FOVCoverage = 0.85f;

    // HMD horizontal FOV used to size the face-locked video quad. Set from
    // overlay.json's hmd_hfov_deg alongside PlaneDistance/FOVCoverage above, so
    // this quad and the ghost overlay quad are always sized from one source.
    UPROPERTY(EditAnywhere, Category = "VideoFeed")
    float HmdHFovDeg = 110.0f;

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

    // Auto-reconnect: restart a source's pipeline if no frames arrive for
    // this long. Tracked per registered source (not just the active one) so
    // a source we aren't currently looking at (e.g. twin, while avatar is
    // on-screen) is already healthy by the time SetActiveSource switches to
    // it, instead of only starting its reconnect timer at switch time.
    //
    // The restart itself runs on a worker thread: IVideoSource::Start() can
    // block for seconds inside GStreamer's state change, and doing that in
    // TickComponent froze the game thread long enough for the compositor to
    // drop us and show its "waiting for app" screen in the HMD.
    //
    // Delay backs off exponentially (base * 2^Attempt, capped) so a stream
    // that simply isn't running -- e.g. the twin when nobody launched it --
    // settles into a cheap 30 s poll instead of thrashing every 3 s forever.
    static constexpr float kReconnectBaseSec = 3.0f;
    static constexpr float kReconnectMaxSec  = 30.0f;

    struct FReconnectState
    {
        float AccumSec = 0.0f;   // time since we last saw frames (or last retry)
        int32 Attempt  = 0;      // consecutive failed restarts, drives the backoff
        // Valid + not-ready means a restart is in flight on a worker thread and
        // this source must not be touched from the game thread (Initialize()
        // swaps the underlying receiver out from under us).
        TFuture<void> Pending;
    };
    TMap<FString, FReconnectState> Reconnect_;

    // True while a worker thread is inside Stop()/Initialize()/Start() for this
    // source. Every game-thread read of a source (stats, texture, timestamps)
    // must check this first.
    bool IsSourceBusy(const FString& Name) const;
    float ReconnectDelayFor(int32 Attempt) const;

    UPROPERTY()
    TObjectPtr<UTexture2D> RightTexture_;

    UPROPERTY()
    TObjectPtr<UCameraComponent> CameraRef;
};
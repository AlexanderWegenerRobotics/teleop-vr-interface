#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/StereoLayerComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GhostOverlayComponent.generated.h"

class UCameraComponent;
class UComLink;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UTrackedControllerComponent;


UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UGhostOverlayComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGhostOverlayComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void SetCamera(UCameraComponent* InCamera) { CameraRef = InCamera; }
    void SetComLink(UComLink* InComLink) { ComLinkRef = InComLink; }
    void SetStereoMode(bool bStereo) { bStereo_ = bStereo; }
    UTextureRenderTarget2D* GetRenderTarget()      const { return CaptureRT; }
    UTextureRenderTarget2D* GetRenderTargetLeft()  const { return CaptureRTLeft; }
    UTextureRenderTarget2D* GetRenderTargetRight() const { return CaptureRTRight; }

    // Right arm VR controller fallback refs.
    void SetRightHand   (USceneComponent*            h) { RightHandRef    = h; }
    void SetRightTracked(UTrackedControllerComponent* t) { RightTrackedRef = t; }

    // Left arm VR controller fallback refs.
    void SetLeftHand   (USceneComponent*            h) { LeftHandRef    = h; }
    void SetLeftTracked(UTrackedControllerComponent* t) { LeftTrackedRef = t; }

    // Called each frame by the pawn with the current video-feed latency (ms).
    // Thread-safe write; value is consumed and smoothed on the game thread.
    void SetVideoLatencyMs(float Ms) { RawLatencyMs_ = Ms; }

    // ---- Latency feedback ------------------------------------------------- //
    // Smoothed latency (EMA, τ ≈ 0.2 s) is compared against these thresholds.
    // Hysteresis prevents rapid flipping: enter Warning above WarnMs, exit below OkMs;
    // enter Bad above BadMs, exit back to Warning below WarnMs.
    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    float LatencyOkMs   =  100.f;   // smoothed threshold — exit Warning → OK
    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    float LatencyWarnMs =  150.f;   // smoothed threshold — enter Warning
    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    float LatencyBadMs  =  300.f;   // smoothed threshold — enter Bad
    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    float LatencyBadExitMs = 200.f; // smoothed threshold — exit Bad → Warning

    // Amber = "pay attention but no panic".  Orange-red = "ghost is stale, act".
    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    FLinearColor LatencyWarnColor = FLinearColor(1.f, 0.60f, 0.00f, 1.f);
    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    FLinearColor LatencyBadColor  = FLinearColor(1.f, 0.20f, 0.00f, 1.f);

    // Vector/Color parameter name inside M_Ghost to receive the latency tint.
    // Open the material in the editor and check what the parameter is called.
    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    FName LatencyColorParam = TEXT("Color");

    // ---- Overlay / Capture ------------------------------------------------ //
    UPROPERTY(EditAnywhere, Category = "Ghost|Overlay")
    float PlaneDistance = 700.f;

    UPROPERTY(EditAnywhere, Category = "Ghost|Overlay", meta = (ClampMin = "0.5", ClampMax = "1.0"))
    float FOVCoverage = 0.85f;

    UPROPERTY(EditAnywhere, Category = "Ghost|Capture")
    float CaptureFOV = 75.2f;          // mono mode capture FOV

    // Stereo mode: SceneCapture FOV for each eye — should match the video camera fovy (110°).
    UPROPERTY(EditAnywhere, Category = "Ghost|Capture")
    float StereoCaptureFOV = 75.2f;

    // Stereo mode: half-IPD in cm (distance from center to each eye).
    // Match your headset's IPD setting ÷ 2.  Default = 61 mm ÷ 2 = 3.05 cm.
    UPROPERTY(EditAnywhere, Category = "Ghost|Capture")
    float StereoEyeOffsetCm = 3.05f;

    UPROPERTY(EditAnywhere, Category = "Ghost|Capture")
    FIntPoint RenderTargetSize = FIntPoint(1280, 960);

    UPROPERTY(EditAnywhere, Category = "Ghost|HeadCam")
    FVector HeadBasePosition = FVector(0.f, 0.f, 1.34f);

    UPROPERTY(EditAnywhere, Category = "Ghost|HeadCam")
    FVector CamOffsetInHead = FVector(0.05f, 0.f, 0.035f);

    // Per-arm mesh import orientation offset.  Tune in editor if the mesh is
    // rotated relative to the EE frame reported by the robot.
    UPROPERTY(EditAnywhere, Category = "Ghost|Mesh")
    FRotator EEFrameOffset = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, Category = "Ghost|Mesh")
    FRotator LeftEEFrameOffset = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, Category = "Ghost|Fingers")
    FVector RightFingerOpenOffset = FVector(0.f, 4.0f, 5.38f);

    UPROPERTY(EditAnywhere, Category = "Ghost|Fingers")
    FVector RightFingerClosedOffset = FVector(0.f, 0.5f, 5.38f);

    UPROPERTY(EditAnywhere, Category = "Ghost|Fingers")
    FVector LeftFingerOpenOffset = FVector(0.f, -4.0f, 5.38f);

    UPROPERTY(EditAnywhere, Category = "Ghost|Fingers")
    FVector LeftFingerClosedOffset = FVector(0.f, -0.5f, 5.38f);

    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UMaterialInterface> PostProcessMaterial;

    // Stereo mode: post-process material applied to the VR camera that composites
    // left/right ghost RTs over the scene per eye (like M_StereoVideoFeed but with alpha).
    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UMaterialInterface> StereoGhostPostProcessMaterial;

    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UStaticMesh> GhostHandMesh;

    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UStaticMesh> GhostLeftFingerMesh;

    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UStaticMesh> GhostRightFingerMesh;

    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UMaterialInterface> GhostHandMaterial;

    // Left arm hand mesh — optional, falls back to GhostHandMesh if unset.
    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UStaticMesh> GhostLeftHandMesh;

private:
    void LoadAssets();
    void CreateRenderTarget();
    void CreateSceneCapture();
    void CreateStereoLayer();
    void CreateLatencyMaterial();   // builds GhostMID_ from GhostHandMaterial
    void UpdateLayerSize();
    void UpdateGhostPose();       // right arm (index 1)
    void UpdateLeftArmPose();     // left  arm (index 0)
    void UpdateFingerPose(UStaticMeshComponent* LFinger, UStaticMeshComponent* RFinger,
                          UTrackedControllerComponent* Tracked);
    FQuat UpdateCaptureTransforms(const FQuat& R_HW_Protocol);
    void UpdateLatencyState(float DeltaTime);
    void ApplyLatencyColor();       // pushes tint to GhostMID_ based on LatencyLevel_

    UPROPERTY()
    TObjectPtr<UTextureRenderTarget2D> CaptureRT;

    UPROPERTY()
    TObjectPtr<USceneCaptureComponent2D> SceneCapture;

    // Right arm mesh components.
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> GhostMeshComp;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> LeftFingerMeshComp;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> RightFingerMeshComp;

    // Left arm mesh components — same SceneCapture, separate pose.
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> GhostLeftMeshComp;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> LeftArmLeftFingerMeshComp;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> LeftArmRightFingerMeshComp;

    UPROPERTY()
    TObjectPtr<UStereoLayerComponent> StereoLayer;

    UPROPERTY()
    TObjectPtr<UCameraComponent> CameraRef;

    UPROPERTY()
    TObjectPtr<USceneComponent> RightHandRef;

    UPROPERTY()
    TObjectPtr<USceneComponent> LeftHandRef;

    UPROPERTY()
    TObjectPtr<UComLink> ComLinkRef;

    UPROPERTY()
    TObjectPtr<UTrackedControllerComponent> RightTrackedRef;

    UPROPERTY()
    TObjectPtr<UTrackedControllerComponent> LeftTrackedRef;

    // Shared dynamic material instance applied to all ghost mesh components.
    // Created at BeginPlay from GhostHandMaterial so we can tint at runtime.
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> GhostMID_;

    bool bPipelineReady = false;
    bool bStereo_       = false;

    // Stereo eye captures and render targets (null in mono mode).
    UPROPERTY()
    TObjectPtr<USceneCaptureComponent2D> SceneCaptureLeft;

    UPROPERTY()
    TObjectPtr<USceneCaptureComponent2D> SceneCaptureRight;

    UPROPERTY()
    TObjectPtr<UTextureRenderTarget2D> CaptureRTLeft;

    UPROPERTY()
    TObjectPtr<UTextureRenderTarget2D> CaptureRTRight;

    // Post-process MID for stereo ghost composite (null in mono mode).
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> PostProcessGhostMID_;

    // Latency state — written by SetVideoLatencyMs (pawn thread), consumed in Tick.
    float RawLatencyMs_      = 0.f;
    float SmoothedLatencyMs_ = 0.f;
    uint8 LatencyLevel_      = 0;   // 0 = OK  |  1 = Warning  |  2 = Bad
};

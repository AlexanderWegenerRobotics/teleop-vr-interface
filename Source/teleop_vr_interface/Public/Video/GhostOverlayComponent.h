#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/StereoLayerComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Shared/protocol.hpp"
#include "GhostOverlayComponent.generated.h"

class UCameraComponent;
class UComLink;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
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
    void SetVideoLatencyMs(float Ms) { RawLatencyMs_ = Ms; }
    // Delta pose in protocol coordinates (same layout as ArmCommandMsg).
    // Integrated into an absolute pose each tick; ghost freezes on clutch.
    void SetIntentPose(uint8 ArmIndex, const float DeltaPosition[3], const float DeltaQuaternion[4], float Gripper, bool bClutchActive);

    // Seed absolute EE position from ArmStateMsg so the ghost starts at the right place.
    // Call whenever the arm transitions to ENGAGED.
    void SeedIntentPose(uint8 ArmIndex, const float Position[3], const float Quaternion[4]);
    void UnseedIntentPose(uint8 ArmIndex);

    UPROPERTY(EditAnywhere, Category = "Ghost|Intent")
    bool bUseIntentPose = true;

    // Controller->EE orientation retarget per arm (index 0 = left, 1 = right), protocol
    // frame. Set from config in OperatorPawn::BeginPlay; must match the arm's
    // controller_axis_map. Defaults reproduce the values formerly hardcoded here.
    FQuat ControllerToEEQuat[2] = {
        FQuat(0.5f,  0.5f, 0.5f, -0.5f),   // left
        FQuat(0.5f, -0.5f, 0.5f,  0.5f)    // right
    };

    // When true the ghost is held at the workspace limit instead of being allowed to
    // show the command going past it. Default false = show intent (penetration) so the
    // operator sees e.g. "this command drives into the table". Either way the ghost is
    // tinted with CommandLimitColor while the command is past a limit.
    UPROPERTY(EditAnywhere, Category = "Ghost|Intent")
    bool bClampGhostToWorkspace = false;

    // Slow correction of the command origin toward the measured arm origin (arm EE ⊖
    // delta). This is what makes the ghost ease onto the real end-effector once everything
    // has settled, closing the static offset that the command alone never closes — the arm
    // is a compliant spring and sits a steady-state deflection (load / stiffness) short of
    // its setpoint, biggest during contact/manipulation. The correction is gated on
    // operator-at-rest AND arm-settled AND in-bounds, so it never fights the catch-up
    // (ghost holds while the arm flies in) and never erases a deliberate over-command into
    // a limit. Per-fresh-state lerp factor (0 = off, ~0.05–0.15 = ease over a fraction of
    // a second once settled).
    UPROPERTY(EditAnywhere, Category = "Ghost|Intent")
    float OriginConvergeRate = 0.0f;

    // "At rest" thresholds for enabling the origin correction above.
    UPROPERTY(EditAnywhere, Category = "Ghost|Intent")
    float RestPosEpsilonM = 0.002f;
    UPROPERTY(EditAnywhere, Category = "Ghost|Intent")
    float RestAngEpsilonRad = 0.01f;

    // Tint applied while the command target is past a workspace limit.
    UPROPERTY(EditAnywhere, Category = "Ghost|Intent")
    FLinearColor CommandLimitColor = FLinearColor(1.f, 0.f, 0.f, 1.f);

    // Opacity driving — set from config
    float GhostNearThresholdM_ = 0.03f;
    float GhostFarThresholdM_  = 0.30f;
    float GhostMinOpacity_     = 0.35f;
    float GhostMaxOpacity_     = 0.95f;

    // Workspace boundary planes — set from config
    float WorkspaceLowerBoundZ_     = 0.435f;
    float WorkspaceBoundaryMarginM_ = 0.05f;
    float BoundaryPlaneWidthM_      = 1.8f;
    float BoundaryPlaneHeightM_     = 0.96f;
    // Lateral workspace limits
    float WorkspaceMinX_ =  0.4f;
    float WorkspaceMaxY_ =  0.4f;
    float WorkspaceMinY_ = -0.4f;

    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    float LatencyOkMs   =  100.f;   // smoothed threshold — exit Warning → OK
    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    float LatencyWarnMs =  150.f;   // smoothed threshold — enter Warning
    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    float LatencyBadMs  =  300.f;   // smoothed threshold — enter Bad
    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    float LatencyBadExitMs = 200.f; // smoothed threshold — exit Bad → Warning

    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    FLinearColor LatencyWarnColor = FLinearColor(1.f, 0.60f, 0.00f, 1.f);
    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    FLinearColor LatencyBadColor  = FLinearColor(1.f, 0.20f, 0.00f, 1.f);
    UPROPERTY(EditAnywhere, Category = "Ghost|Latency")
    FName LatencyColorParam = TEXT("EmissiveColor");
    FLinearColor GhostDefaultColor = FLinearColor(0.2f, 0.5f, 1.0f, 1.0f);

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
    // Match your headset's IPD setting ÷ 2.  Default = 61 mm ÷ 2 = 3.05 cm.
    UPROPERTY(EditAnywhere, Category = "Ghost|Capture")
    float StereoEyeOffsetCm = 3.05f;

    UPROPERTY(EditAnywhere, Category = "Ghost|Capture")
    FIntPoint RenderTargetSize = FIntPoint(1280, 960);

    UPROPERTY(EditAnywhere, Category = "Ghost|HeadCam")
    FVector HeadBasePosition = FVector(0.f, 0.f, 1.844f);

    UPROPERTY(EditAnywhere, Category = "Ghost|HeadCam")
    FVector CamOffsetInHead = FVector(0.05f, 0.f, 0.035f);

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

    // ---- Overlay hooks for other VR-drawn indicators (e.g. grasp indicator) --------- //
    // Additive-only: lets an external component register a primitive so it renders inside
    // the ghost's SceneCapture(s), and get an anchor to attach to so it inherits the ghost
    // mesh's per-tick pose via normal USceneComponent attachment. Does not touch any of the
    // ghost's own rendering/pose logic.
    void RegisterOverlayComponent(UPrimitiveComponent* Comp);
    USceneComponent* GetRightWristAnchor() const { return GhostMeshComp; }
    USceneComponent* GetLeftWristAnchor()  const { return GhostLeftMeshComp; }

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
    void ApplyLatencyColor();
    void UpdateGhostOpacity();
    void UpdateBoundaryPlane();

    bool ApplyArmPoseToMesh(UStaticMeshComponent* Mesh, const float Position[3], const float  Quaternion[4], const FRotator&  EEOffset, FQuat* OutR_HW = nullptr);

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

    // Per-arm dynamic material instances — right arm and left arm respectively.
    // Keeping them separate allows independent opacity per arm.
    // Created at BeginPlay from GhostHandMaterial so we can tint at runtime.
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> GhostMID_;      // right arm
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> GhostLeftMID_;  // left arm

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

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> BoundaryPlaneMeshComp;       // floor (z-min)
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> BoundaryPlaneLeftMeshComp;   // +Y wall
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> BoundaryPlaneRightMeshComp;  // -Y wall
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> BoundaryPlaneTorsoMeshComp;  // -X (torso) wall

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> BoundaryPlaneMID_;
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> BoundaryPlaneLeftMID_;
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> BoundaryPlaneRightMID_;
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> BoundaryPlaneTorsoMID_;

    // Latency state — written by SetVideoLatencyMs (pawn thread), consumed in Tick.
    float RawLatencyMs_      = 0.f;
    float SmoothedLatencyMs_ = 0.f;
    uint8 LatencyLevel_      = 0;   // 0 = OK  |  1 = Warning  |  2 = Bad

    // Intent pose storage — one slot per arm (index 0 = left, 1 = right).
    //
    // Raw command-target display: the ghost shows where the operator is commanding the
    // arm to go, so it LEADS the arm and reveals intent that is past a limit (e.g. into
    // the table):
    //     ghost = Origin ⊕ command delta
    // To still settle back onto the real arm in reachable space, Origin is nudged toward
    // the measured arm origin (arm EE ⊖ delta) ONLY while the operator is at rest and the
    // command is in-bounds — so the live lead is never dragged back during motion and a
    // deliberate over-command into a limit is preserved. All quaternions are stored
    // w, x, y, z (protocol order).
    struct FIntentPose
    {
        // Command origin (protocol/world frame). Seeded at engage with the arm EE pose;
        // thereafter only corrected slowly toward the measured arm origin while at rest
        // and in-bounds. The displayed ghost is Origin ⊕ command delta.
        float OriginPosition[3]   = {0.f, 0.f, 0.f};
        float OriginQuaternion[4] = {1.f, 0.f, 0.f, 0.f};   // w, x, y, z

        // Most recent operator command delta (set by SetIntentPose, protocol frame).
        float LastDeltaPosition[3]   = {0.f, 0.f, 0.f};
        float LastDeltaQuaternion[4] = {1.f, 0.f, 0.f, 0.f};

        // Previous tick's command delta, used to detect operator "at rest".
        float PrevDeltaPosition[3]   = {0.f, 0.f, 0.f};
        float PrevDeltaQuaternion[4] = {1.f, 0.f, 0.f, 0.f};

        // Previous fresh arm-state EE pose, used to detect the arm itself "settled"
        // (i.e. it has finished catching up, not still in flight toward the ghost).
        float PrevArmPosition[3]   = {0.f, 0.f, 0.f};
        float PrevArmQuaternion[4] = {1.f, 0.f, 0.f, 0.f};
        bool  bHavePrevArm         = false;

        // Displayed absolute pose = Origin ⊕ delta (recomputed each tick).
        float Position[3]   = {0.f, 0.f, 0.f};
        float Quaternion[4] = {1.f, 0.f, 0.f, 0.f};

        float Gripper      = 0.f;
        bool  bSeeded      = false;
        bool  bPastBounds  = false;   // command target past a workspace limit this tick
    };
    FIntentPose IntentPoses_[2];

    // Per-tick arm-state cache. The arm state is consumed once per tick (Read() clears
    // the HasNew flag) and shared by the intent, opacity and pose paths so they don't
    // steal the fresh-state flag from one another.
    ArmStateMsg CachedArmState_[2] = {};
    bool        bArmStateFresh_[2] = { false, false };
    bool        bGhostPastBounds_  = false;
    bool        bPrevGhostPastBounds_ = false;

    void CacheArmStates();          // read each stream once, fill the cache above
    void UpdateIntentPoses();       // raw command target + guarded origin correction
    bool IsWithinWorkspace(const float Pos[3]) const;

    // Primitives registered via RegisterOverlayComponent — re-applied to newly created
    // captures so registration order relative to BeginPlay never matters.
    UPROPERTY()
    TArray<TObjectPtr<UPrimitiveComponent>> RegisteredOverlayComponents_;
};

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/MaterialBillboardComponent.h"
#include "AlignmentPanelComponent.generated.h"

class UGhostOverlayComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

// Alignment state for one arm as sent by the nudge engine (type "alignment").
// World/protocol frame (X forward, Y left, Z up), metres and radians. The engine decides
// what is ok; this side only maps the numbers into the operator's view and draws them.
struct FAlignmentState {
    bool    bActive = false;
    FString Phase;
    float   PosErr[3] = { 0.f, 0.f, 0.f };
    float   YawErr = 0.f;
    float   TargetYaw = 0.f;
    float   Lean[2] = { 0.f, 0.f };
    float   TolXY = 0.005f;
    float   TolYaw = 0.05f;
    float   TolTilt = 0.026f;
    float   ZRange = 0.10f;
    float   Depth = 0.f;
    bool    bXYOk = false;
    bool    bYawOk = false;
    bool    bTiltOk = false;
    bool    bTiltActive = false;
    bool    bDone = false;
    bool    bTooFast = false;
    bool    bWedged = false;
    bool    bHasAnchor = false;
    bool    bHasCmd = false;
    float   CmdErr[3] = { 0.f, 0.f, 0.f };
    float   Anchor[3] = { 0.f, 0.f, 0.f };
};

// 2D alignment panel drawn next to one gripper: top-down xy dot with yaw line, a ring
// around the dot that shrinks with height, an insertion-depth arc, and pitch/roll gauges
// on the panel edges. Rendered through the ghost's scene-capture rig as a camera-facing
// billboard so it sits beside the gripper and never on the insertion axis.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UAlignmentPanelComponent : public UActorComponent {
    GENERATED_BODY()

public:
    UAlignmentPanelComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void Initialize(UGhostOverlayComponent* Ghost, uint8 InArmIndex);

    void SetState(const FAlignmentState& State);

    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    TObjectPtr<UMaterialInterface> PanelMaterial;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float SizeCm = 8.f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float SideOffsetCm = 12.f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float UpOffsetCm = 4.f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float FollowTauS = 0.12f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float SideSwitchCm = 3.f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float HoldS = 0.4f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float FadeS = 0.2f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    bool bAutoScale = true;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    bool bViewAligned = false;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float TolRadius = 0.25f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float FixedFullScaleM = 0.02f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float MinFullScaleM = 0.006f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float RingMin = 0.11f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float RingMax = 0.9f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float TiltFullScaleFactor = 4.f;
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel")
    float TiltIdleEmphasis = 0.35f;

    UPROPERTY(EditAnywhere, Category = "AlignmentPanel|Material")
    FName XYParam = TEXT("XY");
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel|Material")
    FName YawParam = TEXT("Yaw");
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel|Material")
    FName ZDParam = TEXT("ZD");
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel|Material")
    FName TiltParam = TEXT("Tilt");
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel|Material")
    FName FlagsParam = TEXT("Flags");
    UPROPERTY(EditAnywhere, Category = "AlignmentPanel|Material")
    FName CmdParam = TEXT("Cmd");

private:
    void ApplyParams(const FVector2D& Fwd, const FVector2D& Left);
    bool ResolveAnchor(FVector& OutWorld, FVector& OutTargetWorld, bool& bOutHaveTarget);
    bool ViewAxesProtocol(FVector2D& OutFwd, FVector2D& OutLeft) const;

    UPROPERTY()
    TObjectPtr<UMaterialBillboardComponent> Billboard_;
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> MID_;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Probe_;
    UPROPERTY()
    TObjectPtr<UGhostOverlayComponent> GhostRef_;

    FCriticalSection Mtx_;
    FAlignmentState Pending_;
    bool bPending_ = false;

    FAlignmentState State_;
    double LastSeen_ = 0.0;
    float Level_ = 0.f;
    FVector Smoothed_ = FVector::ZeroVector;
    bool bHaveSmoothed_ = false;
    uint8 ArmIndex_ = 0;
    float Side_ = 0.f;
    bool bGhostValid_ = false;
    float GhostPos_[3] = { 0.f, 0.f, 0.f };
    bool bInitialized_ = false;
};

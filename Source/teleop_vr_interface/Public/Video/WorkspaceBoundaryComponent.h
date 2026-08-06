#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WorkspaceBoundaryComponent.generated.h"

class UGhostOverlayComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

// Localized proximity cue for the four workspace faces the ghost itself reacts to (torso,
// left, right, floor — no far wall, no ceiling, per project scope). One patch mesh per face,
// moved to the closest point on that face to the ghost EE and scaled/colored/faded by a
// severity scalar. Registers its meshes with UGhostOverlayComponent via the additive
// RegisterOverlayComponent/GetCaptureAnchor hooks — standalone, does not modify the ghost.
//
// Severity is a pure function of (distance, closing rate) — see ComputeFaceSeverity() — so it
// stays deterministic/auditable and is easy to extend with a second source (e.g. an avatar-side
// joint-limit signal) later without restructuring this component.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UWorkspaceBoundaryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UWorkspaceBoundaryComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // Call once, after Super::BeginPlay() in the owning pawn so the ghost's capture anchor
    // already exists. ArmIndex: 0 = left, 1 = right (protocol convention).
    void Initialize(UGhostOverlayComponent* Ghost, uint8 InArmIndex);

    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Assets")
    TObjectPtr<UMaterialInterface> PatchMaterial;

    // Time-to-contact is the primary severity driver: a slow deliberate approach stays quiet,
    // a fast one warns early. Tune against logged episodes — the cue should be visible in
    // well under 10% of normal operating time.
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Severity")
    float TtcHorizonS = 0.5f;

    // Distance floor: guarantees genuine closeness is visible even at zero velocity.
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Severity")
    float DistFloorM = 0.15f;

    // Distance gate on the TTC term only: beyond this raw distance, a large instantaneous
    // closing-rate estimate (VR tracking jitter, a fast flick) cannot light up the cue by
    // itself — that's pure numerics, not a real imminent violation.
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Severity")
    float TtcEngageDistanceM = 0.4f;

    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|AntiFlicker")
    float OnThreshold = 0.35f;
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|AntiFlicker")
    float OffThreshold = 0.20f;
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|AntiFlicker")
    float AttackS = 0.12f;
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|AntiFlicker")
    float ReleaseS = 0.35f;
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|AntiFlicker")
    float MinOnS = 0.30f;

    // Patch radius GROWS with severity — small/subtle for an early prediction, large and hard
    // to miss at an actual violation. Urgency is carried by size AND color/alpha together.
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Visual")
    float RadiusMinCm = 8.f;
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Visual")
    float RadiusMaxCm = 32.f;
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Visual")
    float GridSpacingCm = 5.f;

    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Material")
    FName ContactParam = TEXT("Contact");
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Material")
    FName TanUParam = TEXT("TanU");
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Material")
    FName TanVParam = TEXT("TanV");
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Material")
    FName RadiusParam = TEXT("Radius");
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Material")
    FName SpacingParam = TEXT("Spacing");
    UPROPERTY(EditAnywhere, Category = "WorkspaceBoundary|Material")
    FName SevParam = TEXT("Sev");

private:
    enum class EFace : uint8 { Torso = 0, Left = 1, Right = 2, Floor = 3 };
    static constexpr int32 kNumFaces = 4;

    struct FFaceState {
        UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
        UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
        float PlaneQuat[4] = { 1.f, 0.f, 0.f, 0.f };   // protocol-frame orientation, constant
        float PrevD = 0.f;
        float PrevRate = 0.f;
        float Sev = 0.f;
        float TimeSinceOn = 0.f;
        bool  bHavePrevD = false;
    };
    FFaceState Faces_[kNumFaces];

    float ComputeFaceSeverity(float Distance, float ClosingRate) const;
    void  EvaluateFace(EFace Face, const float EEPosition[3], float DeltaTime);
    void  FaceGeometry(EFace Face, const float EEPosition[3], float& OutDistance, float OutClosestPoint[3]) const;

    UPROPERTY()
    TObjectPtr<UGhostOverlayComponent> GhostRef_;

    uint8 ArmIndex_ = 0;
    bool  bInitialized_ = false;
};

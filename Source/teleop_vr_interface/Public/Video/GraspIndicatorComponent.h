#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/MaterialBillboardComponent.h"
#include "Shared/protocol.hpp"
#include "GraspIndicatorComponent.generated.h"

class UComLink;
class UGhostOverlayComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EGraspDisplayState : uint8 {
    Unknown = 0,
    Open    = 1,
    Held    = 2,
    Lost    = 3
};

// Per-arm grasp confirmation glyph, attached to the ghost overlay's wrist anchor for the
// given arm. Reads GraspState off the arm's ArmStateMsg via ComLink, resolves it into an
// EGraspDisplayState (adding the interface-only Unknown state on staleness/disconnect),
// and drives an SDF glyph material through an on-change-only MID parameter update.
// Standalone system — registers itself with UGhostOverlayComponent via the additive
// RegisterOverlayComponent/GetXWristAnchor hooks only, does not modify ghost internals.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UGraspIndicatorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGraspIndicatorComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // Call once, after Super::BeginPlay() in the owning pawn so the ghost's wrist anchors
    // and captures already exist. ArmIndex: 0 = left, 1 = right (protocol convention).
    void Initialize(UGhostOverlayComponent* Ghost, UComLink* InComLink, uint8 InArmIndex);

    EGraspDisplayState GetDisplayState() const { return DisplayState_; }

    UPROPERTY(EditAnywhere, Category = "GraspIndicator|Assets")
    TObjectPtr<UMaterialInterface> GlyphMaterial;

    UPROPERTY(EditAnywhere, Category = "GraspIndicator|Layout")
    FVector GlyphOffset = FVector(0.f, 0.f, -8.f);

    UPROPERTY(EditAnywhere, Category = "GraspIndicator|Layout")
    float GlyphSize = 5.25f;

    // No new ArmStateMsg sequence number for this long marks the arm's grasp state stale
    // and the glyph falls back to Unknown.
    UPROPERTY(EditAnywhere, Category = "GraspIndicator|Timing")
    float StalenessTimeoutS = 0.3f;

    UPROPERTY(EditAnywhere, Category = "GraspIndicator|Colors")
    FLinearColor UnknownColor = FLinearColor(0.5f, 0.5f, 0.5f, 0.6f);
    UPROPERTY(EditAnywhere, Category = "GraspIndicator|Colors")
    FLinearColor OpenColor = FLinearColor(0.2f, 0.5f, 1.0f, 0.85f);
    UPROPERTY(EditAnywhere, Category = "GraspIndicator|Colors")
    FLinearColor HeldColor = FLinearColor(0.15f, 0.9f, 0.25f, 1.0f);
    UPROPERTY(EditAnywhere, Category = "GraspIndicator|Colors")
    FLinearColor LostColor = FLinearColor(1.0f, 0.15f, 0.1f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "GraspIndicator|Material")
    FName StateParam = TEXT("GlyphState");
    UPROPERTY(EditAnywhere, Category = "GraspIndicator|Material")
    FName ColorParam = TEXT("GlyphColor");

private:
    void ResolveDisplayState(float DeltaTime);
    void ApplyDisplayState();

    UPROPERTY()
    TObjectPtr<UMaterialBillboardComponent> Billboard_;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> GlyphMID_;

    UPROPERTY()
    TObjectPtr<UComLink> ComLinkRef_;

    uint8 ArmIndex_ = 0;
    bool  bInitialized_ = false;

    uint32 LastSeenSequence_ = 0;
    bool   bHaveSeenSequence_ = false;
    float  TimeSinceLastSequence_ = 0.f;

    GraspState         LastRawGraspState_ = GraspState::OPEN;
    EGraspDisplayState DisplayState_      = EGraspDisplayState::Unknown;
    EGraspDisplayState LastAppliedState_  = static_cast<EGraspDisplayState>(0xFF);
};

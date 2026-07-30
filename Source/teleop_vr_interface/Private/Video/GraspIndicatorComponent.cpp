#include "Video/GraspIndicatorComponent.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Networking/ComLink.h"
#include "Video/GhostOverlayComponent.h"

UGraspIndicatorComponent::UGraspIndicatorComponent() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGraspIndicatorComponent::Initialize(UGhostOverlayComponent* Ghost, UComLink* InComLink, uint8 InArmIndex) {
    if (!Ghost || !InComLink) return;

    ComLinkRef_ = InComLink;
    ArmIndex_   = InArmIndex;

    USceneComponent* Anchor = (ArmIndex_ == 0) ? Ghost->GetLeftWristAnchor() : Ghost->GetRightWristAnchor();
    if (!Anchor) return;

    AActor* Owner = GetOwner();
    if (!Owner) return;

    const FName BillboardName = MakeUniqueObjectName(Owner, UMaterialBillboardComponent::StaticClass(), TEXT("GraspGlyphBillboard"));
    Billboard_ = NewObject<UMaterialBillboardComponent>(Owner, BillboardName);
    Billboard_->SetupAttachment(Anchor);
    Billboard_->RegisterComponent();
    Billboard_->SetRelativeLocation(GlyphOffset);
    Billboard_->SetCastShadow(false);
    Billboard_->SetVisibleInSceneCaptureOnly(true);

    if (GlyphMaterial) {
        GlyphMID_ = UMaterialInstanceDynamic::Create(GlyphMaterial, this);
    }

    UMaterialInterface* ElementMaterial = GlyphMID_ ? static_cast<UMaterialInterface*>(GlyphMID_) : GlyphMaterial.Get();

    FMaterialSpriteElement Element;
    Element.Material              = ElementMaterial;
    Element.BaseSizeX              = GlyphSize;
    Element.BaseSizeY              = GlyphSize;
    Element.bSizeIsInScreenSpace   = false;
    Billboard_->Elements.Add(Element);
    Billboard_->MarkRenderStateDirty();

    Ghost->RegisterOverlayComponent(Billboard_);

    bHaveSeenSequence_     = false;
    TimeSinceLastSequence_ = 0.f;
    DisplayState_          = EGraspDisplayState::Unknown;
    ApplyDisplayState();

    bInitialized_ = true;
}

void UGraspIndicatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bInitialized_ || !ComLinkRef_) return;

    ResolveDisplayState(DeltaTime);
    ApplyDisplayState();
}

void UGraspIndicatorComponent::ResolveDisplayState(float DeltaTime) {
    ArmStateMsg State = ComLinkRef_->PeekArmState(ArmIndex_);

    if (bHaveSeenSequence_ && State.header.sequence == LastSeenSequence_) {
        TimeSinceLastSequence_ += DeltaTime;
    } else {
        LastSeenSequence_      = State.header.sequence;
        bHaveSeenSequence_     = true;
        TimeSinceLastSequence_ = 0.f;
        LastRawGraspState_     = State.grasp_state;
    }

    if (!ComLinkRef_->IsArmAlive(ArmIndex_) || TimeSinceLastSequence_ > StalenessTimeoutS) {
        DisplayState_ = EGraspDisplayState::Unknown;
        return;
    }

    switch (LastRawGraspState_) {
        case GraspState::HELD: DisplayState_ = EGraspDisplayState::Held; break;
        case GraspState::LOST: DisplayState_ = EGraspDisplayState::Lost; break;
        case GraspState::OPEN:
        default:                DisplayState_ = EGraspDisplayState::Open; break;
    }
}

void UGraspIndicatorComponent::ApplyDisplayState() {
    if (DisplayState_ == LastAppliedState_) return;
    LastAppliedState_ = DisplayState_;

    if (!GlyphMID_) return;

    FLinearColor Color;
    float StateValue = 0.f;
    switch (DisplayState_) {
        case EGraspDisplayState::Open:    Color = OpenColor;    StateValue = 1.f; break;
        case EGraspDisplayState::Held:    Color = HeldColor;    StateValue = 2.f; break;
        case EGraspDisplayState::Lost:    Color = LostColor;    StateValue = 3.f; break;
        case EGraspDisplayState::Unknown:
        default:                         Color = UnknownColor; StateValue = 0.f; break;
    }

    GlyphMID_->SetVectorParameterValue(ColorParam, Color);
    GlyphMID_->SetScalarParameterValue(StateParam, StateValue);
}

#include "Video/AlignmentPanelComponent.h"
#include "Video/GhostOverlayComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "HAL/PlatformTime.h"

UAlignmentPanelComponent::UAlignmentPanelComponent() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UAlignmentPanelComponent::Initialize(UGhostOverlayComponent* Ghost, uint8 InArmIndex) {
    if (!Ghost) return;
    GhostRef_ = Ghost;
    ArmIndex_ = InArmIndex;
    USceneComponent* Anchor = Ghost->GetCaptureAnchor();
    AActor* Owner = GetOwner();
    if (!Anchor || !Owner) return;

    Probe_ = NewObject<UStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), TEXT("AlignmentProbe")));
    Probe_->SetupAttachment(Anchor);
    Probe_->RegisterComponent();
    Probe_->SetVisibility(false);
    Probe_->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    Billboard_ = NewObject<UMaterialBillboardComponent>(Owner, MakeUniqueObjectName(Owner, UMaterialBillboardComponent::StaticClass(), TEXT("AlignmentPanel")));
    Billboard_->SetupAttachment(Anchor);
    Billboard_->RegisterComponent();
    Billboard_->SetCastShadow(false);
    Billboard_->SetVisibleInSceneCaptureOnly(true);
    Billboard_->SetVisibility(false);
    Billboard_->TranslucencySortPriority = 10;

    if (!PanelMaterial) PanelMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_AlignmentPanel.M_AlignmentPanel"));
    if (PanelMaterial) MID_ = UMaterialInstanceDynamic::Create(PanelMaterial, this);
    else UE_LOG(LogTemp, Warning, TEXT("AlignmentPanel: no PanelMaterial (expected /Game/Materials/M_AlignmentPanel), panel stays invisible"));

    FMaterialSpriteElement Element;
    Element.Material = MID_ ? static_cast<UMaterialInterface*>(MID_) : PanelMaterial.Get();
    Element.BaseSizeX = SizeCm;
    Element.BaseSizeY = SizeCm;
    Element.bSizeIsInScreenSpace = false;
    Billboard_->Elements.Add(Element);
    Billboard_->MarkRenderStateDirty();

    Ghost->RegisterOverlayComponent(Billboard_);
    bInitialized_ = true;
}

void UAlignmentPanelComponent::SetState(const FAlignmentState& State) {
    FScopeLock Lock(&Mtx_);
    Pending_ = State;
    bPending_ = true;
}

bool UAlignmentPanelComponent::ResolveAnchor(FVector& OutWorld, FVector& OutTargetWorld, bool& bOutHaveTarget) {
    float P[3] = { 0.f, 0.f, 0.f };
    bool bHave = GhostRef_->GetGhostEEPosition(ArmIndex_, P);
    bGhostValid_ = bHave;
    if (bHave) FMemory::Memcpy(GhostPos_, P, sizeof(GhostPos_));
    if (!bHave && State_.bHasAnchor) {
        FMemory::Memcpy(P, State_.Anchor, sizeof(P));
        bHave = true;
    }
    if (!bHave) return false;
    static const float kIdent[4] = { 1.f, 0.f, 0.f, 0.f };
    const float T[3] = { P[0] - State_.PosErr[0], P[1] - State_.PosErr[1], P[2] - State_.PosErr[2] };
    bOutHaveTarget = GhostRef_->ApplyOverlayPose(Probe_, T, kIdent);
    if (bOutHaveTarget) OutTargetWorld = Probe_->GetComponentLocation();
    if (!GhostRef_->ApplyOverlayPose(Probe_, P, kIdent)) return false;
    OutWorld = Probe_->GetComponentLocation();
    return true;
}

bool UAlignmentPanelComponent::ViewAxesProtocol(FVector2D& OutFwd, FVector2D& OutLeft) const {
    if (!bViewAligned) {
        OutFwd = FVector2D(1.f, 0.f);
        OutLeft = FVector2D(0.f, 1.f);
        return true;
    }
    const FQuat R = GhostRef_->GetOverlayCameraQuat();
    const FVector F3 = R.RotateVector(FVector(1.f, 0.f, 0.f));
    FVector2D F(F3.X, F3.Y);
    if (F.Size() < 0.2f) {
        const FVector U3 = R.RotateVector(FVector(0.f, 0.f, 1.f));
        F = FVector2D(U3.X, U3.Y);
    }
    if (F.Size() < KINDA_SMALL_NUMBER) return false;
    F.Normalize();
    OutFwd = F;
    OutLeft = FVector2D(-F.Y, F.X);
    return true;
}

void UAlignmentPanelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bInitialized_ || !GhostRef_ || !Billboard_) return;
    const double Now = FPlatformTime::Seconds();

    {
        FScopeLock Lock(&Mtx_);
        if (bPending_) {
            State_ = Pending_;
            bPending_ = false;
            LastSeen_ = Now;
        }
    }

    const bool bLive = State_.bActive && (Now - LastSeen_) <= HoldS;
    Level_ = FMath::FInterpConstantTo(Level_, bLive ? 1.f : 0.f, DeltaTime, 1.f / FMath::Max(FadeS, KINDA_SMALL_NUMBER));
    if (Level_ < 0.01f) {
        Billboard_->SetVisibility(false);
        bHaveSmoothed_ = false;
        return;
    }

    FVector AnchorWorld, TargetWorld;
    bool bHaveTarget = false;
    FVector2D Fwd, Left;
    if (!ResolveAnchor(AnchorWorld, TargetWorld, bHaveTarget) || !ViewAxesProtocol(Fwd, Left)) {
        Billboard_->SetVisibility(false);
        return;
    }

    const FQuat View = GhostRef_->GetOverlayViewQuatUE();
    if (Side_ == 0.f) Side_ = ArmIndex_ == 0 ? -1.f : 1.f;
    if (bHaveTarget) {
        const float TargetRight = FVector::DotProduct(TargetWorld - AnchorWorld, View.RotateVector(FVector(0.f, 1.f, 0.f)));
        if (TargetRight > SideSwitchCm) Side_ = -1.f;
        else if (TargetRight < -SideSwitchCm) Side_ = 1.f;
    }
    const FVector Target = AnchorWorld + View.RotateVector(FVector(0.f, Side_ * SideOffsetCm, UpOffsetCm));
    if (!bHaveSmoothed_) {
        Smoothed_ = Target;
        bHaveSmoothed_ = true;
    } else {
        const float Alpha = 1.f - FMath::Exp(-DeltaTime / FMath::Max(FollowTauS, 0.001f));
        Smoothed_ = FMath::Lerp(Smoothed_, Target, Alpha);
    }
    Billboard_->SetWorldLocation(Smoothed_);
    Billboard_->SetVisibility(true);

    ApplyParams(Fwd, Left);
}

void UAlignmentPanelComponent::ApplyParams(const FVector2D& Fwd, const FVector2D& Left) {
    if (!MID_) return;
    const FAlignmentState& S = State_;

    const FVector2D E(S.PosErr[0], S.PosErr[1]);
    const float ErrFwd  = FVector2D::DotProduct(E, Fwd);
    const float ErrLeft = FVector2D::DotProduct(E, Left);
    const float Full = bAutoScale
        ? FMath::Max(S.TolXY / FMath::Max(TolRadius, 0.01f), MinFullScaleM)
        : FMath::Max(FixedFullScaleM, 0.001f);
    const float RTol = FMath::Clamp(S.TolXY / Full, 0.02f, 1.f);
    const float U = -ErrLeft / Full;
    const float V = ErrFwd / Full;

    auto PanelDir = [&](float WorldYaw) {
        const FVector2D D(FMath::Cos(WorldYaw), FMath::Sin(WorldYaw));
        return FVector2D(-FVector2D::DotProduct(D, Left), FVector2D::DotProduct(D, Fwd));
    };
    const FVector2D TD = PanelDir(S.TargetYaw);
    const FVector2D SD = PanelDir(S.TargetYaw + S.YawErr);

    const float Dz = S.PosErr[2];
    const float ZNorm = FMath::Clamp(Dz / FMath::Max(S.ZRange, 0.005f), 0.f, 1.f);
    const float Ring = (Dz > 0.001f && !S.bDone) ? RingMin + (RingMax - RingMin) * ZNorm : 0.f;

    const FVector2D Lean(S.Lean[0], S.Lean[1]);
    const float TiltFull = FMath::Max(S.TolTilt * TiltFullScaleFactor, 1e-4f);
    const float PitchMarker = FVector2D::DotProduct(Lean, Fwd) / TiltFull;
    const float RollMarker = -FVector2D::DotProduct(Lean, Left) / TiltFull;
    const float Band = 1.f / FMath::Max(TiltFullScaleFactor, 1.f);
    const bool bPlace = S.Phase == TEXT("place");
    const float Emphasis = S.bTiltActive ? 1.f : (bPlace ? TiltIdleEmphasis : 0.f);

    const float Warn = S.bWedged ? 2.f : (S.bTooFast ? 1.f : 0.f);

    FVector2D CmdUV(U, V);
    bool bCmd = false;
    if (S.bHasCmd) {
        const FVector2D C(S.CmdErr[0], S.CmdErr[1]);
        CmdUV = FVector2D(-FVector2D::DotProduct(C, Left) / Full, FVector2D::DotProduct(C, Fwd) / Full);
        bCmd = true;
    } else if (bGhostValid_ && S.bHasAnchor) {
        const FVector2D C(S.PosErr[0] + GhostPos_[0] - S.Anchor[0], S.PosErr[1] + GhostPos_[1] - S.Anchor[1]);
        CmdUV = FVector2D(-FVector2D::DotProduct(C, Left) / Full, FVector2D::DotProduct(C, Fwd) / Full);
        bCmd = true;
    }
    const float CmdShow = bCmd ? FMath::Clamp((FVector2D::Distance(CmdUV, FVector2D(U, V)) - 0.03f) / 0.05f, 0.f, 1.f) : 0.f;

    MID_->SetVectorParameterValue(XYParam, FLinearColor(U, V, RTol, Level_));
    MID_->SetVectorParameterValue(YawParam, FLinearColor(TD.X, TD.Y, SD.X, SD.Y));
    MID_->SetVectorParameterValue(ZDParam, FLinearColor(Ring, FMath::Clamp(S.Depth, 0.f, 1.f), 0.f, S.bDone ? 1.f : 0.f));
    MID_->SetVectorParameterValue(TiltParam, FLinearColor(PitchMarker, RollMarker, Band, Emphasis));
    MID_->SetVectorParameterValue(CmdParam, FLinearColor(CmdUV.X, CmdUV.Y, CmdShow, 0.f));
    MID_->SetVectorParameterValue(FlagsParam, FLinearColor(S.bXYOk ? 1.f : 0.f, S.bYawOk ? 1.f : 0.f, S.bTiltOk ? 1.f : 0.f, Warn));
}

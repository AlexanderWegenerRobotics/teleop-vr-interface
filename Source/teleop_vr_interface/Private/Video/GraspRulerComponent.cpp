#include "Video/GraspRulerComponent.h"
#include "Video/GhostOverlayComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MaterialBillboardComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"

UGraspRulerComponent::UGraspRulerComponent() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

UStaticMeshComponent* UGraspRulerComponent::MakeMesh(const TCHAR* Name, bool bVisible) {
    AActor* Owner = GetOwner();
    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), Name));
    Mesh->SetupAttachment(GhostRef_->GetCaptureAnchor());
    Mesh->RegisterComponent();
    Mesh->SetCastShadow(false);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetVisibleInSceneCaptureOnly(true);
    Mesh->SetVisibility(bVisible);
    return Mesh;
}

void UGraspRulerComponent::Initialize(UGhostOverlayComponent* Ghost, const FGraspRulerSettings& Settings, bool bDrawBase) {
    if (!Ghost || !GetOwner() || !Ghost->GetCaptureAnchor()) return;
    GhostRef_ = Ghost;
    S_ = Settings;
    bDrawBase_ = bDrawBase;

    if (!RulerMaterial) RulerMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_GraspRuler.M_GraspRuler"));
    if (!RulerMaterial) {
        UE_LOG(LogTemp, Warning, TEXT("GraspRuler: no /Game/Materials/M_GraspRuler, rulers stay invisible (see docs/grasp_ruler.md)"));
        return;
    }

    UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    XMesh_ = MakeMesh(TEXT("GraspRulerX"), false);
    YMesh_ = MakeMesh(TEXT("GraspRulerY"), false);
    Probe_ = MakeMesh(TEXT("GraspRulerProbe"), false);
    XMID_ = UMaterialInstanceDynamic::Create(RulerMaterial, this);
    YMID_ = UMaterialInstanceDynamic::Create(RulerMaterial, this);
    auto Setup = [&](UStaticMeshComponent* Mesh, UMaterialInstanceDynamic* MID) {
        if (Plane) Mesh->SetStaticMesh(Plane);
        for (int32 m = 0; m < Mesh->GetNumMaterials(); ++m) Mesh->SetMaterial(m, MID);
        Mesh->TranslucencySortPriority = bDrawBase_ ? 5 : 6;
        Ghost->RegisterOverlayComponent(Mesh);
    };
    Setup(XMesh_, XMID_);
    Setup(YMesh_, YMID_);

    TapeMID_ = UMaterialInstanceDynamic::Create(RulerMaterial, this);
    AActor* Owner = GetOwner();
    Tape_ = NewObject<UMaterialBillboardComponent>(Owner, MakeUniqueObjectName(Owner, UMaterialBillboardComponent::StaticClass(), TEXT("GraspRulerTape")));
    Tape_->SetupAttachment(Ghost->GetCaptureAnchor());
    Tape_->RegisterComponent();
    Tape_->SetCastShadow(false);
    Tape_->SetVisibleInSceneCaptureOnly(true);
    Tape_->SetVisibility(false);
    Tape_->TranslucencySortPriority = bDrawBase_ ? 5 : 6;
    FMaterialSpriteElement Element;
    Element.Material = TapeMID_.Get();
    Element.BaseSizeX = 100.f * S_.TapeBandWidth / kTapeBandFrac;
    Element.BaseSizeY = 100.f * S_.TapeHeight;
    Element.bSizeIsInScreenSpace = false;
    Tape_->Elements.Add(Element);
    Tape_->MarkRenderStateDirty();
    Ghost->RegisterOverlayComponent(Tape_);

    bInitialized_ = true;
    UE_LOG(LogTemp, Log, TEXT("GraspRuler: x ruler at y=%.3f x=[%.3f, %.3f], y ruler at x=%.3f y=[%.3f, %.3f], tape at (%.3f, %.3f), base %s"),
        S_.XAtY, S_.XFrom, S_.XTo, S_.YAtX, S_.YFrom, S_.YTo, S_.TapeX, S_.TapeY, bDrawBase_ ? TEXT("on") : TEXT("off"));
}

void UGraspRulerComponent::SetState(const FGraspRulerState& State) {
    FScopeLock Lock(&Mtx_);
    Pending_ = State;
    bPending_ = true;
}

bool UGraspRulerComponent::PlaceRuler(UStaticMeshComponent* Mesh, bool bAlongX) {
    const float Wq = S_.BandWidth / kBandFrac;
    float P[3];
    float Q[4] = { 1.f, 0.f, 0.f, 0.f };
    float Len;
    if (bAlongX) {
        Len = FMath::Max(S_.XTo - S_.XFrom, 0.01f);
        P[0] = 0.5f * (S_.XFrom + S_.XTo);
        P[1] = S_.XAtY + S_.XOuter * 0.5f * Wq;
    } else {
        Len = FMath::Max(S_.YTo - S_.YFrom, 0.01f);
        P[0] = S_.YAtX + S_.YOuter * 0.5f * Wq;
        P[1] = 0.5f * (S_.YFrom + S_.YTo);
        Q[0] = UE_INV_SQRT_2;
        Q[3] = UE_INV_SQRT_2;
    }
    P[2] = S_.TableZ + S_.Lift;
    if (!GhostRef_->ApplyOverlayPose(Mesh, P, Q)) return false;
    Mesh->SetWorldScale3D(FVector(Len, Wq, 1.f));
    return true;
}

bool UGraspRulerComponent::PlaceTape() {
    static const float kIdent[4] = { 1.f, 0.f, 0.f, 0.f };
    const float Mid[3] = { S_.TapeX, S_.TapeY, S_.TableZ + 0.5f * S_.TapeHeight };
    if (!GhostRef_->ApplyOverlayPose(Probe_, Mid, kIdent)) return false;
    Tape_->SetWorldLocation(Probe_->GetComponentLocation());
    return true;
}

void UGraspRulerComponent::ApplyRuler(UMaterialInstanceDynamic* MID, bool bAlongX) {
    if (!MID) return;
    const int32 Axis = bAlongX ? 0 : 1;
    const float From = bAlongX ? S_.XFrom : S_.YFrom;
    const float Len = bAlongX ? S_.XTo - S_.XFrom : S_.YTo - S_.YFrom;
    const float Outer = bAlongX ? S_.XOuter : S_.YOuter;
    const int32 Flip = bAlongX ? (Outer > 0.f ? 4 : 0) : (Outer < 0.f ? 4 : 0);
    MID->SetVectorParameterValue(TEXT("Geo"), FLinearColor(Len, kBandFrac, 0.f, static_cast<float>(S_.UVBits ^ Flip)));

    const TArray<FVector2f>& Bands = bAlongX ? State_.BandsX : State_.BandsY;
    float B[6] = { 0.f, -1.f, 0.f, -1.f, 0.f, -1.f };
    for (int32 i = 0; i < 3 && i < Bands.Num(); ++i) {
        B[2 * i] = Bands[i].X - From;
        B[2 * i + 1] = Bands[i].Y - From;
    }
    const FVector2f Ext = bAlongX ? State_.ExtentX : State_.ExtentY;
    MID->SetVectorParameterValue(TEXT("BandA"), FLinearColor(B[0], B[1], B[2], B[3]));
    MID->SetVectorParameterValue(TEXT("BandB"), FLinearColor(B[4], B[5], Ext.X - From, Ext.Y - From));

    float Stat = 0.f;
    if (State_.bValid && State_.bHasAim) Stat = State_.Ok[Axis] ? 2.f : 1.f;
    const float Centre = State_.bHasAim ? State_.Aim[Axis] - From : -1.f;
    MID->SetVectorParameterValue(TEXT("Target"), FLinearColor(Centre, State_.TolXY, State_.Tcp[Axis] - From, Stat));
    MID->SetVectorParameterValue(TEXT("Loupe"), FLinearColor(LoupeLevel_ > 0.01f ? 1.f : 0.f, S_.LoupeGain, S_.LoupeHalfRange, LoupeLevel_));
    MID->SetVectorParameterValue(TEXT("Style"), FLinearColor(S_.BaseAlpha, Level_, S_.BandWidth, bDrawBase_ ? 1.f : 0.f));
}

void UGraspRulerComponent::ApplyTape() {
    if (!TapeMID_) return;
    TapeMID_->SetVectorParameterValue(TEXT("Geo"), FLinearColor(S_.TapeHeight, kTapeBandFrac, 1.f, 0.f));
    TapeMID_->SetVectorParameterValue(TEXT("BandA"), FLinearColor(0.f, -1.f, 0.f, -1.f));
    TapeMID_->SetVectorParameterValue(TEXT("BandB"), FLinearColor(0.f, -1.f, State_.ExtentZ.X - S_.TableZ, State_.ExtentZ.Y - S_.TableZ));
    float Stat = 0.f;
    if (State_.bValid && State_.bHasAim) Stat = State_.bReady ? 3.f : (State_.Ok[2] ? 2.f : 1.f);
    const float Centre = State_.bHasAim ? State_.Aim[2] - S_.TableZ : -1.f;
    TapeMID_->SetVectorParameterValue(TEXT("Target"), FLinearColor(Centre, State_.TolZ, State_.Tcp[2] - S_.TableZ, Stat));
    TapeMID_->SetVectorParameterValue(TEXT("Loupe"), FLinearColor(0.f, 1.f, 0.01f, 0.f));
    TapeMID_->SetVectorParameterValue(TEXT("Style"), FLinearColor(S_.BaseAlpha, Level_, S_.TapeBandWidth, bDrawBase_ ? 1.f : 0.f));
}

void UGraspRulerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bInitialized_ || !GhostRef_) return;
    const double Now = FPlatformTime::Seconds();
    {
        FScopeLock Lock(&Mtx_);
        if (bPending_) {
            if (Pending_.bActive) State_ = Pending_;
            else State_.bActive = false;
            bPending_ = false;
            LastSeen_ = Now;
        }
    }
    const bool bLive = State_.bActive && (Now - LastSeen_) <= S_.HoldS;
    const float Rate = 1.f / FMath::Max(S_.FadeS, KINDA_SMALL_NUMBER);
    Level_ = FMath::FInterpConstantTo(Level_, bLive ? 1.f : 0.f, DeltaTime, Rate);
    LoupeLevel_ = FMath::FInterpConstantTo(LoupeLevel_, (bLive && State_.bNear && State_.bValid) ? 1.f : 0.f, DeltaTime, Rate);
    if (bLive != bWasActive_) {
        bWasActive_ = bLive;
        UE_LOG(LogTemp, Verbose, TEXT("GraspRuler: %s"), bLive ? TEXT("target on") : TEXT("target off"));
    }

    const bool bShow = bDrawBase_ || Level_ > 0.01f;
    const bool bX = bShow && PlaceRuler(XMesh_, true);
    const bool bY = bShow && PlaceRuler(YMesh_, false);
    const bool bT = bShow && PlaceTape();
    XMesh_->SetVisibility(bX);
    YMesh_->SetVisibility(bY);
    Tape_->SetVisibility(bT);
    if (bX) ApplyRuler(XMID_, true);
    if (bY) ApplyRuler(YMID_, false);
    if (bT) ApplyTape();
}

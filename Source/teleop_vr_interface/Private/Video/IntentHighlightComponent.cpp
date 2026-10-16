#include "Video/IntentHighlightComponent.h"
#include "Video/GhostOverlayComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"

UIntentHighlightComponent::UIntentHighlightComponent() {
    PrimaryComponentTick.bCanEverTick = true;
}

void UIntentHighlightComponent::Initialize(UGhostOverlayComponent* Ghost) {
    if (!Ghost) return;
    GhostRef_ = Ghost;
    USceneComponent* Anchor = Ghost->GetCaptureAnchor();
    AActor* Owner = GetOwner();
    if (!Anchor || !Owner) return;

    UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    for (int32 i = 0; i < kMaxDiscs; ++i) {
        FDisc& D = Discs_[i];
        const FName CompName = MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), TEXT("IntentHighlight"));
        D.Mesh = NewObject<UStaticMeshComponent>(Owner, CompName);
        D.Mesh->SetupAttachment(Anchor);
        D.Mesh->RegisterComponent();
        if (PlaneMesh) D.Mesh->SetStaticMesh(PlaneMesh);
        D.Mesh->SetCastShadow(false);
        D.Mesh->SetVisibleInSceneCaptureOnly(true);
        D.Mesh->SetVisibility(false);
        D.Mesh->TranslucencySortPriority = -9;
        if (DiscMaterial) {
            D.MID = UMaterialInstanceDynamic::Create(DiscMaterial, this);
            if (D.MID) {
                D.MID->SetVectorParameterValue(TintParam, Tint);
                for (int32 m = 0; m < D.Mesh->GetNumMaterials(); ++m) D.Mesh->SetMaterial(m, D.MID);
            }
        }
        Ghost->RegisterOverlayComponent(D.Mesh);
    }
    bInitialized_ = true;
}

// Called from the receive thread; applied on the game thread in Tick.
void UIntentHighlightComponent::SetTargets(const TArray<FHighlightTarget>& Targets) {
    FScopeLock Lock(&Mtx_);
    Pending_ = Targets;
    bPending_ = true;
}

void UIntentHighlightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bInitialized_ || !GhostRef_) return;
    const double Now = FPlatformTime::Seconds();

    TArray<FHighlightTarget> Targets;
    bool bApply = false;
    {
        FScopeLock Lock(&Mtx_);
        if (bPending_) { Targets = Pending_; bPending_ = false; bApply = true; }
    }
    if (bApply) {
        for (int32 i = 0; i < kMaxDiscs; ++i) {
            FDisc& D = Discs_[i];
            if (D.Target <= 0.f) continue;
            bool bStillIn = false;
            for (const FHighlightTarget& T : Targets) if (T.Prob >= MinProb && T.Name == D.Name) { bStillIn = true; break; }
            if (!bStillIn) D.Target = 0.f;
        }
        for (const FHighlightTarget& T : Targets) {
            if (T.Prob < MinProb) continue;
            int32 Slot = -1;
            for (int32 i = 0; i < kMaxDiscs; ++i) if (Discs_[i].Name == T.Name) { Slot = i; break; }
            if (Slot < 0) for (int32 i = 0; i < kMaxDiscs; ++i) if (Discs_[i].Target <= 0.f && Discs_[i].Level < 0.01f) { Slot = i; break; }
            if (Slot < 0) continue;
            FDisc& D = Discs_[Slot];
            D.Name = T.Name;
            FMemory::Memcpy(D.Position, T.Position, sizeof(D.Position));
            D.bGoal = T.bGoal;
            if (D.bGoal) D.Position[2] += GoalLiftM;
            D.Target = T.Prob;
            D.LastSeen = Now;
        }
    }

    static const float kIdentQuat[4] = { 1.f, 0.f, 0.f, 0.f };
    for (int32 i = 0; i < kMaxDiscs; ++i) {
        FDisc& D = Discs_[i];
        if (D.Target > 0.f && Now - D.LastSeen > HoldS) D.Target = 0.f;
        D.Level = FMath::FInterpConstantTo(D.Level, D.Target, DeltaTime, 1.f / FMath::Max(FadeS, KINDA_SMALL_NUMBER));
        const bool bVisible = D.Level > 0.01f;
        if (!D.Mesh) continue;
        D.Mesh->SetVisibility(bVisible);
        if (!bVisible) continue;
        if (!GhostRef_->ApplyOverlayPose(D.Mesh, D.Position, kIdentQuat, FRotator::ZeroRotator)) {
            D.Mesh->SetVisibility(false);
            continue;
        }
        const float Radius = D.bGoal ? GoalRadiusCm : RadiusCm;
        const float Scale = (2.2f * Radius) / 100.f;
        D.Mesh->SetWorldScale3D(FVector(Scale, Scale, 1.f));
        if (D.MID) {
            const FQuat Rot = D.Mesh->GetComponentQuat();
            D.MID->SetVectorParameterValue(ContactParam, FLinearColor(D.Mesh->GetComponentLocation()));
            D.MID->SetVectorParameterValue(TanUParam, FLinearColor(Rot.RotateVector(FVector::ForwardVector)));
            D.MID->SetVectorParameterValue(TanVParam, FLinearColor(Rot.RotateVector(FVector::RightVector)));
            D.MID->SetScalarParameterValue(RadiusParam, Radius);
            D.MID->SetScalarParameterValue(SpacingParam, Radius);
            D.MID->SetScalarParameterValue(SevParam, D.Level);
        }
    }
}

#include "Video/WorkspaceBoundaryComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Video/GhostOverlayComponent.h"

UWorkspaceBoundaryComponent::UWorkspaceBoundaryComponent() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UWorkspaceBoundaryComponent::Initialize(UGhostOverlayComponent* Ghost, uint8 InArmIndex) {
    if (!Ghost) return;
    GhostRef_ = Ghost;
    ArmIndex_ = InArmIndex;

    USceneComponent* Anchor = Ghost->GetCaptureAnchor();
    AActor* Owner = GetOwner();
    if (!Anchor || !Owner) return;

    UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));

    static const float kYWallQuat[4]     = { 0.7071f, -0.7071f, 0.f, 0.f };
    static const float kTorsoWallQuat[4] = { 0.7071f, 0.f, 0.7071f, 0.f };
    static const float kIdentQuat[4]     = { 1.f, 0.f, 0.f, 0.f };
    const TCHAR* Names[kNumFaces] = { TEXT("BoundaryTorso"), TEXT("BoundaryLeft"), TEXT("BoundaryRight"), TEXT("BoundaryFloor") };

    for (int32 i = 0; i < kNumFaces; ++i) {
        FFaceState& F = Faces_[i];
        switch (static_cast<EFace>(i)) {
            case EFace::Torso: FMemory::Memcpy(F.PlaneQuat, kTorsoWallQuat, sizeof(F.PlaneQuat)); break;
            case EFace::Left:
            case EFace::Right: FMemory::Memcpy(F.PlaneQuat, kYWallQuat, sizeof(F.PlaneQuat)); break;
            case EFace::Floor: FMemory::Memcpy(F.PlaneQuat, kIdentQuat, sizeof(F.PlaneQuat)); break;
        }

        const FName CompName = MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), Names[i]);
        F.Mesh = NewObject<UStaticMeshComponent>(Owner, CompName);
        F.Mesh->SetupAttachment(Anchor);
        F.Mesh->RegisterComponent();
        if (PlaneMesh) F.Mesh->SetStaticMesh(PlaneMesh);
        F.Mesh->SetCastShadow(false);
        F.Mesh->SetVisibleInSceneCaptureOnly(true);
        F.Mesh->SetVisibility(false);
        F.Mesh->TranslucencySortPriority = -10;

        if (PatchMaterial) {
            F.MID = UMaterialInstanceDynamic::Create(PatchMaterial, this);
            if (F.MID) {
                for (int32 m = 0; m < F.Mesh->GetNumMaterials(); ++m)
                    F.Mesh->SetMaterial(m, F.MID);
            }
        }

        Ghost->RegisterOverlayComponent(F.Mesh);
    }

    bInitialized_ = true;
}

void UWorkspaceBoundaryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bInitialized_ || !GhostRef_) return;

    float EE[3];
    const bool bHaveEE = GhostRef_->GetGhostEEPosition(ArmIndex_, EE);

    for (int32 i = 0; i < kNumFaces; ++i) {
        if (bHaveEE) {
            EvaluateFace(static_cast<EFace>(i), EE, DeltaTime);
        } else {
            FFaceState& F = Faces_[i];
            const float Speed = 1.f / FMath::Max(ReleaseS, KINDA_SMALL_NUMBER);
            F.Sev = FMath::Clamp(FMath::FInterpTo(F.Sev, 0.f, DeltaTime, Speed), 0.f, 1.f);
            if (F.Mesh) F.Mesh->SetVisibility(F.Sev > 0.002f);
        }
    }
}

float UWorkspaceBoundaryComponent::ComputeFaceSeverity(float Distance, float ClosingRate) const {
    const float ttc = Distance / FMath::Max(-ClosingRate, KINDA_SMALL_NUMBER);
    float sTtc = 1.f - FMath::Clamp(ttc / FMath::Max(TtcHorizonS, KINDA_SMALL_NUMBER), 0.f, 1.f);
    if (Distance > TtcEngageDistanceM) sTtc = 0.f;

    const float sDist = 1.f - FMath::Clamp(Distance / FMath::Max(DistFloorM, KINDA_SMALL_NUMBER), 0.f, 1.f);
    return FMath::Max(sTtc, sDist);
}

void UWorkspaceBoundaryComponent::FaceGeometry(EFace Face, const float EEPosition[3], float& OutDistance, float OutClosestPoint[3]) const {
    switch (Face) {
        case EFace::Torso:
            OutDistance = EEPosition[0] - GhostRef_->GetWorkspaceMinX();
            OutClosestPoint[0] = GhostRef_->GetWorkspaceMinX(); OutClosestPoint[1] = EEPosition[1]; OutClosestPoint[2] = EEPosition[2];
            break;
        case EFace::Left:
            OutDistance = GhostRef_->GetWorkspaceMaxY() - EEPosition[1];
            OutClosestPoint[0] = EEPosition[0]; OutClosestPoint[1] = GhostRef_->GetWorkspaceMaxY(); OutClosestPoint[2] = EEPosition[2];
            break;
        case EFace::Right:
            OutDistance = EEPosition[1] - GhostRef_->GetWorkspaceMinY();
            OutClosestPoint[0] = EEPosition[0]; OutClosestPoint[1] = GhostRef_->GetWorkspaceMinY(); OutClosestPoint[2] = EEPosition[2];
            break;
        case EFace::Floor:
        default:
            OutDistance = EEPosition[2] - GhostRef_->GetWorkspaceMinZ();
            OutClosestPoint[0] = EEPosition[0]; OutClosestPoint[1] = EEPosition[1]; OutClosestPoint[2] = GhostRef_->GetWorkspaceMinZ();
            break;
    }
}

void UWorkspaceBoundaryComponent::EvaluateFace(EFace Face, const float EEPosition[3], float DeltaTime) {
    FFaceState& F = Faces_[static_cast<int32>(Face)];

    float d = 0.f;
    float closest[3];
    FaceGeometry(Face, EEPosition, d, closest);

    const float rawRate = F.bHavePrevD ? (d - F.PrevD) / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER) : 0.f;
    F.PrevD = d;
    F.bHavePrevD = true;
    F.PrevRate = FMath::FInterpTo(F.PrevRate, rawRate, DeltaTime, 12.f);

    float target = ComputeFaceSeverity(d, F.PrevRate);

    const bool bWasOn = F.Sev > 0.f;
    if (!bWasOn) {
        if (target < OnThreshold) target = 0.f;
        else F.TimeSinceOn = 0.f;
    } else {
        F.TimeSinceOn += DeltaTime;
        if (target < OffThreshold && F.TimeSinceOn >= MinOnS) target = 0.f;
    }

    const float Speed = (target > F.Sev) ? (1.f / FMath::Max(AttackS, KINDA_SMALL_NUMBER))
                                          : (1.f / FMath::Max(ReleaseS, KINDA_SMALL_NUMBER));
    F.Sev = FMath::Clamp(FMath::FInterpTo(F.Sev, target, DeltaTime, Speed), 0.f, 1.f);

    const bool bVisible = F.Sev > 0.002f;
    if (F.Mesh) F.Mesh->SetVisibility(bVisible);
    if (!bVisible || !F.Mesh) return;

    // ApplyOverlayPose can fail when the closest point rotates behind the head-relative
    // capture camera (same guard the ghost meshes themselves use). Hide rather than leave a
    // stale/misplaced patch on screen — a missed warning tick beats a wrong one.
    if (!GhostRef_->ApplyOverlayPose(F.Mesh, closest, F.PlaneQuat, FRotator::ZeroRotator)) {
        F.Mesh->SetVisibility(false);
        return;
    }

    const float RadiusUU    = FMath::Lerp(RadiusMinCm, RadiusMaxCm, F.Sev);
    const float ScaleFactor = (2.2f * RadiusUU) / 100.f;   // /Engine/BasicShapes/Plane is 100x100uu at scale 1
    F.Mesh->SetWorldScale3D(FVector(ScaleFactor, ScaleFactor, 1.f));

    if (F.MID) {
        const FVector WorldPos = F.Mesh->GetComponentLocation();
        const FQuat   WorldRot = F.Mesh->GetComponentQuat();
        const FVector TanU = WorldRot.RotateVector(FVector::ForwardVector);
        const FVector TanV = WorldRot.RotateVector(FVector::RightVector);

        F.MID->SetVectorParameterValue(ContactParam, FLinearColor(WorldPos));
        F.MID->SetVectorParameterValue(TanUParam,    FLinearColor(TanU));
        F.MID->SetVectorParameterValue(TanVParam,    FLinearColor(TanV));
        F.MID->SetScalarParameterValue(RadiusParam,  RadiusUU);
        F.MID->SetScalarParameterValue(SpacingParam, GridSpacingCm);
        F.MID->SetScalarParameterValue(SevParam,     F.Sev);
    }
}

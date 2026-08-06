#include "Video/GhostOverlayComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent.h"
#include "Components/StereoLayerComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Input/TrackedControllerComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Networking/ComLink.h"
#include "Shared/AvatarTypes.h"
#include "UObject/UnrealType.h"

UGhostOverlayComponent::UGhostOverlayComponent() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

// Resolve references, build the capture/render-target/stereo-layer pipeline.
void UGhostOverlayComponent::BeginPlay(){
    Super::BeginPlay();

    if (!CameraRef) {
        if (AActor* Owner = GetOwner()) {
            CameraRef = Owner->FindComponentByClass<UCameraComponent>();
        }
    }

    LoadAssets();
    CreateRenderTarget();
    CreateSceneCapture();
    CreateStereoLayer();

    if (bStereo_)
        bPipelineReady = CaptureRTLeft && CaptureRTRight && SceneCaptureLeft && SceneCaptureRight && GhostMeshComp;
    else
        bPipelineReady = CaptureRT && SceneCapture && GhostMeshComp && StereoLayer;
    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: BeginPlay complete — pipeline %s (stereo=%s)"),
        bPipelineReady ? TEXT("READY") : TEXT("NOT READY"), bStereo_ ? TEXT("true") : TEXT("false"));

    if (bPipelineReady)
        CreateLatencyMaterial();
}

// Component teardown hook.
void UGhostOverlayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason){
    if (bStereo_ && PostProcessGhostMID_ && CameraRef)
    {
        auto& Blendables = CameraRef->PostProcessSettings.WeightedBlendables.Array;
        Blendables.RemoveAll([this](const FWeightedBlendable& B) { return B.Object == PostProcessGhostMID_; });
    }
    Super::EndPlay(EndPlayReason);
}

// Per-frame update of the ghost pose.
void UGhostOverlayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction){
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bPipelineReady) return;
    CacheArmStates();
    UpdateIntentPoses();
    UpdateLatencyState(DeltaTime);
    UpdateGhostOpacity();
    UpdateGhostPose();
    UpdateLeftArmPose();
}

// Load mesh and material assets from /Game.
void UGhostOverlayComponent::LoadAssets() {
    if (!PostProcessMaterial){
        PostProcessMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_PP_GhostAlpha.M_PP_GhostAlpha"));
    }

    if (!GhostHandMesh) {
        GhostHandMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Visuals/Ghost/ghost_hand.ghost_hand"));
    }

    if (!GhostHandMaterial){
        GhostHandMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Ghost.M_Ghost"));
    }

    if (!GhostLeftFingerMesh) {
        GhostLeftFingerMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Visuals/Ghost/ghost_left_finger.ghost_left_finger"));
    }

    if (!GhostRightFingerMesh) {
        GhostRightFingerMesh = LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Visuals/Ghost/ghost_right_finger.ghost_right_finger"));
    }
}

// Allocate the RGBA8 render target(s) the SceneCapture(s) write into.
void UGhostOverlayComponent::CreateRenderTarget() {
    auto MakeRT = [&](const TCHAR* Name) -> UTextureRenderTarget2D*
    {
        UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetOwner(), Name);
        RT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
        RT->ClearColor         = FLinearColor(0.f, 0.f, 0.f, 0.f);
        RT->bAutoGenerateMips  = false;
        RT->InitAutoFormat(RenderTargetSize.X, RenderTargetSize.Y);
        RT->UpdateResourceImmediate(true);
        return RT;
    };

    if (bStereo_)
    {
        CaptureRTLeft  = MakeRT(TEXT("GhostCaptureRT_L"));
        CaptureRTRight = MakeRT(TEXT("GhostCaptureRT_R"));
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: stereo RTs created (%dx%d, RGBA8 × 2)"), RenderTargetSize.X, RenderTargetSize.Y);
    }
    else
    {
        CaptureRT = MakeRT(TEXT("GhostCaptureRT"));
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: mono RT created (%dx%d, RGBA8)"), RenderTargetSize.X, RenderTargetSize.Y);
    }
}

// Spawn the world-rotation-locked SceneCapture and the ghost hand/finger meshes it sees.
void UGhostOverlayComponent::CreateSceneCapture() {
    AActor* Owner = GetOwner();
    if (!Owner) return;
    // In stereo mode CaptureRT is null; check the eye RTs instead.
    if (bStereo_ && (!CaptureRTLeft || !CaptureRTRight)) return;
    if (!bStereo_ && !CaptureRT) return;

    SceneCapture = NewObject<USceneCaptureComponent2D>(Owner, TEXT("GhostSceneCapture"));
    SceneCapture->SetupAttachment(Owner->GetRootComponent());
    SceneCapture->RegisterComponent();

    SceneCapture->SetUsingAbsoluteRotation(true);
    SceneCapture->SetRelativeLocation(FVector::ZeroVector);
    SceneCapture->SetWorldRotation(FRotator::ZeroRotator);

    SceneCapture->TextureTarget              = CaptureRT;
    SceneCapture->CaptureSource              = ESceneCaptureSource::SCS_FinalColorLDR;
    SceneCapture->PrimitiveRenderMode        = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    SceneCapture->FOVAngle                   = CaptureFOV;
    SceneCapture->bCaptureEveryFrame         = true;
    SceneCapture->bCaptureOnMovement         = false;
    SceneCapture->bAlwaysPersistRenderingState = true;
    SceneCapture->CompositeMode              = SCCM_Overwrite;

    SceneCapture->ShowFlags.SetAtmosphere(false);
    SceneCapture->ShowFlags.SetFog(false);
    SceneCapture->ShowFlags.SetSkyLighting(false);
    SceneCapture->ShowFlags.SetDynamicShadows(false);

    if (PostProcessMaterial){
        SceneCapture->PostProcessSettings.WeightedBlendables.Array.Emplace(1.0f, PostProcessMaterial);
    }
    else {
        UE_LOG(LogTemp, Warning, TEXT("Post Process Material not found"));
    }

    {
        GhostMeshComp = NewObject<UStaticMeshComponent>(Owner, TEXT("GhostMeshComp"));
        GhostMeshComp->SetupAttachment(SceneCapture);
        GhostMeshComp->RegisterComponent();

        GhostMeshComp->SetRelativeLocation(FVector(50.f, 0.f, 0.f));

        if (GhostHandMesh) GhostMeshComp->SetStaticMesh(GhostHandMesh);
        if (GhostHandMaterial) {
            for (int32 i = 0; i < GhostMeshComp->GetNumMaterials(); ++i)
                GhostMeshComp->SetMaterial(i, GhostHandMaterial);
        }

        GhostMeshComp->SetCastShadow(false);
        GhostMeshComp->SetVisibleInSceneCaptureOnly(true);

        SceneCapture->ShowOnlyComponents.Add(GhostMeshComp);

        UE_LOG(LogTemp, Log,
            TEXT("GhostOverlay: ShowOnlyComponents count after AddShowOnlyComponent: %d  mesh registered: %s"),
            SceneCapture->ShowOnlyComponents.Num(),
            GhostMeshComp->IsRegistered() ? TEXT("YES") : TEXT("NO"));
    }

    auto CreateFingerMesh = [&](const TCHAR* CompName, UStaticMesh* Mesh, FVector InitialOffset) -> UStaticMeshComponent* {
        UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner, CompName);
        Comp->SetupAttachment(GhostMeshComp);
        Comp->RegisterComponent();
        Comp->SetRelativeLocation(InitialOffset);
        if (Mesh) Comp->SetStaticMesh(Mesh);
        if (GhostHandMaterial) {
            for (int32 i = 0; i < Comp->GetNumMaterials(); ++i) {
                Comp->SetMaterial(i, GhostHandMaterial);
            }
        }
        Comp->SetCastShadow(false);
        Comp->SetVisibleInSceneCaptureOnly(true);
        SceneCapture->ShowOnlyComponents.Add(Comp);
        return Comp;
    };

    LeftFingerMeshComp  = CreateFingerMesh(TEXT("GhostLeftFinger"), GhostLeftFingerMesh,  LeftFingerOpenOffset);
    RightFingerMeshComp = CreateFingerMesh(TEXT("GhostRightFinger"), GhostRightFingerMesh, RightFingerOpenOffset);

    // ---- Left arm meshes — same SceneCapture, separate pose driven by arm index 0 ----
    {
        UStaticMesh* LeftHandMeshAsset = GhostLeftHandMesh ? GhostLeftHandMesh : GhostHandMesh;
        GhostLeftMeshComp = NewObject<UStaticMeshComponent>(Owner, TEXT("GhostLeftMeshComp"));
        GhostLeftMeshComp->SetupAttachment(SceneCapture);
        GhostLeftMeshComp->RegisterComponent();
        GhostLeftMeshComp->SetRelativeLocation(FVector(50.f, 0.f, 0.f));
        if (LeftHandMeshAsset) GhostLeftMeshComp->SetStaticMesh(LeftHandMeshAsset);
        if (GhostHandMaterial) {
            for (int32 i = 0; i < GhostLeftMeshComp->GetNumMaterials(); ++i)
                GhostLeftMeshComp->SetMaterial(i, GhostHandMaterial);
        }
        GhostLeftMeshComp->SetCastShadow(false);
        GhostLeftMeshComp->SetVisibleInSceneCaptureOnly(true);
        SceneCapture->ShowOnlyComponents.Add(GhostLeftMeshComp);
    }

    LeftArmLeftFingerMeshComp  = CreateFingerMesh(TEXT("GhostLeftArmLFinger"), GhostLeftFingerMesh,  LeftFingerOpenOffset);
    LeftArmRightFingerMeshComp = CreateFingerMesh(TEXT("GhostLeftArmRFinger"), GhostRightFingerMesh, RightFingerOpenOffset);

    LeftArmLeftFingerMeshComp->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
    LeftArmLeftFingerMeshComp->AttachToComponent(GhostLeftMeshComp, FAttachmentTransformRules::KeepRelativeTransform);
    LeftArmRightFingerMeshComp->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
    LeftArmRightFingerMeshComp->AttachToComponent(GhostLeftMeshComp, FAttachmentTransformRules::KeepRelativeTransform);

    // ---- Stereo: create per-eye captures at ±IPD offset from the centre anchor ----
    if (bStereo_)
    {
        // Centre capture is now just an anchor for the mesh hierarchy — disable its render.
        SceneCapture->bCaptureEveryFrame = false;
        SceneCapture->bCaptureOnMovement = false;
        SceneCapture->TextureTarget      = nullptr;

        auto MakeEyeCapture = [&](const TCHAR* Name, UTextureRenderTarget2D* RT, float EyeY)
            -> USceneCaptureComponent2D*
        {
            USceneCaptureComponent2D* Cap = NewObject<USceneCaptureComponent2D>(Owner, Name);
            Cap->SetupAttachment(Owner->GetRootComponent());
            Cap->RegisterComponent();
            Cap->SetUsingAbsoluteRotation(true);
            Cap->SetWorldRotation(FRotator::ZeroRotator);
            Cap->SetRelativeLocation(FVector(0.f, EyeY, 0.f));

            Cap->TextureTarget               = RT;
            Cap->CaptureSource               = ESceneCaptureSource::SCS_FinalColorLDR;
            Cap->PrimitiveRenderMode         = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
            Cap->FOVAngle                    = StereoCaptureFOV;
            Cap->bCaptureEveryFrame          = true;
            Cap->bCaptureOnMovement          = false;
            Cap->bAlwaysPersistRenderingState = true;
            Cap->CompositeMode               = SCCM_Overwrite;

            Cap->ShowFlags.SetAtmosphere(false);
            Cap->ShowFlags.SetFog(false);
            Cap->ShowFlags.SetSkyLighting(false);
            Cap->ShowFlags.SetDynamicShadows(false);

            if (PostProcessMaterial)
                Cap->PostProcessSettings.WeightedBlendables.Array.Emplace(1.0f, PostProcessMaterial);

            // Mirror the same ShowOnly list that was built for the mono capture.
            for (auto& Comp : SceneCapture->ShowOnlyComponents)
                Cap->ShowOnlyComponents.Add(Comp);

            return Cap;
        };

        // Left eye = -Y in camera-local space (camera faces +X, right = +Y).
        SceneCaptureLeft  = MakeEyeCapture(TEXT("GhostCapL"), CaptureRTLeft,  -StereoEyeOffsetCm);
        SceneCaptureRight = MakeEyeCapture(TEXT("GhostCapR"), CaptureRTRight, +StereoEyeOffsetCm);

        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: stereo eye captures created (±%.2f cm, FOV=%.0f°, %d meshes each)"),
            StereoEyeOffsetCm, StereoCaptureFOV, SceneCaptureLeft->ShowOnlyComponents.Num());
    }

    // Re-apply any overlay primitives registered before the captures existed.
    for (UPrimitiveComponent* Comp : RegisteredOverlayComponents_) {
        if (!Comp) continue;
        if (SceneCapture)      SceneCapture->ShowOnlyComponents.AddUnique(Comp);
        if (SceneCaptureLeft)  SceneCaptureLeft->ShowOnlyComponents.AddUnique(Comp);
        if (SceneCaptureRight) SceneCaptureRight->ShowOnlyComponents.AddUnique(Comp);
    }
}

// Additive overlay hook — registers Comp into the ghost's SceneCapture ShowOnlyList(s) so it
// renders alongside the ghost meshes without touching any existing ghost rendering/pose logic.
void UGhostOverlayComponent::RegisterOverlayComponent(UPrimitiveComponent* Comp) {
    if (!Comp) return;
    RegisteredOverlayComponents_.AddUnique(Comp);

    if (SceneCapture)      SceneCapture->ShowOnlyComponents.AddUnique(Comp);
    if (SceneCaptureLeft)  SceneCaptureLeft->ShowOnlyComponents.AddUnique(Comp);
    if (SceneCaptureRight) SceneCaptureRight->ShowOnlyComponents.AddUnique(Comp);
}

void UGhostOverlayComponent::CreateStereoLayer(){
    AActor* Owner = GetOwner();

    if (bStereo_)
    {
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: stereo mode — ghost RTs will be bound to video material by OperatorPawn"));
        return;
    }

    // ---- Mono path: face-locked StereoLayer ----
    if (!Owner || !CaptureRT) return;

    USceneComponent* AttachTo = CameraRef ? Cast<USceneComponent>(CameraRef) : Owner->GetRootComponent();

    StereoLayer = NewObject<UStereoLayerComponent>(Owner, TEXT("GhostStereoLayer"));
    StereoLayer->SetupAttachment(AttachTo);
    StereoLayer->RegisterComponent();
    StereoLayer->SetRelativeLocation(FVector(PlaneDistance, 0.f, 0.f));
    StereoLayer->SetRelativeRotation(FRotator::ZeroRotator);

    StereoLayer->bLiveTexture    = true;
    StereoLayer->bNoAlphaChannel = false;
    StereoLayer->bSupportsDepth  = false;
    StereoLayer->SetPriority(1);
    StereoLayer->SetTexture(CaptureRT);
    if (FByteProperty* TypeProp = FindFProperty<FByteProperty>(StereoLayer->GetClass(), TEXT("StereoLayerType"))){
        TypeProp->SetPropertyValue_InContainer(StereoLayer, static_cast<uint8>(SLT_FaceLocked));
    }
    else{
        UE_LOG(LogTemp, Warning, TEXT("GhostOverlay: StereoLayerType property not found — layer may be world-locked"));
    }

    UpdateLayerSize();
}

// Size the stereo layer quad from PlaneDistance, FOVCoverage and RT aspect.
void UGhostOverlayComponent::UpdateLayerSize() {
    if (!StereoLayer) return;
    const float HFOVRad   = FMath::DegreesToRadians(110.f * FOVCoverage);
    const float QuadWidth = 2.f * PlaneDistance * FMath::Tan(HFOVRad * 0.5f);
    const float Aspect    = RenderTargetSize.Y > 0
        ? static_cast<float>(RenderTargetSize.X) / static_cast<float>(RenderTargetSize.Y)
        : 16.f / 9.f;
    const float QuadHeight = QuadWidth / Aspect;

    StereoLayer->SetQuadSize(FVector2D(QuadWidth, QuadHeight));
}

// Drive a pair of finger meshes between open/closed based on grip state.
void UGhostOverlayComponent::UpdateFingerPose(UStaticMeshComponent* LFinger, UStaticMeshComponent* RFinger, UTrackedControllerComponent* Tracked){
    if (!LFinger || !RFinger) return;
    const bool bGrasping = Tracked && Tracked->IsGraspHeld();
    LFinger->SetRelativeLocation(bGrasping ? LeftFingerClosedOffset  : LeftFingerOpenOffset);
    RFinger->SetRelativeLocation(bGrasping ? RightFingerClosedOffset : RightFingerOpenOffset);
}

FQuat UGhostOverlayComponent::UpdateCaptureTransforms(const FQuat& R_HW_Protocol)
{
    const FQuat CamRotUE = CoordConvert::ProtocolToUnrealQuat(
        R_HW_Protocol.W, R_HW_Protocol.X, R_HW_Protocol.Y, R_HW_Protocol.Z);

    if (bStereo_ && SceneCaptureLeft && SceneCaptureRight)
    {
        SceneCaptureLeft->SetWorldRotation(CamRotUE);
        SceneCaptureRight->SetWorldRotation(CamRotUE);

        const FVector PawnPos = GetOwner()->GetActorLocation();
        SceneCaptureLeft->SetWorldLocation(
            PawnPos + CamRotUE.RotateVector(FVector(0.f, -StereoEyeOffsetCm, 0.f)));
        SceneCaptureRight->SetWorldLocation(
            PawnPos + CamRotUE.RotateVector(FVector(0.f, +StereoEyeOffsetCm, 0.f)));
    }

    return CamRotUE;
}

// ---------------------------------------------------------------------------
// SetIntentPose — called by OperatorPawn::SendArmCommands() every tick.
// Freezes the stored pose (no-op) while the clutch is active so the ghost
// holds position during operator repositioning.
// ---------------------------------------------------------------------------
// Seed the command origin with the engage-time arm EE pose and zero the deltas, so the
// ghost starts exactly on the arm and leads from there.
void UGhostOverlayComponent::SeedIntentPose(uint8 ArmIndex, const float Position[3], const float Quaternion[4]) {
    if (ArmIndex > 1) return;
    FIntentPose& P = IntentPoses_[ArmIndex];

    for (int i = 0; i < 3; ++i) P.OriginPosition[i]   = Position[i];
    for (int i = 0; i < 4; ++i) P.OriginQuaternion[i] = Quaternion[i];
    for (int i = 0; i < 3; ++i) P.Position[i]         = Position[i];
    for (int i = 0; i < 4; ++i) P.Quaternion[i]       = Quaternion[i];

    P.LastDeltaPosition[0] = P.LastDeltaPosition[1] = P.LastDeltaPosition[2] = 0.f;
    P.LastDeltaQuaternion[0] = 1.f; P.LastDeltaQuaternion[1] = 0.f;
    P.LastDeltaQuaternion[2] = 0.f; P.LastDeltaQuaternion[3] = 0.f;
    P.PrevDeltaPosition[0] = P.PrevDeltaPosition[1] = P.PrevDeltaPosition[2] = 0.f;
    P.PrevDeltaQuaternion[0] = 1.f; P.PrevDeltaQuaternion[1] = 0.f;
    P.PrevDeltaQuaternion[2] = 0.f; P.PrevDeltaQuaternion[3] = 0.f;

    P.bHavePrevArm = false;
    P.bPastBounds = false;
    P.bSeeded = true;
}

void UGhostOverlayComponent::UnseedIntentPose(uint8 ArmIndex) {
    if (ArmIndex > 1) return;
    IntentPoses_[ArmIndex].bSeeded = false;
}

// Record the latest operator command delta. Pose math happens in UpdateIntentPoses().
// The delta is continuous across clutch (the controller banks it), so it is recorded
// even while clutched — the ghost simply holds, since the delta does not change.
void UGhostOverlayComponent::SetIntentPose(uint8 ArmIndex, const float DeltaPosition[3], const float DeltaQuaternion[4], float Gripper, bool bClutchActive) {
    if (ArmIndex > 1) return;

    FIntentPose& P = IntentPoses_[ArmIndex];
    if (!P.bSeeded) return;
    if (bClutchActive) { P.Gripper = Gripper; return; }  // freeze delta during clutch

    for (int i = 0; i < 3; ++i) P.LastDeltaPosition[i]   = DeltaPosition[i];
    for (int i = 0; i < 4; ++i) P.LastDeltaQuaternion[i] = DeltaQuaternion[i];
    P.Gripper = Gripper;
}

// Read each arm stream exactly once per tick. Read() clears the stream's HasNew flag,
// so this is the single consumption point; everything downstream uses the cache.
void UGhostOverlayComponent::CacheArmStates() {
    for (uint8 Arm = 0; Arm < 2; ++Arm) {
        if (ComLinkRef && ComLinkRef->HasNewArmState(Arm)) {
            CachedArmState_[Arm]   = ComLinkRef->ReadArmState(Arm);
            bArmStateFresh_[Arm]   = true;
        } else {
            bArmStateFresh_[Arm] = false;
        }
    }
}

bool UGhostOverlayComponent::IsWithinWorkspace(const float Pos[3]) const {
    const float Margin = WorkspaceBoundaryMarginM_;
    return Pos[0] >= WorkspaceMinX_ + Margin
        && Pos[1] <= WorkspaceMaxY_ - Margin
        && Pos[1] >= WorkspaceMinY_ + Margin
        && Pos[2] >= WorkspaceLowerBoundZ_ + Margin;
}

bool UGhostOverlayComponent::GetGhostEEPosition(uint8 ArmIndex, float OutPosition[3]) const {
    if (ArmIndex > 1) return false;
    const FIntentPose& P = IntentPoses_[ArmIndex];
    if (!P.bSeeded) return false;
    OutPosition[0] = P.Position[0];
    OutPosition[1] = P.Position[1];
    OutPosition[2] = P.Position[2];
    return true;
}

// Raw command-target intent display:
//   ghost = Origin ⊕ command delta            (Origin·M·R_cmd·Mᵀ for orientation)
// This leads the arm and shows the full operator intent, including driving past a
// workspace limit (e.g. into the table) so the operator can see and correct it.
//
// To still settle back onto the real arm in reachable space, the Origin is nudged toward
// the measured arm origin (arm EE ⊖ command delta) — but ONLY while the operator is at
// rest and the command is in-bounds. That gate keeps the live lead intact during motion
// and prevents a deliberate over-command into a limit from being "corrected" away.
void UGhostOverlayComponent::UpdateIntentPoses() {
    bGhostPastBounds_ = false;

    for (uint8 Arm = 0; Arm < 2; ++Arm) {
        FIntentPose& P = IntentPoses_[Arm];
        if (!P.bSeeded || !bUseIntentPose) continue;

        const FQuat& M = ControllerToEEQuat[Arm];

        // Command delta as quaternions (stored w,x,y,z → FQuat x,y,z,w).
        const FQuat LastDeltaQ(P.LastDeltaQuaternion[1], P.LastDeltaQuaternion[2], P.LastDeltaQuaternion[3], P.LastDeltaQuaternion[0]);
        const FQuat RetargetedDeltaQ = (M * LastDeltaQ * M.Inverse());   // EE-frame command rotation

        // --- Guarded origin correction (at rest + in-bounds only) ----------------
        // "At rest": this tick's command delta barely moved from the previous one.
        const float dP = FMath::Sqrt(
            FMath::Square(P.LastDeltaPosition[0] - P.PrevDeltaPosition[0]) +
            FMath::Square(P.LastDeltaPosition[1] - P.PrevDeltaPosition[1]) +
            FMath::Square(P.LastDeltaPosition[2] - P.PrevDeltaPosition[2]));
        const FQuat PrevDeltaQ(P.PrevDeltaQuaternion[1], P.PrevDeltaQuaternion[2], P.PrevDeltaQuaternion[3], P.PrevDeltaQuaternion[0]);
        const float dA = LastDeltaQ.AngularDistance(PrevDeltaQ);
        const bool bAtRest = (dP < RestPosEpsilonM) && (dA < RestAngEpsilonRad);

        // Provisional command target (used for the in-bounds gate and, unless capped,
        // for the displayed pose).
        float Target[3];
        for (int i = 0; i < 3; ++i) Target[i] = P.OriginPosition[i] + P.LastDeltaPosition[i];
        const bool bInBounds = IsWithinWorkspace(Target);

        // "Arm settled": the measured EE barely moved between the last two fresh states,
        // i.e. it has finished catching up to the ghost rather than still flying toward
        // it. Without this gate the origin correction fires the instant the operator
        // stops and drags the ghost back to meet the still-lagging arm.
        bool bArmSettled = false;
        if (bArmStateFresh_[Arm]) {
            const ArmStateMsg& S = CachedArmState_[Arm];
            if (P.bHavePrevArm) {
                const float adP = FMath::Sqrt(
                    FMath::Square(S.position[0] - P.PrevArmPosition[0]) +
                    FMath::Square(S.position[1] - P.PrevArmPosition[1]) +
                    FMath::Square(S.position[2] - P.PrevArmPosition[2]));
                const FQuat ArmQnow(S.quaternion[1], S.quaternion[2], S.quaternion[3], S.quaternion[0]);
                const FQuat ArmQprev(P.PrevArmQuaternion[1], P.PrevArmQuaternion[2], P.PrevArmQuaternion[3], P.PrevArmQuaternion[0]);
                bArmSettled = (adP < RestPosEpsilonM) && (ArmQnow.AngularDistance(ArmQprev) < RestAngEpsilonRad);
            }
            for (int i = 0; i < 3; ++i) P.PrevArmPosition[i]   = S.position[i];
            for (int i = 0; i < 4; ++i) P.PrevArmQuaternion[i] = S.quaternion[i];
            P.bHavePrevArm = true;
        }

        if (bArmStateFresh_[Arm] && bAtRest && bArmSettled && bInBounds && OriginConvergeRate > 0.f) {
            const ArmStateMsg& S = CachedArmState_[Arm];
            // Measured origin estimate = arm EE ⊖ command delta.
            const float k = FMath::Clamp(OriginConvergeRate, 0.f, 1.f);
            for (int i = 0; i < 3; ++i) {
                const float OriginEst = S.position[i] - P.LastDeltaPosition[i];
                P.OriginPosition[i] += k * (OriginEst - P.OriginPosition[i]);
            }
            const FQuat ArmQ(S.quaternion[1], S.quaternion[2], S.quaternion[3], S.quaternion[0]);
            const FQuat OriginEstQ = ArmQ * RetargetedDeltaQ.Inverse();
            FQuat OriginQ(P.OriginQuaternion[1], P.OriginQuaternion[2], P.OriginQuaternion[3], P.OriginQuaternion[0]);
            OriginQ = FQuat::Slerp(OriginQ, OriginEstQ, k).GetNormalized();
            P.OriginQuaternion[0] = OriginQ.W; P.OriginQuaternion[1] = OriginQ.X;
            P.OriginQuaternion[2] = OriginQ.Y; P.OriginQuaternion[3] = OriginQ.Z;

            // Recompute target after the origin nudge.
            for (int i = 0; i < 3; ++i) Target[i] = P.OriginPosition[i] + P.LastDeltaPosition[i];
        }

        // --- Displayed pose ------------------------------------------------------
        P.bPastBounds = !IsWithinWorkspace(Target);
        bGhostPastBounds_ |= P.bPastBounds;

        if (bClampGhostToWorkspace && P.bPastBounds) {
            const float Margin = WorkspaceBoundaryMarginM_;
            Target[0] = FMath::Max(Target[0], WorkspaceMinX_ + Margin);
            Target[1] = FMath::Clamp(Target[1], WorkspaceMinY_ + Margin, WorkspaceMaxY_ - Margin);
            Target[2] = FMath::Max(Target[2], WorkspaceLowerBoundZ_ + Margin);
        }
        for (int i = 0; i < 3; ++i) P.Position[i] = Target[i];

        const FQuat OriginQ(P.OriginQuaternion[1], P.OriginQuaternion[2], P.OriginQuaternion[3], P.OriginQuaternion[0]);
        const FQuat NewQ = (OriginQ * RetargetedDeltaQ).GetNormalized();
        P.Quaternion[0] = NewQ.W; P.Quaternion[1] = NewQ.X;
        P.Quaternion[2] = NewQ.Y; P.Quaternion[3] = NewQ.Z;

        for (int i = 0; i < 3; ++i) P.PrevDeltaPosition[i]   = P.LastDeltaPosition[i];
        for (int i = 0; i < 4; ++i) P.PrevDeltaQuaternion[i] = P.LastDeltaQuaternion[i];
    }

    // Refresh the tint when the past-limit state flips (ApplyLatencyColor reads the flag).
    if (bGhostPastBounds_ != bPrevGhostPastBounds_) {
        bPrevGhostPastBounds_ = bGhostPastBounds_;
        ApplyLatencyColor();
    }
}

bool UGhostOverlayComponent::ApplyArmPoseToMesh(UStaticMeshComponent* Mesh, const float Position[3], const float Quaternion[4], const FRotator& EEOffset, FQuat* OutR_HW) {
    float Pan = 0.f, Tilt = 0.f;
    if (ComLinkRef && ComLinkRef->IsHeadAlive()) {
        const HeadStateMsg H = ComLinkRef->ReadHeadState();
        Pan  = H.pan;
        Tilt = H.tilt;
    }

    const FVector p_EE(Position[0],   Position[1],   Position[2]);
    const FQuat   q_EE(Quaternion[1], Quaternion[2], Quaternion[3], Quaternion[0]);

    const FQuat R_HW_pan (FVector(0,  0, 1), Pan);
    const FQuat R_HW_tilt(FVector(0, -1, 0), Tilt);
    const FQuat R_HW = R_HW_pan * R_HW_tilt;
    const FQuat R_CH = R_HW.Inverse();

    const FVector p_cam = R_CH.RotateVector(p_EE - HeadBasePosition) - CamOffsetInHead;
    if (p_cam.X <= 0.f) return false;

    const FQuat   q_cam  = R_CH * q_EE;
    const FVector posUE  = CoordConvert::ProtocolToUnreal(p_cam.X, p_cam.Y, p_cam.Z);
    const FQuat   rotUE  = CoordConvert::ProtocolToUnrealQuat(q_cam.W, q_cam.X, q_cam.Y, q_cam.Z);
    const FQuat   finRot = rotUE * FQuat(EEOffset);

    if (bStereo_) {
        const FQuat CamRotUE = UpdateCaptureTransforms(R_HW);
        Mesh->SetWorldLocationAndRotation(
            GetOwner()->GetActorLocation() + CamRotUE.RotateVector(posUE),
            CamRotUE * finRot);
    }
    else {
        Mesh->SetRelativeLocationAndRotation(posUE, finRot);
    }

    if (OutR_HW) *OutR_HW = R_HW;
    return true;
}

// ---------------------------------------------------------------------------
// UpdateGhostPose — right arm (index 1).
// Priority: intent pose (if bUseIntentPose) → twin/state → VR controller.
// ---------------------------------------------------------------------------
void UGhostOverlayComponent::UpdateGhostPose()
{
    constexpr uint8 kRightArm = 1;

    // --- Intent cursor path (new) -------------------------------------------
    if (bUseIntentPose && IntentPoses_[kRightArm].bSeeded)
    {
        const FIntentPose& I = IntentPoses_[kRightArm];
        if (ApplyArmPoseToMesh(GhostMeshComp, I.Position, I.Quaternion, EEFrameOffset))
            UpdateFingerPose(LeftFingerMeshComp, RightFingerMeshComp, RightTrackedRef);
        return;
    }

    // --- Original twin/state path (uses the per-tick cache) ------------------
    if (bArmStateFresh_[kRightArm])
    {
        const ArmStateMsg& A = CachedArmState_[kRightArm];
        if (ApplyArmPoseToMesh(GhostMeshComp, A.position, A.quaternion, EEFrameOffset))
            UpdateFingerPose(LeftFingerMeshComp, RightFingerMeshComp, RightTrackedRef);
        return;
    }

    if (ComLinkRef && ComLinkRef->IsArmAlive(kRightArm)) return;
    if (!RightHandRef || !CameraRef) return;

    const FTransform& HandTM = RightHandRef->GetComponentTransform();
    const FTransform& CamTM  = CameraRef->GetComponentTransform();

    if (bStereo_) {
        GhostMeshComp->SetWorldLocationAndRotation(
            HandTM.GetLocation(),
            (CamTM.GetRotation().Inverse() * HandTM.GetRotation()) * FQuat(EEFrameOffset));
    }
    else {
        const FTransform& RefTM = SceneCapture->GetComponentTransform();
        const FVector RelPos = RefTM.InverseTransformPosition(HandTM.GetLocation());
        const FQuat   RelRot = RefTM.GetRotation().Inverse()
                             * (CamTM.GetRotation().Inverse() * HandTM.GetRotation())
                             * FQuat(EEFrameOffset);
        GhostMeshComp->SetRelativeLocationAndRotation(RelPos, RelRot);
    }
    UpdateFingerPose(LeftFingerMeshComp, RightFingerMeshComp, RightTrackedRef);
}

// ---------------------------------------------------------------------------
// CreateLatencyMaterial
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::CreateLatencyMaterial()
{
    if (!GhostHandMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("GhostOverlay: no GhostHandMaterial — latency tinting disabled"));
        return;
    }

    GhostMID_ = UMaterialInstanceDynamic::Create(GhostHandMaterial, GetOwner());
    if (!GhostMID_)
    {
        UE_LOG(LogTemp, Warning, TEXT("GhostOverlay: failed to create MID — latency tinting disabled"));
        return;
    }

    // Right-arm MID (GhostMID_) already created above.
    GhostLeftMID_ = UMaterialInstanceDynamic::Create(GhostHandMaterial, GetOwner());
    if (!GhostLeftMID_)
    {
        UE_LOG(LogTemp, Warning, TEXT("GhostOverlay: failed to create left-arm MID — falling back to shared"));
        GhostLeftMID_ = GhostMID_;
    }

    auto ApplyMID = [&](UStaticMeshComponent* Mesh, UMaterialInstanceDynamic* MID)
    {
        if (!Mesh || !MID) return;
        for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
            Mesh->SetMaterial(i, MID);
    };
    ApplyMID(GhostMeshComp,             GhostMID_);
    ApplyMID(LeftFingerMeshComp,        GhostMID_);
    ApplyMID(RightFingerMeshComp,       GhostMID_);
    ApplyMID(GhostLeftMeshComp,         GhostLeftMID_);
    ApplyMID(LeftArmLeftFingerMeshComp, GhostLeftMID_);
    ApplyMID(LeftArmRightFingerMeshComp,GhostLeftMID_);

    ApplyLatencyColor();
}

// ---------------------------------------------------------------------------
// UpdateLatencyState  — called every tick before UpdateGhostPose
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::UpdateLatencyState(float DeltaTime)
{
    if (!GhostMID_) return;

    const float Alpha = FMath::Clamp(DeltaTime * 5.f, 0.f, 1.f);
    SmoothedLatencyMs_ = FMath::Lerp(SmoothedLatencyMs_, RawLatencyMs_, Alpha);

    uint8 NewLevel = LatencyLevel_;
    switch (LatencyLevel_)
    {
        case 0: // OK
            if (SmoothedLatencyMs_ > LatencyWarnMs)   NewLevel = 1;
            break;
        case 1: // Warning
            if      (SmoothedLatencyMs_ < LatencyOkMs)  NewLevel = 0;
            else if (SmoothedLatencyMs_ > LatencyBadMs)  NewLevel = 2;
            break;
        case 2: // Bad
            if (SmoothedLatencyMs_ < LatencyBadExitMs) NewLevel = 1;
            break;
        default: NewLevel = 0; break;
    }

    if (NewLevel != LatencyLevel_)
    {
        LatencyLevel_ = NewLevel;
        ApplyLatencyColor();
        UE_LOG(LogTemp, Log,
            TEXT("GhostOverlay: latency state → %s  (smoothed=%.0f ms)"),
            NewLevel == 0 ? TEXT("OK") : NewLevel == 1 ? TEXT("WARNING") : TEXT("BAD"),
            SmoothedLatencyMs_);
    }
}

// ---------------------------------------------------------------------------
// ApplyLatencyColor
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::UpdateGhostOpacity()
{
    if (!GhostMID_ || !ComLinkRef) return;

    // Compute per-arm distance between intent pose and actual EE.
    // Arm 1 = right (GhostMID_), Arm 0 = left (GhostLeftMID_).
    constexpr uint8 kRight = 1;
    constexpr uint8 kLeft  = 0;

    auto ArmDelta = [&](uint8 Arm) -> float
    {
        if (!IntentPoses_[Arm].bSeeded || !bArmStateFresh_[Arm]) return 0.f;
        const ArmStateMsg& S = CachedArmState_[Arm];
        const float* IP = IntentPoses_[Arm].Position;
        return FMath::Sqrt(
            FMath::Square(IP[0] - S.position[0]) +
            FMath::Square(IP[1] - S.position[1]) +
            FMath::Square(IP[2] - S.position[2]));
    };

    auto DeltaToOpacity = [&](float Delta) -> float
    {
        const float T = FMath::Clamp(
            (Delta - GhostNearThresholdM_) / FMath::Max(GhostFarThresholdM_ - GhostNearThresholdM_, 1e-4f),
            0.f, 1.f);
        return FMath::Lerp(GhostMinOpacity_, GhostMaxOpacity_, T);
    };

    GhostMID_->SetScalarParameterValue(TEXT("Alpha param"), DeltaToOpacity(ArmDelta(kRight)));
    if (GhostLeftMID_ && GhostLeftMID_ != GhostMID_)
        GhostLeftMID_->SetScalarParameterValue(TEXT("Alpha param"), DeltaToOpacity(ArmDelta(kLeft)));
}

void UGhostOverlayComponent::ApplyLatencyColor()
{
    if (!GhostMID_) return;

    // Command-past-limit takes priority over the latency tint: it is a direct, actionable
    // signal that the operator's command is driving past the workspace (e.g. into the table).
    FLinearColor Color;
    if (bGhostPastBounds_)
    {
        Color = CommandLimitColor;
    }
    else
    {
        switch (LatencyLevel_)
        {
            case 1:  Color = LatencyWarnColor; break;   // amber — pay attention
            case 2:  Color = LatencyBadColor;  break;   // orange-red — ghost is stale
            default: Color = GhostDefaultColor; break;
        }
    }
    GhostMID_->SetVectorParameterValue(LatencyColorParam, Color);
    if (GhostLeftMID_ && GhostLeftMID_ != GhostMID_)
        GhostLeftMID_->SetVectorParameterValue(LatencyColorParam, Color);
}

void UGhostOverlayComponent::UpdateLeftArmPose()
{
    if (!GhostLeftMeshComp) return;

    constexpr uint8 kLeftArm = 0;

    // --- Intent cursor path (new) -------------------------------------------
    if (bUseIntentPose && IntentPoses_[kLeftArm].bSeeded)
    {
        const FIntentPose& I = IntentPoses_[kLeftArm];
        if (ApplyArmPoseToMesh(GhostLeftMeshComp, I.Position, I.Quaternion, LeftEEFrameOffset))
            UpdateFingerPose(LeftArmLeftFingerMeshComp, LeftArmRightFingerMeshComp, LeftTrackedRef);
        return;
    }

    // --- Original twin/state path (uses the per-tick cache) ------------------
    if (bArmStateFresh_[kLeftArm])
    {
        const ArmStateMsg& A = CachedArmState_[kLeftArm];
        if (ApplyArmPoseToMesh(GhostLeftMeshComp, A.position, A.quaternion, LeftEEFrameOffset))
            UpdateFingerPose(LeftArmLeftFingerMeshComp, LeftArmRightFingerMeshComp, LeftTrackedRef);
        return;
    }

    if (ComLinkRef && ComLinkRef->IsArmAlive(kLeftArm)) return;
    if (!LeftHandRef || !CameraRef) return;

    const FTransform& HandTM = LeftHandRef->GetComponentTransform();
    const FTransform& CamTM  = CameraRef->GetComponentTransform();

    if (bStereo_)
    {
        GhostLeftMeshComp->SetWorldLocationAndRotation(
            HandTM.GetLocation(),
            (CamTM.GetRotation().Inverse() * HandTM.GetRotation()) * FQuat(LeftEEFrameOffset));
    }
    else
    {
        const FTransform& RefTM = SceneCapture->GetComponentTransform();
        const FVector RelPos = RefTM.InverseTransformPosition(HandTM.GetLocation());
        const FQuat   RelRot = RefTM.GetRotation().Inverse()
                             * (CamTM.GetRotation().Inverse() * HandTM.GetRotation())
                             * FQuat(LeftEEFrameOffset);
        GhostLeftMeshComp->SetRelativeLocationAndRotation(RelPos, RelRot);
    }
    UpdateFingerPose(LeftArmLeftFingerMeshComp, LeftArmRightFingerMeshComp, LeftTrackedRef);
}

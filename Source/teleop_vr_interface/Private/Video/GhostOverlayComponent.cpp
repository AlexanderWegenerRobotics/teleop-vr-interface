#include "Video/GhostOverlayComponent.h"

#include "Camera/CameraComponent.h"
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
    UpdateLatencyState(DeltaTime);
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
}

// Spawn the face-locked stereo layer (mono) or post-process blendable (stereo).
void UGhostOverlayComponent::CreateStereoLayer(){
    AActor* Owner = GetOwner();

    // ---- Stereo path: ghost RTs are composited inside the video PP material ----
    // VR/OpenXR drops all but the first post-process blendable on the camera.
    // We side-step this by binding the ghost eye RTs to GhostLeft/GhostRight params
    // on the video material (M_StereoVideoFeed).  The binding is done by OperatorPawn
    // after Super::BeginPlay() so both VideoFeed and GhostOverlay are initialised.
    // PostProcessGhostMID_ is left null; bPipelineReady relies on the RTs alone.
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

// Project the right-arm EE world pose (index 1) into the SceneCapture-local frame.
void UGhostOverlayComponent::UpdateGhostPose()
{
    constexpr uint8 kRightArm = 1;
    if (ComLinkRef && ComLinkRef->HasNewArmState(kRightArm))
    {
        const ArmStateMsg A = ComLinkRef->ReadArmState(kRightArm);

        float Pan  = 0.f;
        float Tilt = 0.f;
        if (ComLinkRef->IsHeadAlive()) {
            const HeadStateMsg H = ComLinkRef->ReadHeadState();
            Pan  = H.pan;
            Tilt = H.tilt;
        }

        FVector p_EE(A.position[0],   A.position[1],   A.position[2]);
        FQuat   q_EE(A.quaternion[1], A.quaternion[2], A.quaternion[3], A.quaternion[0]);

        const FQuat R_HW_pan(FVector(0, 0, 1), Pan);
        const FQuat R_HW_tilt(FVector(0, -1, 0), Tilt);
        const FQuat R_HW = R_HW_pan * R_HW_tilt;
        const FQuat R_CH = R_HW.Inverse();

        const FVector p_cam = R_CH.RotateVector(p_EE - HeadBasePosition) - CamOffsetInHead;
        const FQuat   q_cam = R_CH * q_EE;

        if (p_cam.X <= 0.f) return;

        const FVector posUE = CoordConvert::ProtocolToUnreal(p_cam.X, p_cam.Y, p_cam.Z);
        const FQuat   rotUE = CoordConvert::ProtocolToUnrealQuat(q_cam.W, q_cam.X, q_cam.Y, q_cam.Z);
        const FQuat finalRot = rotUE * FQuat(EEFrameOffset);

        if (bStereo_)
        {
            const FQuat CamRotUE = UpdateCaptureTransforms(R_HW);
            GhostMeshComp->SetWorldLocationAndRotation(
                GetOwner()->GetActorLocation() + CamRotUE.RotateVector(posUE),
                CamRotUE * finalRot);
        }
        else
        {
            GhostMeshComp->SetRelativeLocationAndRotation(posUE, finalRot);
        }
        UpdateFingerPose(LeftFingerMeshComp, RightFingerMeshComp, RightTrackedRef);
        return;
    }

    if (ComLinkRef && ComLinkRef->IsArmAlive(kRightArm)) return;
    if (!RightHandRef || !CameraRef) return;

    const FTransform& HandTM  = RightHandRef->GetComponentTransform();
    const FTransform& CamTM   = CameraRef->GetComponentTransform();

    if (bStereo_)
    {
        GhostMeshComp->SetWorldLocationAndRotation(
            HandTM.GetLocation(),
            (CamTM.GetRotation().Inverse() * HandTM.GetRotation()) * FQuat(EEFrameOffset));
    }
    else
    {
        const FTransform& RefTM = SceneCapture->GetComponentTransform();
        const FVector RelPos = RefTM.InverseTransformPosition(HandTM.GetLocation());
        const FQuat   RelRot = RefTM.GetRotation().Inverse() * (CamTM.GetRotation().Inverse() * HandTM.GetRotation()) * FQuat(EEFrameOffset);
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

    auto ApplyMID = [&](UStaticMeshComponent* Mesh)
    {
        if (!Mesh) return;
        for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
            Mesh->SetMaterial(i, GhostMID_);
    };
    ApplyMID(GhostMeshComp);
    ApplyMID(LeftFingerMeshComp);
    ApplyMID(RightFingerMeshComp);
    ApplyMID(GhostLeftMeshComp);
    ApplyMID(LeftArmLeftFingerMeshComp);
    ApplyMID(LeftArmRightFingerMeshComp);

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

void UGhostOverlayComponent::ApplyLatencyColor()
{
    if (!GhostMID_) return;

    FLinearColor Color;
    switch (LatencyLevel_)
    {
        case 1:  Color = LatencyWarnColor; break;   // amber — pay attention
        case 2:  Color = LatencyBadColor;  break;   // orange-red — ghost is stale
        default: Color = FLinearColor::White; break; // no tint
    }
    GhostMID_->SetVectorParameterValue(LatencyColorParam, Color);
}

void UGhostOverlayComponent::UpdateLeftArmPose()
{
    if (!GhostLeftMeshComp) return;

    constexpr uint8 kLeftArm = 0;
    if (ComLinkRef && ComLinkRef->HasNewArmState(kLeftArm))
    {
        const ArmStateMsg A = ComLinkRef->ReadArmState(kLeftArm);

        float Pan  = 0.f;
        float Tilt = 0.f;
        if (ComLinkRef->IsHeadAlive())
        {
            const HeadStateMsg H = ComLinkRef->ReadHeadState();
            Pan  = H.pan;
            Tilt = H.tilt;
        }

        FVector p_EE(A.position[0],   A.position[1],   A.position[2]);
        FQuat   q_EE(A.quaternion[1], A.quaternion[2], A.quaternion[3], A.quaternion[0]);

        const FQuat R_HW_pan (FVector(0,  0, 1), Pan);
        const FQuat R_HW_tilt(FVector(0, -1, 0), Tilt);
        const FQuat R_HW = R_HW_pan * R_HW_tilt;
        const FQuat R_CH = R_HW.Inverse();

        const FVector p_cam = R_CH.RotateVector(p_EE - HeadBasePosition) - CamOffsetInHead;
        const FQuat   q_cam = R_CH * q_EE;

        if (p_cam.X <= 0.f) return;

        const FVector posUE = CoordConvert::ProtocolToUnreal(p_cam.X, p_cam.Y, p_cam.Z);
        const FQuat   rotUE = CoordConvert::ProtocolToUnrealQuat(q_cam.W, q_cam.X, q_cam.Y, q_cam.Z);
        const FQuat finalRot = rotUE * FQuat(LeftEEFrameOffset);

        if (bStereo_)
        {
            const FQuat CamRotUE = UpdateCaptureTransforms(R_HW);
            GhostLeftMeshComp->SetWorldLocationAndRotation(
                GetOwner()->GetActorLocation() + CamRotUE.RotateVector(posUE),
                CamRotUE * finalRot);
        }
        else
        {
            GhostLeftMeshComp->SetRelativeLocationAndRotation(posUE, finalRot);
        }
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

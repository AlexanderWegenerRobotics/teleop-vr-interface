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

    bPipelineReady = CaptureRT && SceneCapture && GhostMeshComp && StereoLayer;
    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: BeginPlay complete — pipeline %s"), bPipelineReady ? TEXT("READY") : TEXT("NOT READY"));

    if (bPipelineReady)
        CreateLatencyMaterial();
}

// Component teardown hook.
void UGhostOverlayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason){
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

// Allocate the RGBA8 render target the SceneCapture writes into.
void UGhostOverlayComponent::CreateRenderTarget() {
    CaptureRT = NewObject<UTextureRenderTarget2D>(GetOwner(), TEXT("GhostCaptureRT"));
    CaptureRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    CaptureRT->ClearColor         = FLinearColor(0.f, 0.f, 0.f, 0.f);
    CaptureRT->bAutoGenerateMips  = false;
    CaptureRT->InitAutoFormat(RenderTargetSize.X, RenderTargetSize.Y);
    CaptureRT->UpdateResourceImmediate(true);

    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: RT created (%dx%d, RGBA8)"), RenderTargetSize.X, RenderTargetSize.Y);
}

// Spawn the world-rotation-locked SceneCapture and the ghost hand/finger meshes it sees.
void UGhostOverlayComponent::CreateSceneCapture() {
    AActor* Owner = GetOwner();
    if (!Owner || !CaptureRT) return;

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

    // Left arm fingers — created attached to SceneCapture initially (same lambda above),
    // then re-parented to GhostLeftMeshComp so they move with the left hand.
    LeftArmLeftFingerMeshComp  = CreateFingerMesh(TEXT("GhostLeftArmLFinger"), GhostLeftFingerMesh,  LeftFingerOpenOffset);
    LeftArmRightFingerMeshComp = CreateFingerMesh(TEXT("GhostLeftArmRFinger"), GhostRightFingerMesh, RightFingerOpenOffset);
    LeftArmLeftFingerMeshComp->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
    LeftArmLeftFingerMeshComp->AttachToComponent(GhostLeftMeshComp, FAttachmentTransformRules::KeepRelativeTransform);
    LeftArmRightFingerMeshComp->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
    LeftArmRightFingerMeshComp->AttachToComponent(GhostLeftMeshComp, FAttachmentTransformRules::KeepRelativeTransform);

    UE_LOG(LogTemp, Log,
        TEXT("GhostOverlay: fingers created — left '%s'  right '%s'  ShowOnly count: %d"),
        GhostLeftFingerMesh  ? *GhostLeftFingerMesh->GetName()  : TEXT("none"),
        GhostRightFingerMesh ? *GhostRightFingerMesh->GetName() : TEXT("none"),
        SceneCapture->ShowOnlyComponents.Num());

    UE_LOG(LogTemp, Log,
        TEXT("GhostOverlay: scene capture created — mesh '%s'  PP '%s'"),
        GhostHandMesh     ? *GhostHandMesh->GetName()     : TEXT("none"),
        PostProcessMaterial ? *PostProcessMaterial->GetName() : TEXT("none"));
}

// Spawn the face-locked stereo layer that displays the capture RT to the operator.
void UGhostOverlayComponent::CreateStereoLayer(){
    AActor* Owner = GetOwner();
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
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: StereoLayerType → FaceLocked (via reflection)"));
    }
    else{
        UE_LOG(LogTemp, Warning, TEXT("GhostOverlay: StereoLayerType property not found — layer may be world-locked"));
    }

    UpdateLayerSize();

    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: stereo layer created at %.0f cm, priority 1"),PlaneDistance);
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
        GhostMeshComp->SetRelativeLocationAndRotation(posUE, finalRot);
        UpdateFingerPose(LeftFingerMeshComp, RightFingerMeshComp, RightTrackedRef);
        return;
    }

    if (ComLinkRef && ComLinkRef->IsArmAlive(kRightArm)) return;
    if (!RightHandRef || !CameraRef) return;

    const FTransform& CaptureTM = SceneCapture->GetComponentTransform();
    const FTransform& HandTM    = RightHandRef->GetComponentTransform();
    const FTransform& CamTM     = CameraRef->GetComponentTransform();

    const FVector RelPos  = CaptureTM.InverseTransformPosition(HandTM.GetLocation());
    const FQuat   RelRot  = CaptureTM.GetRotation().Inverse() * (CamTM.GetRotation().Inverse() * HandTM.GetRotation()) * FQuat(EEFrameOffset);

    GhostMeshComp->SetRelativeLocationAndRotation(RelPos, RelRot);
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
    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: latency MID created, param='%s'"), *LatencyColorParam.ToString());
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
        GhostLeftMeshComp->SetRelativeLocationAndRotation(posUE, finalRot);
        UpdateFingerPose(LeftArmLeftFingerMeshComp, LeftArmRightFingerMeshComp, LeftTrackedRef);
        return;
    }

    if (ComLinkRef && ComLinkRef->IsArmAlive(kLeftArm)) return;
    if (!LeftHandRef || !CameraRef) return;

    const FTransform& CaptureTM = SceneCapture->GetComponentTransform();
    const FTransform& HandTM    = LeftHandRef->GetComponentTransform();
    const FTransform& CamTM     = CameraRef->GetComponentTransform();

    const FVector RelPos = CaptureTM.InverseTransformPosition(HandTM.GetLocation());
    const FQuat   RelRot = CaptureTM.GetRotation().Inverse()
                         * (CamTM.GetRotation().Inverse() * HandTM.GetRotation())
                         * FQuat(LeftEEFrameOffset);

    GhostLeftMeshComp->SetRelativeLocationAndRotation(RelPos, RelRot);
    UpdateFingerPose(LeftArmLeftFingerMeshComp, LeftArmRightFingerMeshComp, LeftTrackedRef);
}

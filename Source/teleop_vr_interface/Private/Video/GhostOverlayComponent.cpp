#include "Video/GhostOverlayComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent.h"
#include "Components/StereoLayerComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Input/TrackedControllerComponent.h"
#include "Materials/MaterialInterface.h"
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
}

// Component teardown hook.
void UGhostOverlayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason){
    Super::EndPlay(EndPlayReason);
}

// Per-frame update of the ghost pose.
void UGhostOverlayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction){
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bPipelineReady) return;
    UpdateGhostPose();
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

// Drive ghost finger meshes between open and closed offsets based on grip state.
void UGhostOverlayComponent::UpdateFingerPose() {
    if (!LeftFingerMeshComp || !RightFingerMeshComp) return;
    const bool bGrasping = RightTrackedRef && RightTrackedRef->IsGraspHeld();
    LeftFingerMeshComp->SetRelativeLocation(bGrasping ? LeftFingerClosedOffset  : LeftFingerOpenOffset);
    RightFingerMeshComp->SetRelativeLocation(bGrasping ? RightFingerClosedOffset : RightFingerOpenOffset);
}

// Project the EE world pose into the SceneCapture-local frame and place the ghost mesh.
void UGhostOverlayComponent::UpdateGhostPose()
{
    if (ComLinkRef && ComLinkRef->HasNewArmState(1))
    {
        const ArmStateMsg A = ComLinkRef->ReadArmState(1);

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
        UpdateFingerPose();
        return;
    }

    if (ComLinkRef && ComLinkRef->IsArmAlive(1)) return;
    if (!RightHandRef || !CameraRef) return;

    const FTransform& CaptureTM = SceneCapture->GetComponentTransform();
    const FTransform& HandTM    = RightHandRef->GetComponentTransform();
    const FTransform& CamTM     = CameraRef->GetComponentTransform();

    const FVector RelPos  = CaptureTM.InverseTransformPosition(HandTM.GetLocation());
    const FQuat   RelRot  = CaptureTM.GetRotation().Inverse() * (CamTM.GetRotation().Inverse() * HandTM.GetRotation()) * FQuat(EEFrameOffset);

    GhostMeshComp->SetRelativeLocationAndRotation(RelPos, RelRot);
    UpdateFingerPose();
}

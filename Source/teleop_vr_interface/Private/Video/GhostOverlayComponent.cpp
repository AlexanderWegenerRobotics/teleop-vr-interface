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

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

UGhostOverlayComponent::UGhostOverlayComponent() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

// ---------------------------------------------------------------------------
// BeginPlay
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: BeginPlay on '%s'"), GetOwner() ? *GetOwner()->GetName() : TEXT("null"));
    if (!CameraRef) {
        if (AActor* Owner = GetOwner())
            CameraRef = Owner->FindComponentByClass<UCameraComponent>();
    }

    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: CameraRef %s"), CameraRef ? TEXT("found") : TEXT("NOT FOUND — stereo layer will attach to root"));

    LoadAssets();
    CreateRenderTarget();
    CreateSceneCapture();
    CreateStereoLayer();

    bPipelineReady = CaptureRT && SceneCapture && GhostMeshComp && StereoLayer;
    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: BeginPlay complete — pipeline %s"), bPipelineReady ? TEXT("READY") : TEXT("NOT READY"));
}

// ---------------------------------------------------------------------------
// EndPlay
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason){
    Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// TickComponent
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction){
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bPipelineReady) return;
    UpdateGhostPose();
}

// ---------------------------------------------------------------------------
// LoadAssets
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::LoadAssets() {

    if (!PostProcessMaterial){
        PostProcessMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_PP_GhostAlpha.M_PP_GhostAlpha"));
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: M_PP_GhostAlpha %s"), PostProcessMaterial ? TEXT("loaded") : TEXT("FAILED — capture alpha will be wrong"));
    }

    if (!GhostHandMesh) {
        GhostHandMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Visuals/Ghost/ghost_hand.ghost_hand"));
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: ghost_hand mesh %s"), GhostHandMesh ? TEXT("loaded") : TEXT("FAILED"));
    }

    if (!GhostHandMaterial){
        GhostHandMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Ghost.M_Ghost"));
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: M_Ghost %s"), GhostHandMaterial ? TEXT("loaded") : TEXT("FAILED"));
    }

    if (!GhostLeftFingerMesh) {
        GhostLeftFingerMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Visuals/Ghost/ghost_left_finger.ghost_left_finger"));
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: ghost_left_finger mesh %s"),GhostLeftFingerMesh ? TEXT("loaded") : TEXT("FAILED — fingers won't render"));
    }

    if (!GhostRightFingerMesh) {
        GhostRightFingerMesh = LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Visuals/Ghost/ghost_right_finger.ghost_right_finger"));
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: ghost_right_finger mesh %s"), GhostRightFingerMesh ? TEXT("loaded") : TEXT("FAILED — fingers won't render"));
    }
}

// ---------------------------------------------------------------------------
// CreateRenderTarget
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::CreateRenderTarget() {
    CaptureRT = NewObject<UTextureRenderTarget2D>(GetOwner(), TEXT("GhostCaptureRT"));
    CaptureRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    CaptureRT->ClearColor         = FLinearColor(0.f, 0.f, 0.f, 0.f);
    CaptureRT->bAutoGenerateMips  = false;
    CaptureRT->InitAutoFormat(RenderTargetSize.X, RenderTargetSize.Y);
    CaptureRT->UpdateResourceImmediate(true);

    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: RT created (%dx%d, RGBA8)"), RenderTargetSize.X, RenderTargetSize.Y);
}

// ---------------------------------------------------------------------------
// CreateSceneCapture
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::CreateSceneCapture()
{
    AActor* Owner = GetOwner();
    if (!Owner || !CaptureRT) return;

    // --- Scene capture component ---
    SceneCapture = NewObject<USceneCaptureComponent2D>(Owner, TEXT("GhostSceneCapture"));
    SceneCapture->SetupAttachment(Owner->GetRootComponent());
    SceneCapture->RegisterComponent();

    // Position at the robot head-camera pivot in world space.
    SceneCapture->SetWorldLocation(CapturePivotPosition);
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

    // Post-process material that writes the correct alpha into CaptureRT.
    if (PostProcessMaterial){
        SceneCapture->PostProcessSettings.WeightedBlendables.Array.Emplace(1.0f, PostProcessMaterial);
    }
    else {
        UE_LOG(LogTemp, Warning, TEXT("Post Process Material not found"));
    }

    // --- Ghost mesh ---
    {
        GhostMeshComp = NewObject<UStaticMeshComponent>(Owner, TEXT("GhostMeshComp"));
        GhostMeshComp->SetupAttachment(SceneCapture);
        GhostMeshComp->RegisterComponent();

        GhostMeshComp->SetRelativeLocation(FVector(50.f, 0.f, 0.f));
        GhostMeshComp->SetRelativeRotation(EEFrameOffset);

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

    // --- Finger meshes (children of GhostMeshComp so they follow EE pose) ---
    // Positions are driven each tick by UpdateGhostPose() based on grip state.
    auto CreateFingerMesh = [&](const TCHAR* CompName, UStaticMesh* Mesh, FVector InitialOffset) -> UStaticMeshComponent*
    {
        UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner, CompName);
        Comp->SetupAttachment(GhostMeshComp);
        Comp->RegisterComponent();
        Comp->SetRelativeLocation(InitialOffset);
        if (Mesh) Comp->SetStaticMesh(Mesh);
        if (GhostHandMaterial)
        {
            for (int32 i = 0; i < Comp->GetNumMaterials(); ++i)
                Comp->SetMaterial(i, GhostHandMaterial);
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

// ---------------------------------------------------------------------------
// CreateStereoLayer
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::CreateStereoLayer(){
    AActor* Owner = GetOwner();
    if (!Owner || !CaptureRT) return;

    USceneComponent* AttachTo = CameraRef
        ? Cast<USceneComponent>(CameraRef)
        : Owner->GetRootComponent();

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
        UE_LOG(LogTemp, Warning,
            TEXT("GhostOverlay: StereoLayerType property not found — layer may be world-locked"));
    }

    UpdateLayerSize();

    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: stereo layer created at %.0f cm, priority 1"),PlaneDistance);
}

// ---------------------------------------------------------------------------
// UpdateLayerSize
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// UpdateFingerPose
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::UpdateFingerPose() {
    if (!LeftFingerMeshComp || !RightFingerMeshComp) return;
    const bool bGrasping = RightTrackedRef && RightTrackedRef->IsGraspHeld();
    LeftFingerMeshComp->SetRelativeLocation(bGrasping ? LeftFingerClosedOffset  : LeftFingerOpenOffset);
    RightFingerMeshComp->SetRelativeLocation(bGrasping ? RightFingerClosedOffset : RightFingerOpenOffset);
}

// ---------------------------------------------------------------------------
// UpdateGhostPose
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::UpdateGhostPose(){

    if (ComLinkRef && ComLinkRef->HasNewArmState(1)) {
        const ArmStateMsg R = ComLinkRef->ReadArmState(1);

        const FVector EEPosBase = CoordConvert::ProtocolToUnreal(R.position[0], R.position[1], R.position[2]);
        const FQuat EERotBase = CoordConvert::ProtocolToUnrealQuat(R.quaternion[0], R.quaternion[1], R.quaternion[2], R.quaternion[3]);

        GhostMeshComp->SetWorldLocationAndRotation(EEPosBase, EERotBase);
        UpdateFingerPose();
        return;
    }

    if (!RightHandRef) return;

    const FTransform& CaptureTM = SceneCapture->GetComponentTransform();
    const FTransform& HandTM    = RightHandRef->GetComponentTransform();

    FVector GhostWorldPos;
    FQuat   GhostWorldQuat;

    if (CameraRef)
    {
        const FVector HandRelToCamera =
            CameraRef->GetComponentTransform().InverseTransformPosition(HandTM.GetLocation());
        GhostWorldPos = CaptureTM.TransformPosition(HandRelToCamera);

        const FQuat HandRelRot =
            CameraRef->GetComponentTransform().GetRotation().Inverse() * HandTM.GetRotation();
        GhostWorldQuat = CaptureTM.GetRotation() * HandRelRot * FQuat(EEFrameOffset);
    }
    else
    {
        GhostWorldPos  = HandTM.GetLocation();
        GhostWorldQuat = HandTM.GetRotation() * FQuat(EEFrameOffset);
    }

    GhostMeshComp->SetWorldLocationAndRotation(GhostWorldPos, GhostWorldQuat);
    UpdateFingerPose();
}

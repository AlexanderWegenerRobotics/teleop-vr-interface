#include "Video/GhostOverlayComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent.h"   // AddShowOnlyComponent (base class)
#include "Components/StereoLayerComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"    // FindFProperty / FByteProperty

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

UGhostOverlayComponent::UGhostOverlayComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

// ---------------------------------------------------------------------------
// BeginPlay
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: BeginPlay on '%s'"),
        GetOwner() ? *GetOwner()->GetName() : TEXT("null"));

    // CameraRef may already be set by SetCamera() called from the owner's constructor.
    // Fall back to FindComponentByClass only if not explicitly wired.
    if (!CameraRef)
    {
        if (AActor* Owner = GetOwner())
            CameraRef = Owner->FindComponentByClass<UCameraComponent>();
    }

    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: CameraRef %s"),
        CameraRef ? TEXT("found") : TEXT("NOT FOUND — stereo layer will attach to root"));

    LoadAssets();
    CreateRenderTarget();
    CreateSceneCapture();
    CreateStereoLayer();

    bPipelineReady = CaptureRT && SceneCapture && GhostMeshComp && StereoLayer;
    // GhostMeshComp is a child of SceneCapture on the same actor (mirrors BP_GhostOverlay).

    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: BeginPlay complete — pipeline %s"),
        bPipelineReady ? TEXT("READY") : TEXT("NOT READY"));
}

// ---------------------------------------------------------------------------
// EndPlay
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// TickComponent
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bPipelineReady) return;

    UpdateGhostPose();
}

// ---------------------------------------------------------------------------
// LoadAssets
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::LoadAssets()
{
    // Each slot is only loaded if not already assigned (EditAnywhere override wins).

    if (!PostProcessMaterial)
    {
        PostProcessMaterial = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/Materials/M_PP_GhostAlpha.M_PP_GhostAlpha"));
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: M_PP_GhostAlpha %s"),
            PostProcessMaterial ? TEXT("loaded") : TEXT("FAILED — capture alpha will be wrong"));
    }

    if (!GhostHandMesh)
    {
        GhostHandMesh = LoadObject<UStaticMesh>(nullptr,
            TEXT("/Game/Visuals/Ghost/ghost_hand.ghost_hand"));
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: ghost_hand mesh %s"),
            GhostHandMesh ? TEXT("loaded") : TEXT("FAILED"));
    }

    if (!GhostHandMaterial)
    {
        GhostHandMaterial = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/Materials/M_Ghost.M_Ghost"));
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: M_Ghost %s"),
            GhostHandMaterial ? TEXT("loaded") : TEXT("FAILED"));
    }
}

// ---------------------------------------------------------------------------
// CreateRenderTarget
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::CreateRenderTarget()
{
    CaptureRT = NewObject<UTextureRenderTarget2D>(GetOwner(), TEXT("GhostCaptureRT"));
    CaptureRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    CaptureRT->ClearColor         = FLinearColor(0.f, 0.f, 0.f, 0.f);
    CaptureRT->bAutoGenerateMips  = false;
    CaptureRT->InitAutoFormat(RenderTargetSize.X, RenderTargetSize.Y);
    CaptureRT->UpdateResourceImmediate(true);

    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: RT created (%dx%d, RGBA8)"),
        RenderTargetSize.X, RenderTargetSize.Y);
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
    // Phase 2: replace with head joint state from ComLink.
    SceneCapture->SetWorldLocation(CapturePivotPosition);
    SceneCapture->SetWorldRotation(FRotator::ZeroRotator);

    SceneCapture->TextureTarget              = CaptureRT;
    // "Final Color (with tone curve) in Linear sRGB" in the BP dropdown = SCS_FinalColorLDR
    SceneCapture->CaptureSource              = ESceneCaptureSource::SCS_FinalColorLDR;
    SceneCapture->PrimitiveRenderMode        = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    SceneCapture->FOVAngle                   = CaptureFOV;
    SceneCapture->bCaptureEveryFrame         = true;
    SceneCapture->bCaptureOnMovement         = false;
    SceneCapture->bAlwaysPersistRenderingState = true;
    SceneCapture->CompositeMode              = SCCM_Overwrite;

    // PRM_UseShowOnlyList only filters mesh primitives — sky atmosphere, fog,
    // and sky lighting are rendered at a separate pass and ignore the ShowOnly
    // list.  Disable them explicitly so the RT background is black/transparent
    // instead of showing the scene sky.
    SceneCapture->ShowFlags.SetAtmosphere(false);
    SceneCapture->ShowFlags.SetFog(false);
    SceneCapture->ShowFlags.SetSkyLighting(false);
    SceneCapture->ShowFlags.SetDynamicShadows(false);

    // Post-process material that writes the correct alpha into CaptureRT.
    // bOverride_WeightedBlendables does not exist in UE 5.4 — add directly.
    if (PostProcessMaterial)
    {
        SceneCapture->PostProcessSettings.WeightedBlendables.Array.Emplace(
            1.0f, PostProcessMaterial);
    }
    else {
        UE_LOG(LogTemp, Warning, TEXT("Post Process Material not found"));
    }

    // --- Ghost mesh ---
    // Mirror the Blueprint: GhostMesh is a child of SceneCaptureComponent2D
    // on the same actor.  Use ShowOnlyComponents (component-level list) exactly
    // as the BP does with the "Show Only Component" node at BeginPlay.
    {
        GhostMeshComp = NewObject<UStaticMeshComponent>(Owner, TEXT("GhostMeshComp"));
        GhostMeshComp->SetupAttachment(SceneCapture);
        GhostMeshComp->RegisterComponent();

        // Phase 1: 50 cm forward along the capture's local X so it sits in frame.
        // Phase 2: detach and drive world pose from ComLink EE state each tick.
        GhostMeshComp->SetRelativeLocation(FVector(50.f, 0.f, 0.f));
        GhostMeshComp->SetRelativeRotation(EEFrameOffset);

        if (GhostHandMesh) GhostMeshComp->SetStaticMesh(GhostHandMesh);
        if (GhostHandMaterial)
        {
            for (int32 i = 0; i < GhostMeshComp->GetNumMaterials(); ++i)
                GhostMeshComp->SetMaterial(i, GhostHandMaterial);
        }

        GhostMeshComp->SetCastShadow(false);
        GhostMeshComp->SetVisibleInSceneCaptureOnly(true);

        // Exact C++ equivalent of the Blueprint "Show Only Component" node.
        SceneCapture->ShowOnlyComponents.Add(GhostMeshComp);

        UE_LOG(LogTemp, Log,
            TEXT("GhostOverlay: ShowOnlyComponents count after AddShowOnlyComponent: %d  mesh registered: %s"),
            SceneCapture->ShowOnlyComponents.Num(),
            GhostMeshComp->IsRegistered() ? TEXT("YES") : TEXT("NO"));
    }

    UE_LOG(LogTemp, Log,
        TEXT("GhostOverlay: scene capture created — mesh '%s'  PP '%s'"),
        GhostHandMesh     ? *GhostHandMesh->GetName()     : TEXT("none"),
        PostProcessMaterial ? *PostProcessMaterial->GetName() : TEXT("none"));
}

// ---------------------------------------------------------------------------
// CreateStereoLayer
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::CreateStereoLayer()
{
    AActor* Owner = GetOwner();
    if (!Owner || !CaptureRT) return;

    // Attach to the VR camera so the quad stays at PlaneDistance in front of the HMD.
    USceneComponent* AttachTo = CameraRef
        ? Cast<USceneComponent>(CameraRef)
        : Owner->GetRootComponent();

    StereoLayer = NewObject<UStereoLayerComponent>(Owner, TEXT("GhostStereoLayer"));
    StereoLayer->SetupAttachment(AttachTo);
    StereoLayer->RegisterComponent();
    StereoLayer->SetRelativeLocation(FVector(PlaneDistance, 0.f, 0.f));
    StereoLayer->SetRelativeRotation(FRotator::ZeroRotator);

    StereoLayer->bLiveTexture    = true;
    StereoLayer->bNoAlphaChannel = false;   // respect RT alpha for compositing
    StereoLayer->bSupportsDepth  = false;
    StereoLayer->SetPriority(1);            // composites over video layer (priority 0)

    // Phase 1: mono capture — same RT for both eyes.
    // Phase 2: call SetLeftTexture(LeftRT) when two captures are added.
    StereoLayer->SetTexture(CaptureRT);

    // StereoLayerType is protected in UE 5.4 with no public C++ setter.
    // Use property reflection to set SLT_FaceLocked without subclassing.
    if (FByteProperty* TypeProp = FindFProperty<FByteProperty>(
            StereoLayer->GetClass(), TEXT("StereoLayerType")))
    {
        TypeProp->SetPropertyValue_InContainer(
            StereoLayer, static_cast<uint8>(SLT_FaceLocked));
        UE_LOG(LogTemp, Log, TEXT("GhostOverlay: StereoLayerType → FaceLocked (via reflection)"));
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GhostOverlay: StereoLayerType property not found — layer may be world-locked"));
    }

    UpdateLayerSize();

    UE_LOG(LogTemp, Log, TEXT("GhostOverlay: stereo layer created at %.0f cm, priority 1"),
        PlaneDistance);
}

// ---------------------------------------------------------------------------
// UpdateLayerSize
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::UpdateLayerSize()
{
    if (!StereoLayer) return;

    // Size the quad to cover FOVCoverage of the operator's 110° HFoV at PlaneDistance.
    const float HFOVRad   = FMath::DegreesToRadians(110.f * FOVCoverage);
    const float QuadWidth = 2.f * PlaneDistance * FMath::Tan(HFOVRad * 0.5f);
    const float Aspect    = RenderTargetSize.Y > 0
        ? static_cast<float>(RenderTargetSize.X) / static_cast<float>(RenderTargetSize.Y)
        : 16.f / 9.f;
    const float QuadHeight = QuadWidth / Aspect;

    StereoLayer->SetQuadSize(FVector2D(QuadWidth, QuadHeight));
}

// ---------------------------------------------------------------------------
// UpdateGhostPose
// ---------------------------------------------------------------------------

void UGhostOverlayComponent::UpdateGhostPose()
{
    // Phase 1: mesh is a child of SceneCapture — no pose update needed.
    //
    // Phase 2 (TODO): detach GhostMeshComp from SceneCapture, then each tick:
    //   if (ComLinkRef->HasNewArmState(1))
    //   {
    //       ArmStateMsg R = ComLinkRef->ReadArmState(1);
    //       FVector  EEPos  = CoordConvert::ProtocolToUnreal(R.position[0], R.position[1], R.position[2]);
    //       FQuat    EEQuat = FQuat(R.quaternion[1], -R.quaternion[2], R.quaternion[3], R.quaternion[0]);
    //       GhostMeshComp->SetWorldLocationAndRotation(EEPos, EEQuat * FQuat(EEFrameOffset));
    //   }
}

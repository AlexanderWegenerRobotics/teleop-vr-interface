#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/StereoLayerComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GhostOverlayComponent.generated.h"

class UCameraComponent;
class UMaterialInterface;
class UStaticMesh;

// ---------------------------------------------------------------------------
// UGhostOverlayComponent
//
// Mirrors the working BP_GhostOverlay actor in C++.
//
// Rendering pipeline (mirrors BP_GhostOverlay):
//   GhostMeshComp (child of SceneCapture, same actor, SetVisibleInSceneCaptureOnly)
//     → SceneCapture (FinalColorLDR + M_PP_GhostAlpha PP,
//                     PRM_UseShowOnlyList, ShowOnlyComponents=[GhostMeshComp])
//     → CaptureRT (RTF_RGBA8, alpha written by PP material)
//     → StereoLayer (priority 1, face-locked, attached to VR camera)
//
// Phase 1: ghost mesh fixed relative to capture; pose = CapturePivotPosition.
// Phase 2: drive GhostMeshComp world transform from ComLink EE state.
// Phase 3: two-capture stereo; UpdateLayerSize driven by robot-cam extrinsics.
// ---------------------------------------------------------------------------

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UGhostOverlayComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGhostOverlayComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

    /** Explicit camera wiring — call from owner constructor before BeginPlay.
     *  BeginPlay falls back to FindComponentByClass if this is not called. */
    void SetCamera(UCameraComponent* InCamera) { CameraRef = InCamera; }

    // -----------------------------------------------------------------------
    // Overlay geometry — match VideoFeedComponent values
    // -----------------------------------------------------------------------

    /** Distance of the stereo layer quad from the HMD (cm). */
    UPROPERTY(EditAnywhere, Category = "Ghost|Overlay")
    float PlaneDistance = 700.f;

    /** Fraction of 110° HFoV the quad covers. */
    UPROPERTY(EditAnywhere, Category = "Ghost|Overlay",
              meta = (ClampMin = "0.5", ClampMax = "1.0"))
    float FOVCoverage = 0.85f;

    // -----------------------------------------------------------------------
    // Scene capture
    // -----------------------------------------------------------------------

    /** Vertical FOV of the capture camera (degrees). From robot_config fovy. */
    UPROPERTY(EditAnywhere, Category = "Ghost|Capture")
    float CaptureFOV = 60.f;

    /** Render target resolution per eye. */
    UPROPERTY(EditAnywhere, Category = "Ghost|Capture")
    FIntPoint RenderTargetSize = FIntPoint(960, 540);

    /**
     * World-space position of the scene capture (cm).
     * Phase 1: fixed at robot-head height. Phase 2: driven from head joint state.
     * From robot_config: head base_pose position=[0,0,1.2] m → (0,0,120) cm.
     */
    UPROPERTY(EditAnywhere, Category = "Ghost|Capture")
    FVector CapturePivotPosition = FVector(0.f, 0.f, 120.f);

    // -----------------------------------------------------------------------
    // Ghost mesh
    // -----------------------------------------------------------------------

    /**
     * Rotation applied to the ghost mesh after spawning.
     * Tune empirically to align mesh axes with the Franka EE frame.
     */
    UPROPERTY(EditAnywhere, Category = "Ghost|Mesh")
    FRotator EEFrameOffset = FRotator::ZeroRotator;

    // -----------------------------------------------------------------------
    // Asset slots — auto-loaded from /Game/ paths if left null
    // -----------------------------------------------------------------------

    /** Post-process material on the scene capture that writes correct RT alpha.
     *  Path: /Game/Materials/M_PP_GhostAlpha */
    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UMaterialInterface> PostProcessMaterial;

    /** Static mesh used as the ghost hand.
     *  Path: /Game/Visuals/Ghost/ghost_hand */
    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UStaticMesh> GhostHandMesh;

    /** Material applied to GhostHandMesh.
     *  Path: /Game/Materials/M_Ghost */
    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UMaterialInterface> GhostHandMaterial;

private:
    // --- One-time setup ---
    void LoadAssets();
    void CreateRenderTarget();
    void CreateSceneCapture();
    void CreateStereoLayer();
    void UpdateLayerSize();

    // --- Per-tick ---
    void UpdateGhostPose();

    // -----------------------------------------------------------------------
    // Runtime objects (UPROPERTY keeps them alive / GC-visible)
    // -----------------------------------------------------------------------

    /** Render target written by SceneCapture and read by StereoLayer. */
    UPROPERTY()
    TObjectPtr<UTextureRenderTarget2D> CaptureRT;

    /** Scene capture pointing at the ghost mesh. */
    UPROPERTY()
    TObjectPtr<USceneCaptureComponent2D> SceneCapture;

    /** Ghost hand mesh component, child of SceneCapture (same actor). */
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> GhostMeshComp;

    /** Face-locked stereo layer (priority 1) attached to the VR camera. */
    UPROPERTY()
    TObjectPtr<UStereoLayerComponent> StereoLayer;

    /** Cached VR camera reference (not owned here). */
    UPROPERTY()
    TObjectPtr<UCameraComponent> CameraRef;

    bool bPipelineReady = false;
};

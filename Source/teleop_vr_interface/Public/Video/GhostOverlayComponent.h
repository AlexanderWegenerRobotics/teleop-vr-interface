#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/StereoLayerComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GhostOverlayComponent.generated.h"

class UCameraComponent;
class UComLink;
class UMaterialInterface;
class UStaticMesh;
class UTrackedControllerComponent;

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
// Phase 1: ghost mesh tracks the right VR controller (operator intent).
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

    void SetCamera(UCameraComponent* InCamera) { CameraRef = InCamera; }
    void SetRightHand(USceneComponent* InHand) { RightHandRef = InHand; }
    void SetComLink(UComLink* InComLink) { ComLinkRef = InComLink; }
    void SetRightTracked(UTrackedControllerComponent* InTracked) { RightTrackedRef = InTracked; }


    UPROPERTY(EditAnywhere, Category = "Ghost|Overlay")
    float PlaneDistance = 700.f;

    UPROPERTY(EditAnywhere, Category = "Ghost|Overlay",
              meta = (ClampMin = "0.5", ClampMax = "1.0"))
    float FOVCoverage = 0.85f;

    UPROPERTY(EditAnywhere, Category = "Ghost|Capture")
    float CaptureFOV = 60.f;
    UPROPERTY(EditAnywhere, Category = "Ghost|Capture")
    FIntPoint RenderTargetSize = FIntPoint(1280, 960);
    UPROPERTY(EditAnywhere, Category = "Ghost|Capture")
    FVector CapturePivotPosition = FVector(5.f, 0.f, 123.5f);
    UPROPERTY(EditAnywhere, Category = "Ghost|Mesh")
    FRotator EEFrameOffset = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, Category = "Ghost|Fingers")
    FVector RightFingerOpenOffset = FVector(0.f, 4.0f, 0.f);
    UPROPERTY(EditAnywhere, Category = "Ghost|Fingers")
    FVector RightFingerClosedOffset = FVector(0.f, 0.5f, 0.f);
    UPROPERTY(EditAnywhere, Category = "Ghost|Fingers")
    FVector LeftFingerOpenOffset = FVector(0.f, -4.0f, 0.f);
    UPROPERTY(EditAnywhere, Category = "Ghost|Fingers")
    FVector LeftFingerClosedOffset = FVector(0.f, -0.5f, 0.f);
    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UMaterialInterface> PostProcessMaterial;
    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UStaticMesh> GhostHandMesh;
    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UStaticMesh> GhostLeftFingerMesh;
    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UStaticMesh> GhostRightFingerMesh;
    UPROPERTY(EditAnywhere, Category = "Ghost|Assets")
    TObjectPtr<UMaterialInterface> GhostHandMaterial;

private:
    void LoadAssets();
    void CreateRenderTarget();
    void CreateSceneCapture();
    void CreateStereoLayer();
    void UpdateLayerSize();

    void UpdateGhostPose();
    void UpdateFingerPose();
    UPROPERTY()
    TObjectPtr<UTextureRenderTarget2D> CaptureRT;

    UPROPERTY()
    TObjectPtr<USceneCaptureComponent2D> SceneCapture;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> GhostMeshComp;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> LeftFingerMeshComp;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> RightFingerMeshComp;
    UPROPERTY()
    TObjectPtr<UStereoLayerComponent> StereoLayer;
    UPROPERTY()
    TObjectPtr<UCameraComponent> CameraRef;

    UPROPERTY()
    TObjectPtr<USceneComponent> RightHandRef;
    UPROPERTY()
    TObjectPtr<UComLink> ComLinkRef;
    UPROPERTY()
    TObjectPtr<UTrackedControllerComponent> RightTrackedRef;

    bool bPipelineReady = false;
};

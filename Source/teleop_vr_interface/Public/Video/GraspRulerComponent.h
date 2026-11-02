#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GraspRulerComponent.generated.h"

class UGhostOverlayComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UMaterialBillboardComponent;

// What the nudge engine's grasp_ruler sends for one arm, world/protocol frame (x forward, y left, z up), metres.
struct FGraspRulerState {
    bool  bActive = false;
    bool  bValid = false;
    bool  bHasAim = false;
    float Tcp[3] = { 0.f, 0.f, 0.f };
    float Aim[3] = { 0.f, 0.f, 0.f };
    float TolXY = 0.008f;
    float TolZ = 0.008f;
    TArray<FVector2f> BandsX;
    TArray<FVector2f> BandsY;
    FVector2f ExtentX = FVector2f::ZeroVector;
    FVector2f ExtentY = FVector2f::ZeroVector;
    FVector2f ExtentZ = FVector2f::ZeroVector;
    bool  Ok[3] = { false, false, false };
    bool  bNear = false;
    bool  bReady = false;
};

// Where the rulers lie on the table, from overlay*.json grasp_ruler.
struct FGraspRulerSettings {
    float TableZ = 0.70f;
    float XAtY = -0.36f;
    float XFrom = 0.40f;
    float XTo = 0.95f;
    float XOuter = -1.f;
    float YAtX = 0.40f;
    float YFrom = -0.36f;
    float YTo = 0.35f;
    float YOuter = -1.f;
    float TapeX = 0.43f;
    float TapeY = -0.33f;
    float TapeHeight = 0.15f;
    float BandWidth = 0.016f;
    float TapeBandWidth = 0.010f;
    float Lift = 0.0015f;
    float LoupeGain = 3.f;
    float LoupeHalfRange = 0.02f;
    float BaseAlpha = 0.45f;
    int32 UVBits = 0;
    float HoldS = 0.4f;
    float FadeS = 0.25f;
};

// World-locked table rulers drawn through the ghost's scene-capture rig: one along world x, one along world y,
// and a height tape at their corner. The rulers are always there (faint); while the engine sends a grasp target
// for this arm they show the valid grasp bands, the target bar, the gripper marker and, close to the target,
// a magnified window. One component per arm; only the first draws the ruler base.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UGraspRulerComponent : public UActorComponent {
    GENERATED_BODY()

public:
    UGraspRulerComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void Initialize(UGhostOverlayComponent* Ghost, const FGraspRulerSettings& Settings, bool bDrawBase);

    // Called from the receive thread; applied on the game thread in Tick.
    void SetState(const FGraspRulerState& State);

    UPROPERTY(EditAnywhere, Category = "GraspRuler")
    TObjectPtr<UMaterialInterface> RulerMaterial;

private:
    UStaticMeshComponent* MakeMesh(const TCHAR* Name, bool bVisible);
    bool PlaceRuler(UStaticMeshComponent* Mesh, bool bAlongX);
    bool PlaceTape();
    void ApplyRuler(UMaterialInstanceDynamic* MID, bool bAlongX);
    void ApplyTape();

    UPROPERTY() TObjectPtr<UStaticMeshComponent> XMesh_;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> YMesh_;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Probe_;
    UPROPERTY() TObjectPtr<UMaterialBillboardComponent> Tape_;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> XMID_;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> YMID_;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TapeMID_;
    UPROPERTY() TObjectPtr<UGhostOverlayComponent> GhostRef_;

    FGraspRulerSettings S_;
    bool bDrawBase_ = true;
    bool bInitialized_ = false;

    FCriticalSection Mtx_;
    FGraspRulerState Pending_;
    bool bPending_ = false;
    FGraspRulerState State_;
    double LastSeen_ = 0.0;
    float Level_ = 0.f;
    float LoupeLevel_ = 0.f;
    bool bWasActive_ = false;

    static constexpr float kBandFrac = 0.31f;
    static constexpr float kTapeBandFrac = 0.26f;
};

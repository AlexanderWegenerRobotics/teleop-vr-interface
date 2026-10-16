#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IntentHighlightComponent.generated.h"

class UGhostOverlayComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

// One object the local orchestrator wants marked, in protocol (world) metres.
struct FHighlightTarget {
    FString Name;
    float   Position[3] = { 0.f, 0.f, 0.f };
    float   Prob = 0.f;
    FString Arm;
    bool    bGoal = false;   // board slot rather than a part: drawn smaller and lifted to the slot walls
};

// Draws a disc under each highlighted object through the ghost's scene-capture rig,
// the same path the workspace boundary uses. Development aid for watching the intent
// module live; later the "grasp this next" recommendation renders through the same thing.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UIntentHighlightComponent : public UActorComponent {
    GENERATED_BODY()

public:
    UIntentHighlightComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void Initialize(UGhostOverlayComponent* Ghost);

    // Replaces the current set: a disc missing from the new set fades out at once; HoldS only
    // bridges gaps between messages (lost packets, engine paused).
    void SetTargets(const TArray<FHighlightTarget>& Targets);

    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    TObjectPtr<UMaterialInterface> DiscMaterial;
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    float RadiusCm = 8.f;
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    float GoalRadiusCm = 4.f;
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    float GoalLiftM = 0.035f;
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    float MinProb = 0.5f;
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    float HoldS = 0.5f;
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    float FadeS = 0.25f;
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    FLinearColor Tint = FLinearColor(1.f, 0.75f, 0.2f, 1.f);
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    FName TintParam = TEXT("Tint");
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    FName SevParam = TEXT("Sev");
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    FName RadiusParam = TEXT("Radius");
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    FName ContactParam = TEXT("Contact");
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    FName TanUParam = TEXT("TanU");
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    FName TanVParam = TEXT("TanV");
    UPROPERTY(EditAnywhere, Category = "IntentHighlight")
    FName SpacingParam = TEXT("Spacing");

private:
    static constexpr int32 kMaxDiscs = 4;

    struct FDisc {
        UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
        UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
        FString Name;
        float   Position[3] = { 0.f, 0.f, 0.f };
        float   Target = 0.f;
        float   Level = 0.f;
        double  LastSeen = 0.0;
        bool    bGoal = false;
    };
    FDisc Discs_[kMaxDiscs];
    FCriticalSection Mtx_;
    TArray<FHighlightTarget> Pending_;
    bool bPending_ = false;

    UPROPERTY()
    TObjectPtr<UGhostOverlayComponent> GhostRef_;
    bool bInitialized_ = false;
};

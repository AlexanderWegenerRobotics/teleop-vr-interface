#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MotionControllerComponent.h"
#include "InputAction.h"
#include "TrackedControllerComponent.generated.h"

UENUM(BlueprintType)
enum class EControllerTrackingState : uint8 {
    Tracking,
    Lost,
    Stale
};

USTRUCT(BlueprintType)
struct FControllerDeltaPose {
    GENERATED_BODY()

    FVector Translation = FVector::ZeroVector;
    FQuat Rotation = FQuat::Identity;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UTrackedControllerComponent : public UActorComponent {
    GENERATED_BODY()

public:
    UTrackedControllerComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void CaptureOrigin();
    FControllerDeltaPose GetDeltaPose() const;

    bool IsTracking() const;
    EControllerTrackingState GetTrackingState() const;

    bool IsGripHeld() const { return bGripHeld; }
    bool IsMenuPressed() const { return bMenuPressed; }
    float GetTriggerValue() const { return TriggerValue; }
    bool IsClutching() const { return bIsClutching; }
    void ConsumeMenuPress() { bMenuPressed = false; }

    UPROPERTY(EditAnywhere, Category = "Controller")
    UMotionControllerComponent* MotionController = nullptr;

    UPROPERTY(EditAnywhere, Category = "Controller|Input")
    UInputAction* IA_Trigger = nullptr;

    UPROPERTY(EditAnywhere, Category = "Controller|Input")
    UInputAction* IA_Grip = nullptr;

    UPROPERTY(EditAnywhere, Category = "Controller|Input")
    UInputAction* IA_Stop = nullptr;

    UPROPERTY(EditAnywhere, Category = "Controller|Debug")
    bool bDrawDebugRay = false;

    UPROPERTY(EditAnywhere, Category = "Controller|Debug")
    bool bPrintDebugInfo = false;

    UPROPERTY(EditAnywhere, Category = "Controller|Debug")
    float DebugRayLength = 50.0f;

    UPROPERTY(EditAnywhere, Category = "Controller|Calibration")
    int32 CalibrationSamples = 15;

    UPROPERTY(EditAnywhere, Category = "Controller|Tracking")
    float StaleThreshold = 0.5f;

private:
    void BindInputActions();

    void OnTrigger(const FInputActionValue& Value);
    void OnGripPressed(const FInputActionValue& Value);
    void OnGripReleased(const FInputActionValue& Value);
    void OnMenuPressed(const FInputActionValue& Value);
    void OnMenuReleased(const FInputActionValue& Value);

    void UpdateClutch();
    void UpdateTrackingState();
    void RecordSample();

    FTransform Origin;
    bool bOriginValid = false;

    TArray<FTransform> SampleBuffer;

    float TriggerValue = 0.0f;
    bool bGripHeld = false;
    bool bMenuPressed = false;
    bool bIsClutching = false;
    bool bWasClutching = false;

    FTransform LastTrackedTransform;
    double LastTrackingTimestamp = 0.0;
    EControllerTrackingState TrackingState = EControllerTrackingState::Lost;

    double LastLogTime = 0.0;
};
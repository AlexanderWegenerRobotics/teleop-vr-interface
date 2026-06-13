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

    bool IsGraspHeld() const { return bGripHeld; }
    bool IsMenuPressed() const { return bMenuPressed; }
    float GetClutchFactor() const;
    bool IsFullClutch() const { return bFullClutch; }
    bool IsClutching() const { return bFullClutch; }
    void ConsumeMenuPress() { bMenuPressed = false; }
    float GetScaleFactor() const { return ScaleFactor; }

    // Arm a one-shot wrist-pivot calibration. While armed, the NEXT grip hold captures
    // poses instead of toggling grasp; releasing grip solves for ControlPointOffset,
    // applies it, and logs a ready-to-paste value. Auto-disarms after a timeout.
    void ArmPivotCalibration();

    // Local-frame offset (cm) from the controller's tracked origin to the operator's
    // wrist pivot. Applied to translation integration so rotating in place no longer
    // produces parasitic translation ("the arc"). Found via ArmPivotCalibration, or set
    // by hand. Zero = use the raw tracked origin (legacy behaviour).
    UPROPERTY(EditAnywhere, Category = "Controller|Calibration")
    FVector ControlPointOffset = FVector::ZeroVector;

    // Minimum orientation advance between captured calibration samples (deg).
    UPROPERTY(EditAnywhere, Category = "Controller|Calibration")
    float CalibAngularStepDeg = 3.0f;

    // How long a calibration capture runs before auto-solving (s).
    UPROPERTY(EditAnywhere, Category = "Controller|Calibration")
    float CalibWindowSec = 12.0f;

    UPROPERTY(EditAnywhere, Category = "Controller")
    UMotionControllerComponent* MotionController = nullptr;

    UPROPERTY(EditAnywhere, Category = "Controller|Input")
    UInputAction* IA_Trigger = nullptr;

    UPROPERTY(EditAnywhere, Category = "Controller|Input")
    UInputAction* IA_Grip = nullptr;

    UPROPERTY(EditAnywhere, Category = "Controller|Input")
    UInputAction* IA_Stop = nullptr;

    UPROPERTY(EditAnywhere, Category = "Controller|Input")
    UInputAction* IA_PadUp = nullptr;

    UPROPERTY(EditAnywhere, Category = "Controller|Input")
    UInputAction* IA_PadDown = nullptr;

    UPROPERTY(EditAnywhere, Category = "Controller|Input")
    UInputAction* IA_HandGrip = nullptr;

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

    UPROPERTY(EditAnywhere, Category = "Controller|Clutch")
    float ClutchDeadZoneLow = 0.05f;

    UPROPERTY(EditAnywhere, Category = "Controller|Clutch")
    float ClutchEngageThreshold = 0.08f;

    UPROPERTY(EditAnywhere, Category = "Controller|Clutch")
    float ClutchDisengageThreshold = 0.04f;

    UPROPERTY(EditAnywhere, Category = "Controller|Clutch")
    float ClutchActiveRangeMax = 0.9f;

    UPROPERTY(EditAnywhere, Category = "Controller|Scale")
    float MinScale = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Controller|Scale")
    float MaxScale = 5.0f;

    UPROPERTY(EditAnywhere, Category = "Controller|Scale")
    float ScaleStep = 0.5f;
    UPROPERTY(EditAnywhere, Category = "Controller|Feedback")
    float ClutchEngageHapticIntensity = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Controller|Feedback")
    float ClutchEngageHapticDuration = 0.3f;

    UPROPERTY(EditAnywhere, Category = "Controller|Feedback")
    float ClutchDisengageHapticIntensity = 0.25f;

    UPROPERTY(EditAnywhere, Category = "Controller|Feedback")
    float ClutchDisengageHapticDuration = 0.15f;

    UPROPERTY(EditAnywhere, Category = "Controller|Feedback")
    USoundBase* GraspSound = nullptr;

private:
    void BindInputActions();

    void OnTrigger(const FInputActionValue& Value);
    void OnGripPressed(const FInputActionValue& Value);
    void OnGripReleased(const FInputActionValue& Value);
    void OnMenuPressed(const FInputActionValue& Value);
    void OnMenuReleased(const FInputActionValue& Value);
    void OnPadUp(const FInputActionValue& Value);
    void OnPadDown(const FInputActionValue& Value);
    void OnHandGripPressed(const FInputActionValue& Value);
    void OnHandGripReleased(const FInputActionValue& Value);

    void UpdateClutch();
    void UpdateScaledTranslation();
    void UpdateTrackingState();
    void RecordSample();

    FVector ControlPointLocation(const FTransform& T) const {
        return T.GetLocation() + T.TransformVectorNoScale(ControlPointOffset);
    }
    void SolvePivotCalibration();

    bool bCalibCapturing  = false;
    double CalibStartTime = 0.0;
    double LastCalibProgressTime = 0.0;
    TArray<FTransform> CalibSamples;
    FQuat LastCalibQuat = FQuat::Identity;

    float ComputeClutchScale(float TriggerRaw) const;
    void PlayClutchHaptic(float Intensity, float Duration);
    EControllerHand GetHand() const;
    bool bWasGraspHeld = false;

    FTransform Origin;
    bool bOriginValid = false;

    TArray<FTransform> SampleBuffer;

    float TriggerValue = 0.0f;
    bool bGripHeld = false;
    bool bHandGripHeld = false;
    bool bMenuPressed = false;
    bool bFullClutch = true;
    bool bWasFullClutch = false;

    float ScaleFactor = 2.0f;

    FVector ScaledTranslation = FVector::ZeroVector;
    FVector BankedScaledTranslation = FVector::ZeroVector;
    FVector PrevTrackedLocation = FVector::ZeroVector;
    bool bPrevLocationValid = false;

    FTransform LastTrackedTransform;
    double LastTrackingTimestamp = 0.0;
    EControllerTrackingState TrackingState = EControllerTrackingState::Lost;
    FQuat BankedRotation = FQuat::Identity;

    double LastLogTime = 0.0;

};
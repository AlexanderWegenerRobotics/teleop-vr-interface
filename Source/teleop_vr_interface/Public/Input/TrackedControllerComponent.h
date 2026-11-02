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

    // Filtered, jump-rejected controller pose as of the last game tick. Used by
    // the command thread only when it cannot sample OpenXR directly.
    FTransform GetTrackedTransform() const { return LastTrackedTransform; }

    bool IsGraspHeld() const { return bGripHeld; }
    bool IsMenuPressed() const { return bMenuPressed; }
    float GetClutchFactor() const;
    bool IsFullClutch() const { return bFullClutch; }
    bool IsClutching() const { return bFullClutch; }
    void ConsumeMenuPress() { bMenuPressed = false; }

    // Latched hand-grip tap, the handover shortcut. See OnHandGripReleased and
    // PollResumeButton.
    bool IsResumeRequested() const { return bResumeRequested; }
    void ConsumeResumePress() { bResumeRequested = false; }

    // Force the grasp toggle to a known value, used on a takeover (see
    // AOperatorPawn::RequestArmAuthority).
    void SetGraspHeld(bool bHeld) { bGripHeld = bHeld; }

    float GetScaleFactor() const { return ScaleFactor; }

    // One-shot wrist-pivot calibration: the next grip hold captures poses, the
    // release solves for ControlPointOffset and logs the value. Auto-disarms after a
    // timeout.
    void ArmPivotCalibration();

    // Offset (cm) from the controller's tracked origin to the wrist pivot, so
    // rotating in place does not produce translation. Zero = raw tracked origin.
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

    // Largest tracked control-point speed (cm/s, before ScaleFactor) accepted as
    // real operator motion. A frame implying more than this is a tracker
    // discontinuity, not a hand, and is dropped rather than integrated.
    UPROPERTY(EditAnywhere, Category = "Controller|Tracking")
    float MaxTrackedSpeed = 150.0f;

    // Upper bound on the frame interval used to size the guard, so a long hitch
    // cannot buy a proportionally large allowance.
    UPROPERTY(EditAnywhere, Category = "Controller|Tracking")
    float MaxGuardWindow = 0.1f;

    // One-euro filter on the tracked control point: fc = MinCutoff + Beta * speed
    // (cm/s), so jitter at rest is smoothed hard while real motion passes with little
    // lag. Mirrored into FOperatorInputSnapshot for the command thread, so tune here
    // only.
    // Beta 0.4 because the noise floor above 15 Hz is small and lag is the expensive
    // side; DerivCutoff 5 Hz so the speed estimate does not delay movement onset.
    // MinCutoff 0 disables it, e.g. when measuring the runtime's pose update rate.
    UPROPERTY(EditAnywhere, Category = "Controller|Tracking")
    float FilterMinCutoff = 1.5f;

    UPROPERTY(EditAnywhere, Category = "Controller|Tracking")
    float FilterBeta = 0.4f;

    UPROPERTY(EditAnywhere, Category = "Controller|Tracking")
    float FilterDerivCutoff = 5.0f;

    int32 GetRejectedTrackingJumps() const { return RejectedTrackingJumps; }
    int32 GetInertialOnlyFrames() const { return InertialOnlyFrames; }

    // Binary clutch with hysteresis: engage above the first threshold, release at or
    // below the second.
    UPROPERTY(EditAnywhere, Category = "Controller|Clutch")
    float ClutchEngageThreshold = 0.55f;

    UPROPERTY(EditAnywhere, Category = "Controller|Clutch")
    float ClutchDisengageThreshold = 0.35f;

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
    void UpdateScaledTranslation(float DeltaTime);
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

    void PlayClutchHaptic(float Intensity, float Duration);
    EControllerHand GetHand() const;
    FTimerHandle HapticStopTimer_;
public:
    // Haptic primitive for the nudge receiver: amplitude follows Intensity (the clutch cue drives frequency instead).
    void PlayHaptic(float Intensity, float Duration);
private:
    bool bWasGraspHeld = false;

    FTransform Origin;
    bool bOriginValid = false;

    TArray<FTransform> SampleBuffer;

    float TriggerValue = 0.0f;
    bool bGripHeld = false;
    bool bHandGripHeld = false;
    // True once the pad was used during the current hand-grip hold, which
    // makes that hold a scale gesture rather than a handover tap.
    bool bPadUsedThisHold = false;
    bool bResumeRequested = false;
    bool bMenuPressed = false;
    bool bFullClutch = true;
    bool bWasFullClutch = false;

    float ScaleFactor = 1.5f;

    FVector ScaledTranslation = FVector::ZeroVector;
    FVector BankedScaledTranslation = FVector::ZeroVector;
    FVector PrevTrackedLocation = FVector::ZeroVector;
    bool bPrevLocationValid = false;
    int32 RejectedTrackingJumps = 0;
    double LastJumpLogTime = 0.0;
    int32 InertialOnlyFrames = 0;
    double LastInertialLogTime = 0.0;

    FVector FilterOneEuro(const FVector& Raw, float DeltaTime);
    void ResetOneEuro();
    FVector FilteredLocation = FVector::ZeroVector;
    FVector FilteredDerivative = FVector::ZeroVector;
    bool bFilterPrimed = false;

    FTransform LastTrackedTransform;
    double LastTrackingTimestamp = 0.0;
    EControllerTrackingState TrackingState = EControllerTrackingState::Lost;
    FQuat BankedRotation = FQuat::Identity;

    double LastLogTime = 0.0;

};
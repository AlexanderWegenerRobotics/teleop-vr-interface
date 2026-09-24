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

    // Latched HAND GRIP TAP -- pressed and released without touching the pad.
    // This is the handover shortcut; see OnHandGripReleased for why a tap and
    // not a press, and PollResumeButton for what it does.
    bool IsResumeRequested() const { return bResumeRequested; }
    void ConsumeResumePress() { bResumeRequested = false; }

    // Force the grasp toggle to a known value. Used on a takeover to re-sync
    // this latch with the gripper the operator is actually inheriting -- see
    // AOperatorPawn::RequestArmAuthority.
    void SetGraspHeld(bool bHeld) { bGripHeld = bHeld; }

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

    // Largest tracked control-point speed (cm/s, before ScaleFactor) accepted as
    // real operator motion. A frame implying more than this is a tracker
    // discontinuity, not a hand, and is dropped rather than integrated.
    UPROPERTY(EditAnywhere, Category = "Controller|Tracking")
    float MaxTrackedSpeed = 150.0f;

    // Upper bound on the frame interval used to size the guard, so a long hitch
    // cannot buy a proportionally large allowance.
    UPROPERTY(EditAnywhere, Category = "Controller|Tracking")
    float MaxGuardWindow = 0.1f;

    // One-euro filter on the tracked control point. The cutoff rises with speed,
    // so standing jitter is smoothed hard while real motion passes with little
    // added lag. MinCutoff sets the smoothing at rest, Beta how fast the cutoff
    // opens up. 0 disables the filter.
    //
    // These values are read by the command thread as well (mirrored into
    // FOperatorInputSnapshot each tick), so this is the one place to tune them.
    //
    // History: off until 2026-09-19. At the ~26 Hz game-thread rate the
    // tracker's jitter was already ALIASED into the signal band and no causal
    // filter could separate it from real motion; the best non-absurd setting
    // (0.8 Hz) removed 35% of the >4 Hz jitter on x and 3% on y. Sampling now
    // runs at 90 Hz on its own thread, so the noise is no longer aliased and
    // the filter can do its job.
    //
    // Correcting a claim the old note made: raising the sample rate does NOT
    // reduce the filter's lag. A first-order low-pass has group delay
    // tau = 1/(2*pi*fc) whatever the sample rate -- 1.5 Hz is 106 ms. What the
    // rate buys is that the noise is real rather than aliased. The lag is
    // managed by Beta instead, which opens the cutoff with hand speed
    // (cm/s here): fc = MinCutoff + Beta * speed.
    //
    //     speed      fc (1.5 + 0.4*v)    tau
    //     at rest        1.5 Hz         106 ms   (nothing is moving)
    //      10 cm/s       5.5 Hz          29 ms
    //      30 cm/s      13.5 Hz          12 ms
    //     100 cm/s      41.5 Hz           4 ms
    //
    // Beta 0.4 rather than the paper's small values because the measured noise
    // floor above 15 Hz is only 0.06-0.18 mm rms per axis (session 2026-09-19),
    // so there is little to smooth and lag is the expensive side of the trade.
    // DerivCutoff 5 Hz rather than 1 Hz for the same reason: at 1 Hz the speed
    // estimate itself lags 160 ms, so the cutoff stays shut through the first
    // part of every movement and smears its onset.
    //
    // Set MinCutoff to 0 to disable. Do that when measuring the runtime's true
    // pose update rate -- the filter erases exactly the kinks that measurement
    // looks for.
    UPROPERTY(EditAnywhere, Category = "Controller|Tracking")
    float FilterMinCutoff = 1.5f;

    UPROPERTY(EditAnywhere, Category = "Controller|Tracking")
    float FilterBeta = 0.4f;

    UPROPERTY(EditAnywhere, Category = "Controller|Tracking")
    float FilterDerivCutoff = 5.0f;

    int32 GetRejectedTrackingJumps() const { return RejectedTrackingJumps; }
    int32 GetInertialOnlyFrames() const { return InertialOnlyFrames; }

    // Binary clutch with hysteresis. Engage above the first, release at or
    // below the second. Raised from 0.08/0.04: those were only safe because
    // the old quadratic gain made a light touch worth ~0.1% of full scale.
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
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TeleOpTypes.h"
#include "StateComponent.generated.h"

class UPoseMapper;
class UComLink;
class UHUDComponent;

/**
 * Teleop system states
 */
UENUM(BlueprintType)
enum class ETeleOpState : uint8
{
	Offline,		// No connection
	Idle,			// Connected, video active, not controlling robot
	Anchoring,		// Operator positioning hand, waiting for grip confirm
	Engaged,		// Actively sending delta poses, robot follows
	Paused,			// Robot holds position, operator can reposition (clutch)
	Stopped			// Emergency stop, requires explicit reset
};

UCLASS(ClassGroup = (TeleOp), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStateComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- State transitions (called by UI buttons) ---

	/** UI Engage button: Idle -> Anchoring */
	void RequestEngage();

	/** UI Stop button: Any -> Stopped */
	void RequestStop();

	/** UI Reset: Stopped -> Idle */
	void RequestReset();

	// --- Grip input (called from PoseMapper via pawn) ---

	/** Grip pressed — drives Anchoring/Engaged/Paused cycle */
	void OnGripPressed();

	/** Grip released */
	void OnGripReleased();

	// --- State queries ---

	UFUNCTION(BlueprintCallable, Category = "TeleOp|State")
	ETeleOpState GetCurrentState() const { return CurrentState; }

	UFUNCTION(BlueprintCallable, Category = "TeleOp|State")
	FText GetStateText() const;

	/** Should PoseMapper send poses this frame? */
	bool ShouldSendPoses() const { return CurrentState == ETeleOpState::Engaged; }

	/** Is the system in a state where the robot should hold position? */
	bool IsRobotHolding() const { return CurrentState == ETeleOpState::Paused || CurrentState == ETeleOpState::Stopped; }

	// --- Delta pose access ---

	/** Get the delta pose for right hand (relative to anchor) */
	void GetRightHandDelta(FVector& OutDeltaPosition, FQuat& OutDeltaRotation) const;

	/** Get the delta pose for left hand (relative to anchor) */
	void GetLeftHandDelta(FVector& OutDeltaPosition, FQuat& OutDeltaRotation) const;

	// --- Configuration ---

	/** Workspace scaling factor. 1.0 = 1:1, 2.0 = operator moves half as much */
	UPROPERTY(EditAnywhere, Category = "TeleOp|State", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float WorkspaceScale = 1.0f;

	// Delegate broadcast when state changes
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStateChanged, ETeleOpState, OldState, ETeleOpState, NewState);

	UPROPERTY(BlueprintAssignable, Category = "TeleOp|State")
	FOnStateChanged OnStateChanged;

private:

	void TransitionTo(ETeleOpState NewState);
	void CaptureAnchorPose();

	UPROPERTY()
	ETeleOpState CurrentState = ETeleOpState::Idle;

	// Anchor (reference) poses — captured when entering Engaged from Anchoring
	FVector RightHandAnchorPosition = FVector::ZeroVector;
	FQuat RightHandAnchorRotation = FQuat::Identity;

	FVector LeftHandAnchorPosition = FVector::ZeroVector;
	FQuat LeftHandAnchorRotation = FQuat::Identity;

	// References to sibling components (resolved in BeginPlay)
	UPROPERTY()
	TObjectPtr<UPoseMapper> PoseMapperRef;

	UPROPERTY()
	TObjectPtr<UComLink> ComLinkRef;
};
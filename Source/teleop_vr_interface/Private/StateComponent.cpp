#include "StateComponent.h"
#include "OperatorPawn.h"
#include "PoseMapper.h"
#include "ComLink.h"

UStateComponent::UStateComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UStateComponent::BeginPlay()
{
	Super::BeginPlay();

	AOperatorPawn* Pawn = Cast<AOperatorPawn>(GetOwner());
	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("StateComponent: Owner is not an OperatorPawn."));
		SetComponentTickEnabled(false);
		return;
	}

	PoseMapperRef = Pawn->PoseMapper;
	ComLinkRef = Pawn->ComLink;

	UE_LOG(LogTemp, Log, TEXT("StateComponent: Initialized in state %s"), *GetStateText().ToString());
}

void UStateComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (CurrentState != ETeleOpState::Engaged) return;
	if (!PoseMapperRef || !ComLinkRef) return;

	// Compute and send delta poses
	FVector DeltaPos;
	FQuat DeltaRot;

	// Right hand
	GetRightHandDelta(DeltaPos, DeltaRot);

	FTrackedPose RightDelta;
	RightDelta.Position = DeltaPos;
	RightDelta.Orientation = DeltaRot.Rotator();
	RightDelta.bIsValid = PoseMapperRef->GetRightHandPose().bIsValid;
	RightDelta.Timestamp = FPlatformTime::Seconds();

	if (RightDelta.bIsValid)
	{
		ComLinkRef->SendHandPose(RightDelta, PoseMapperRef->IsRightTriggerHeld() ? 1.0f : 0.0f, false);
	}

	// Left hand
	GetLeftHandDelta(DeltaPos, DeltaRot);

	FTrackedPose LeftDelta;
	LeftDelta.Position = DeltaPos;
	LeftDelta.Orientation = DeltaRot.Rotator();
	LeftDelta.bIsValid = PoseMapperRef->GetLeftHandPose().bIsValid;
	LeftDelta.Timestamp = FPlatformTime::Seconds();

	if (LeftDelta.bIsValid)
	{
		ComLinkRef->SendHandPose(LeftDelta, PoseMapperRef->IsLeftTriggerHeld() ? 1.0f : 0.0f, true);
	}

	// Head pose always sends absolute (for pan-tilt camera tracking)
	FTrackedPose HeadPose = PoseMapperRef->GetHeadPose();
	if (HeadPose.bIsValid)
	{
		ComLinkRef->SendHeadPose(HeadPose);
	}
}

// ============================================================================
// State transitions from UI
// ============================================================================

void UStateComponent::RequestEngage()
{
	if (CurrentState == ETeleOpState::Idle || CurrentState == ETeleOpState::Stopped){
		TransitionTo(ETeleOpState::Anchoring);
	}
	else{
		UE_LOG(LogTemp, Warning, TEXT("StateComponent: Cannot engage from state %s"), *GetStateText().ToString());
	}
}

void UStateComponent::RequestStop()
{
	if (CurrentState != ETeleOpState::Offline && CurrentState != ETeleOpState::Stopped)
	{
		TransitionTo(ETeleOpState::Stopped);

		// Send stop command to robot
		if (ComLinkRef)
		{
			ComLinkRef->SendModeCommand(EOpMode::Stopped);
		}
	}
}

void UStateComponent::RequestReset()
{
	if (CurrentState == ETeleOpState::Stopped)
	{
		TransitionTo(ETeleOpState::Idle);

		if (ComLinkRef)
		{
			ComLinkRef->SendModeCommand(EOpMode::Idle);
		}
	}
}

// ============================================================================
// Grip input - drives the engagement cycle
// ============================================================================

void UStateComponent::OnGripPressed()
{
	switch (CurrentState)
	{
	case ETeleOpState::Anchoring:
		// Grip confirms the anchor pose ? Engaged
		CaptureAnchorPose();
		TransitionTo(ETeleOpState::Engaged);

		if (ComLinkRef)
		{
			ComLinkRef->SendModeCommand(EOpMode::Engaged);
		}
		break;

	case ETeleOpState::Engaged:
		// Grip pauses -> Paused (clutch)
		TransitionTo(ETeleOpState::Paused);

		if (ComLinkRef)
		{
			ComLinkRef->SendModeCommand(EOpMode::Paused);
		}
		break;

	case ETeleOpState::Paused:
		// Grip re-anchors -> Anchoring
		TransitionTo(ETeleOpState::Anchoring);
		break;

	default:
		// Grip does nothing in other states
		break;
	}
}

void UStateComponent::OnGripReleased()
{
	// Currently no release logic needed
	// Could be used for grip-to-hold engagement later
}

// ============================================================================
// Delta pose computation
// ============================================================================

void UStateComponent::GetRightHandDelta(FVector& OutDeltaPosition, FQuat& OutDeltaRotation) const
{
	if (!PoseMapperRef || !PoseMapperRef->GetRightHandPose().bIsValid)
	{
		OutDeltaPosition = FVector::ZeroVector;
		OutDeltaRotation = FQuat::Identity;
		return;
	}

	const FTrackedPose& Current = PoseMapperRef->GetRightHandPose();

	OutDeltaPosition = (Current.Position - RightHandAnchorPosition) * WorkspaceScale;
	OutDeltaRotation = Current.Orientation.Quaternion() * RightHandAnchorRotation.Inverse();
}

void UStateComponent::GetLeftHandDelta(FVector& OutDeltaPosition, FQuat& OutDeltaRotation) const
{
	if (!PoseMapperRef || !PoseMapperRef->GetLeftHandPose().bIsValid)
	{
		OutDeltaPosition = FVector::ZeroVector;
		OutDeltaRotation = FQuat::Identity;
		return;
	}

	const FTrackedPose& Current = PoseMapperRef->GetLeftHandPose();

	OutDeltaPosition = (Current.Position - LeftHandAnchorPosition) * WorkspaceScale;
	OutDeltaRotation = Current.Orientation.Quaternion() * LeftHandAnchorRotation.Inverse();
}

// ============================================================================
// Internal
// ============================================================================

void UStateComponent::CaptureAnchorPose()
{
	if (!PoseMapperRef) return;

	const FTrackedPose& Right = PoseMapperRef->GetRightHandPose();
	if (Right.bIsValid)
	{
		RightHandAnchorPosition = Right.Position;
		RightHandAnchorRotation = Right.Orientation.Quaternion();
		UE_LOG(LogTemp, Log, TEXT("StateComponent: Right hand anchor captured at (%.1f, %.1f, %.1f)"),
			RightHandAnchorPosition.X, RightHandAnchorPosition.Y, RightHandAnchorPosition.Z);
	}

	const FTrackedPose& Left = PoseMapperRef->GetLeftHandPose();
	if (Left.bIsValid)
	{
		LeftHandAnchorPosition = Left.Position;
		LeftHandAnchorRotation = Left.Orientation.Quaternion();
		UE_LOG(LogTemp, Log, TEXT("StateComponent: Left hand anchor captured at (%.1f, %.1f, %.1f)"),
			LeftHandAnchorPosition.X, LeftHandAnchorPosition.Y, LeftHandAnchorPosition.Z);
	}
}

void UStateComponent::TransitionTo(ETeleOpState NewState)
{
	ETeleOpState OldState = CurrentState;
	CurrentState = NewState;

	UE_LOG(LogTemp, Log, TEXT("StateComponent: %s -> %s"),
		*UEnum::GetValueAsString(OldState),
		*UEnum::GetValueAsString(NewState));

	OnStateChanged.Broadcast(OldState, NewState);
}

FText UStateComponent::GetStateText() const
{
	switch (CurrentState)
	{
	case ETeleOpState::Offline:		return FText::FromString(TEXT("OFFLINE"));
	case ETeleOpState::Idle:		return FText::FromString(TEXT("IDLE"));
	case ETeleOpState::Anchoring:	return FText::FromString(TEXT("ANCHORING"));
	case ETeleOpState::Engaged:		return FText::FromString(TEXT("ENGAGED"));
	case ETeleOpState::Paused:		return FText::FromString(TEXT("PAUSED"));
	case ETeleOpState::Stopped:		return FText::FromString(TEXT("STOPPED"));
	default:						return FText::FromString(TEXT("UNKNOWN"));
	}
}
#include "PoseMapper.h"
#include "OperatorPawn.h"
#include "MotionControllerComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/PlayerController.h"
#include "IXRTrackingSystem.h"
#include "IHeadMountedDisplay.h"
#include "ComLink.h"

UPoseMapper::UPoseMapper() {
	PrimaryComponentTick.bCanEverTick = true;

	// Tick after the default group so controller positions are up to date
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UPoseMapper::BeginPlay(){
	Super::BeginPlay();
	
	// Resolve owner
	OwnerPawn = Cast<AOperatorPawn>(GetOwner());
	if (!OwnerPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("PoseMapper: Owner is not an OperatorPawn. Component will not function."));
		SetComponentTickEnabled(false);
		return;
	}

	SetupInput();

	UE_LOG(LogTemp, Log, TEXT("PoseMapper: Initialized on %s"), *OwnerPawn->GetName());
}

void UPoseMapper::SetupInput(){
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn) return;

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC) return;

	// Register mapping context if provided
	if (PoseMappingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(PoseMappingContext, 1);
		}
	}

	// Bind trigger actions if the pawn's input component is ready
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
	if (!EIC)
	{
		UE_LOG(LogTemp, Warning, TEXT("PoseMapper: No EnhancedInputComponent found. Trigger input will not work."));
		return;
	}

	if (LeftTriggerAction)
	{
		EIC->BindAction(LeftTriggerAction, ETriggerEvent::Started, this, &UPoseMapper::OnLeftTriggerStarted);
		EIC->BindAction(LeftTriggerAction, ETriggerEvent::Completed, this, &UPoseMapper::OnLeftTriggerCompleted);
		UE_LOG(LogTemp, Log, TEXT("PoseMapper: Left trigger action bound."));
	}

	if (RightTriggerAction)
	{
		EIC->BindAction(RightTriggerAction, ETriggerEvent::Started, this, &UPoseMapper::OnRightTriggerStarted);
		EIC->BindAction(RightTriggerAction, ETriggerEvent::Completed, this, &UPoseMapper::OnRightTriggerCompleted);
		UE_LOG(LogTemp, Log, TEXT("PoseMapper: Right trigger action bound."));
	}

	if (RightGripAction)
	{
		EIC->BindAction(RightGripAction, ETriggerEvent::Started, this, &UPoseMapper::OnRightGripStarted);
		EIC->BindAction(RightGripAction, ETriggerEvent::Completed, this, &UPoseMapper::OnRightGripCompleted);
		UE_LOG(LogTemp, Log, TEXT("PoseMapper: Right grip action bound."));
	}

	if (LeftGripAction)
	{
		EIC->BindAction(LeftGripAction, ETriggerEvent::Started, this, &UPoseMapper::OnLeftGripStarted);
		EIC->BindAction(LeftGripAction, ETriggerEvent::Completed, this, &UPoseMapper::OnLeftGripCompleted);
		UE_LOG(LogTemp, Log, TEXT("PoseMapper: Left grip action bound."));
	}
}

void UPoseMapper::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction){
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdatePoses();

	// Throttled debug printing
	PrintTimer += DeltaTime;
	if (PrintTimer >= PrintInterval)
	{
		//PrintPoses();
		PrintTimer = 0.0f;
	}
}

bool UPoseMapper::IsControllerTracked(const UMotionControllerComponent* Controller) const{
	if (!Controller) return false;

	// A controller that isn't tracked will sit at origin with zero rotation.
	// Additionally check if the XR system reports it as tracked.
	return Controller->IsTracked();
}

void UPoseMapper::UpdatePoses(){
	const double Now = FPlatformTime::Seconds();

	// Head pose — always available if HMD is worn
	if (UCameraComponent* Camera = OwnerPawn->GetVRCamera())
	{
		HeadPose.Position = Camera->GetComponentLocation();
		HeadPose.Orientation = Camera->GetComponentRotation();
		HeadPose.bIsValid = true;
		HeadPose.Timestamp = Now;
	}

	// Left controller
	if (UMotionControllerComponent* Left = OwnerPawn->GetLeftController())
	{
		if (IsControllerTracked(Left))
		{
			LeftHandPose.Position = Left->GetComponentLocation();
			LeftHandPose.Orientation = Left->GetComponentRotation();
			LeftHandPose.bIsValid = true;
			LeftHandPose.Timestamp = Now;
		}
		else
		{
			LeftHandPose.bIsValid = false;
		}
	}

	// Right controller
	if (UMotionControllerComponent* Right = OwnerPawn->GetRightController())
	{
		if (IsControllerTracked(Right))
		{
			RightHandPose.Position = Right->GetComponentLocation();
			RightHandPose.Orientation = Right->GetComponentRotation();
			RightHandPose.bIsValid = true;
			RightHandPose.Timestamp = Now;
		}
		else
		{
			RightHandPose.bIsValid = false;
		}
	}
}

void UPoseMapper::PrintPoses(){
	// Head
	if (HeadPose.bIsValid)
	{
		UE_LOG(LogTemp, Log, TEXT("HEAD  | Pos: (%.1f, %.1f, %.1f) Rot: (P:%.1f, Y:%.1f, R:%.1f)"),
			HeadPose.Position.X, HeadPose.Position.Y, HeadPose.Position.Z,
			HeadPose.Orientation.Pitch, HeadPose.Orientation.Yaw, HeadPose.Orientation.Roll);
	}

	// Left
	if (LeftHandPose.bIsValid)
	{
		UE_LOG(LogTemp, Log, TEXT("LEFT  | Pos: (%.1f, %.1f, %.1f) Rot: (P:%.1f, Y:%.1f, R:%.1f) | Trigger: %s"),
			LeftHandPose.Position.X, LeftHandPose.Position.Y, LeftHandPose.Position.Z,
			LeftHandPose.Orientation.Pitch, LeftHandPose.Orientation.Yaw, LeftHandPose.Orientation.Roll,
			bLeftTriggerHeld ? TEXT("HELD") : TEXT("--"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("LEFT  | Not tracked"));
	}

	// Right
	if (RightHandPose.bIsValid)
	{
		UE_LOG(LogTemp, Log, TEXT("RIGHT | Pos: (%.1f, %.1f, %.1f) Rot: (P:%.1f, Y:%.1f, R:%.1f) | Trigger: %s"),
			RightHandPose.Position.X, RightHandPose.Position.Y, RightHandPose.Position.Z,
			RightHandPose.Orientation.Pitch, RightHandPose.Orientation.Yaw, RightHandPose.Orientation.Roll,
			bRightTriggerHeld ? TEXT("HELD") : TEXT("--"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("RIGHT | Not tracked"));
	}
}

// --- Trigger callbacks ---

void UPoseMapper::OnLeftTriggerStarted(const FInputActionValue& Value){
	bLeftTriggerHeld = true;
}

void UPoseMapper::OnLeftTriggerCompleted(const FInputActionValue& Value){
	bLeftTriggerHeld = false;
}

void UPoseMapper::OnRightTriggerStarted(const FInputActionValue& Value) {
	bRightTriggerHeld = true;
	if (OwnerPawn) OwnerPawn->OnRightTriggerPressed();
}

void UPoseMapper::OnRightTriggerCompleted(const FInputActionValue& Value){
	bRightTriggerHeld = false;
	if (OwnerPawn) OwnerPawn->OnRightTriggerReleased();
}

void UPoseMapper::OnRightGripStarted(const FInputActionValue& Value){
	if (OwnerPawn) OwnerPawn->OnGripPressed();
}

void UPoseMapper::OnRightGripCompleted(const FInputActionValue& Value){
	if (OwnerPawn) OwnerPawn->OnGripReleased();
}

void UPoseMapper::OnLeftGripStarted(const FInputActionValue& Value){
	// Left grip can mirror right grip or be used for something else later
}

void UPoseMapper::OnLeftGripCompleted(const FInputActionValue& Value){
}
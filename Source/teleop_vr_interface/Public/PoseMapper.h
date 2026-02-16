#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputActionValue.h"
#include "TeleOpTypes.h"
#include "PoseMapper.generated.h"

class AOperatorPawn;
class UMotionControllerComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
class UComLink;


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class TELEOP_VR_INTERFACE_API UPoseMapper : public UActorComponent
{
	GENERATED_BODY()

public:
	UPoseMapper();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Pose access ---
	const FTrackedPose& GetHeadPose() const { return HeadPose; }
	const FTrackedPose& GetLeftHandPose() const { return LeftHandPose; }
	const FTrackedPose& GetRightHandPose() const { return RightHandPose; }

	// --- Trigger state ---
	bool IsLeftTriggerHeld() const { return bLeftTriggerHeld; }
	bool IsRightTriggerHeld() const { return bRightTriggerHeld; }

public:

	// --- Input actions (assign in editor or create in code) ---
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> LeftTriggerAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> RightTriggerAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputMappingContext> PoseMappingContext;

	UPROPERTY()
	TObjectPtr<UComLink> ComLinkRef;

private:

	void SetupInput();
	void UpdatePoses();
	void PrintPoses();

	// Input callbacks
	void OnLeftTriggerStarted(const FInputActionValue& Value);
	void OnLeftTriggerCompleted(const FInputActionValue& Value);
	void OnRightTriggerStarted(const FInputActionValue& Value);
	void OnRightTriggerCompleted(const FInputActionValue& Value);

	// Checks if a motion controller is actively tracked
	bool IsControllerTracked(const UMotionControllerComponent* Controller) const;

	// Cached references (resolved in BeginPlay)
	UPROPERTY()
	TObjectPtr<AOperatorPawn> OwnerPawn;

	// Pose storage
	FTrackedPose HeadPose;
	FTrackedPose LeftHandPose;
	FTrackedPose RightHandPose;

	// Trigger state
	bool bLeftTriggerHeld = false;
	bool bRightTriggerHeld = false;

	// Debug print throttle
	float PrintTimer = 0.0f;
	float PrintInterval = 1.0f;		// print once per second to avoid log spam
};

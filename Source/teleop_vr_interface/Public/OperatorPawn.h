#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "MotionControllerComponent.h"

#include "PoseMapper.h"
#include "ComLink.h"
#include "VideoFeedComponent.h"
#include "HUDComponent.h"
#include "StateComponent.h"

#include "OperatorPawn.generated.h"

UCLASS()
class TELEOP_VR_INTERFACE_API AOperatorPawn : public APawn
{
	GENERATED_BODY()

public:
	AOperatorPawn();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	void OnRightTriggerPressed();
	void OnRightTriggerReleased();
	void OnGripPressed();
	void OnGripReleased();

	FORCEINLINE UCameraComponent* GetVRCamera() const { return VRCamera; }
	FORCEINLINE UMotionControllerComponent* GetLeftController() const { return LeftController; }
	FORCEINLINE UMotionControllerComponent* GetRightController() const { return RightController; }

	UFUNCTION(BlueprintCallable)
	void UIEngage();
	UFUNCTION(BlueprintCallable)
	void UIStop();

protected:
	// Scene root for VR origin
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	USceneComponent* VROrigin;

	// Head-tracked camera
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	UCameraComponent* VRCamera;

	// Hand controllers
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	UMotionControllerComponent* LeftController;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	UMotionControllerComponent* RightController;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TeleOp")
	TObjectPtr<UPoseMapper> PoseMapper;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TeleOp")
	TObjectPtr<UComLink> ComLink;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TeleOp")
	TObjectPtr<UVideoFeedComponent> VideoFeed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TeleOp")
	TObjectPtr<UHUDComponent> HUD;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TeleOp")
	TObjectPtr<UStateComponent> State;

};

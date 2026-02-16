#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "MotionControllerComponent.h"

#include "PoseMapper.h"
#include "ComLink.h"

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

	FORCEINLINE UCameraComponent* GetVRCamera() const { return VRCamera; }
	FORCEINLINE UMotionControllerComponent* GetLeftController() const { return LeftController; }
	FORCEINLINE UMotionControllerComponent* GetRightController() const { return RightController; }

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TeleOp")
	TObjectPtr<UPoseMapper> PoseMapper;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TeleOp")
	TObjectPtr<UComLink> ComLink;

};

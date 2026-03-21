#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "MotionControllerComponent.h"

#include "Video/VideoFeedComponent.h"
#include "Networking/ComLink.h"
#include "UI/GazeComponent.h"
#include "UI/WidgetBinder.h"
#include "UI/TMetricHistory.h"

#include "OperatorPawn.generated.h"

UCLASS()
class TELEOP_VR_INTERFACE_API AOperatorPawn : public APawn
{
	GENERATED_BODY()

public:
	AOperatorPawn();
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;
	UCameraComponent* GetVRCamera() const { return VRCamera; }

protected:
	UPROPERTY()TObjectPtr<USceneComponent> VROrigin;
	UPROPERTY() TObjectPtr<UCameraComponent> VRCamera;
	UPROPERTY()TObjectPtr<UMotionControllerComponent> LeftController;
	UPROPERTY() TObjectPtr<UMotionControllerComponent> RightController;
	UPROPERTY() TObjectPtr<UVideoFeedComponent> VideoFeed;
	UPROPERTY()TObjectPtr<UComLink> ComLink;
	UPROPERTY() TObjectPtr<UGazeComponent> Gaze;
	UPROPERTY() TObjectPtr<UWidgetBinder> UIBinder;
	UPROPERTY() TSubclassOf<UUserWidget> UIWidgetClass;

	TMetricHistory<128> LatencyHistory;
	TMetricHistory<128> JitterHistory;
	TMetricHistory<128> LossHistory;

private:
	void updateStateMachine();
};

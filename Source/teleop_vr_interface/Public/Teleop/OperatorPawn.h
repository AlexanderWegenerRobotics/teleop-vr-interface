#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "MotionControllerComponent.h"
#include "InputMappingContext.h"
#include "EnhancedInputSubsystems.h"
#include "Input/TrackedControllerComponent.h"
#include "Video/VideoFeedComponent.h"
#include "Networking/ComLink.h"
#include "UI/GazeComponent.h"
#include "UI/WidgetBinder.h"
#include "UI/TMetricHistory.h"
#include "UI/SoundFeedback.h"
#include "UI/TrayController.h"

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
	UPROPERTY() TObjectPtr<USceneComponent> VROrigin;
	UPROPERTY() TObjectPtr<UCameraComponent> VRCamera;
	UPROPERTY() TObjectPtr<UMotionControllerComponent> LeftController;
	UPROPERTY() TObjectPtr<UMotionControllerComponent> RightController;

	UPROPERTY() UTrackedControllerComponent* LeftTracked = nullptr;
	UPROPERTY() UTrackedControllerComponent* RightTracked = nullptr;
	UPROPERTY() UInputMappingContext* InputMappingContext = nullptr;
	UPROPERTY() UInputAction* IA_LeftTrigger = nullptr;
	UPROPERTY() UInputAction* IA_LeftGrip = nullptr;
	UPROPERTY() UInputAction* IA_LeftStop = nullptr;
	UPROPERTY() UInputAction* IA_RightTrigger = nullptr;
	UPROPERTY() UInputAction* IA_RightGrip = nullptr;
	UPROPERTY() UInputAction* IA_RightStop = nullptr;

	UPROPERTY() TObjectPtr<UVideoFeedComponent> VideoFeed;
	UPROPERTY() TObjectPtr<UComLink> ComLink;
	UPROPERTY() TObjectPtr<UGazeComponent> Gaze;
	UPROPERTY() TObjectPtr<UWidgetBinder> UIBinder;
	UPROPERTY() TSubclassOf<UUserWidget> UIWidgetClass;
	UPROPERTY() TObjectPtr<USoundFeedback> SoundFeedback;

	UPROPERTY() TObjectPtr<UWidgetBinder> TrayBinder;
	UPROPERTY() TSubclassOf<UUserWidget> TrayWidgetClass;

	UPROPERTY(EditAnywhere, Category = "Tray")
	FTrayController Tray;

	TMetricHistory<128> LatencyHistory;
	TMetricHistory<128> JitterHistory;
	TMetricHistory<128> LossHistory;

private:
	ESysState OperatorState_ = ESysState::Offline;

	void UpdateStateMachine();
	void TransitionTo(ESysState NewState);
	void UpdateButtonStates();
	void CaptureControllerOrigins();
	void SendArmCommands();
	void SendHeadCommand();
	bool CheckEmergencyStop();
	void UpdateTray(float DeltaTime, const FGazeData& GazeData);
	void HandleTrayPress(FName ButtonPressed);

	FTransform HMDOrigin_;
	bool bHMDOriginValid_ = false;
};
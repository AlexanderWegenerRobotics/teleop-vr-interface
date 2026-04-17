#include "Teleop/OperatorPawn.h"
#include "Video/GStreamerSource.h"
#include "Teleop/TeleOpConfig.h"

AOperatorPawn::AOperatorPawn() {
	PrimaryActorTick.bCanEverTick = true;

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	SetRootComponent(VROrigin);

	VRCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("VRCamera"));
	VRCamera->SetupAttachment(VROrigin);
	VRCamera->bLockToHmd = true;
	VRCamera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	VRCamera->PostProcessSettings.MotionBlurAmount = 0.0f;

	LeftController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftController"));
	LeftController->SetupAttachment(VROrigin);
	LeftController->SetTrackingSource(EControllerHand::Left);

	RightController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightController"));
	RightController->SetupAttachment(VROrigin);
	RightController->SetTrackingSource(EControllerHand::Right);

	AutoPossessAI = EAutoPossessAI::Disabled;
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	VideoFeed = CreateDefaultSubobject<UVideoFeedComponent>(TEXT("VideoFeed"));
	ComLink = CreateDefaultSubobject<UComLink>(TEXT("ComLink"));
	Gaze = CreateDefaultSubobject<UGazeComponent>(TEXT("Gaze"));
	SoundFeedback = CreateDefaultSubobject<USoundFeedback>(TEXT("SoundFeedback"));
	UIBinder = CreateDefaultSubobject<UWidgetBinder>(TEXT("UIBinder"));
	TrayBinder = CreateDefaultSubobject<UWidgetBinder>(TEXT("TrayBinder"));
	LeftTracked = CreateDefaultSubobject<UTrackedControllerComponent>(TEXT("LeftTracked"));
	RightTracked = CreateDefaultSubobject<UTrackedControllerComponent>(TEXT("RightTracked"));

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> IMC(TEXT("/Game/Input/IMC_PoseMapper.IMC_PoseMapper"));
	if (IMC.Succeeded()) InputMappingContext = IMC.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> LeftTrig(TEXT("/Game/Input/IA_LeftTrigger.IA_LeftTrigger"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LeftGrip(TEXT("/Game/Input/IA_LeftGrip.IA_LeftGrip"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LeftStop(TEXT("/Game/Input/IA_LeftStop.IA_LeftStop"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LeftPadUp(TEXT("/Game/Input/IA_LeftPadUp.IA_LeftPadUp"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LeftPadDown(TEXT("/Game/Input/IA_LeftPadDown.IA_LeftPadDown"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightTrig(TEXT("/Game/Input/IA_RightTrigger.IA_RightTrigger"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightGrip(TEXT("/Game/Input/IA_RightGrip.IA_RightGrip"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightStop(TEXT("/Game/Input/IA_RightStop.IA_RightStop"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightPadUp(TEXT("/Game/Input/IA_RightPadUp.IA_RightPadUp"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightPadDown(TEXT("/Game/Input/IA_RightPadDown.IA_RightPadDown"));

	LeftTracked->MotionController = LeftController;
	RightTracked->MotionController = RightController;

	LeftTracked->IA_Trigger = LeftTrig.Object;
	LeftTracked->IA_Grip = LeftGrip.Object;
	LeftTracked->IA_Stop = LeftStop.Object;
	LeftTracked->IA_PadUp = LeftPadUp.Object;
	LeftTracked->IA_PadDown = LeftPadDown.Object;
	RightTracked->IA_Trigger = RightTrig.Object;
	RightTracked->IA_Grip = RightGrip.Object;
	RightTracked->IA_Stop = RightStop.Object;
	RightTracked->IA_PadUp = RightPadUp.Object;
	RightTracked->IA_PadDown = RightPadDown.Object;

	static ConstructorHelpers::FClassFinder<UUserWidget> UIClass(TEXT("/Game/UI/WBP_DebugPanel.WBP_DebugPanel_C"));
	if (UIClass.Succeeded()) {
		UIWidgetClass = UIClass.Class;
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("OperatorPawn: WBP_DebugPanel not found"));
	}

	static ConstructorHelpers::FClassFinder<UUserWidget> TrayClass(TEXT("/Game/UI/WBP_TrayPanel.WBP_TrayPanel_C"));
	if (TrayClass.Succeeded()) {
		TrayWidgetClass = TrayClass.Class;
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("OperatorPawn: WBP_TrayPanel not found"));
	}
}

void AOperatorPawn::BeginPlay() {

	UTeleOpConfig* Config = NewObject<UTeleOpConfig>(this);
	if (!Config->Load(UTeleOpConfig::DefaultConfigPath())) {
		UE_LOG(LogTemp, Fatal, TEXT("OperatorPawn: Config load failed! check Config/TeleOp/ in project directory."));
		return;
	}

	ComLink->RemoteIP = Config->Network.RemoteIP;
	ComLink->AvatarSendPort = Config->Network.Avatar.Send;
	ComLink->AvatarReceivePort = Config->Network.Avatar.Receive;
	ComLink->ArmLeftSendPort = Config->Network.ArmLeft.Send;
	ComLink->ArmLeftReceivePort = Config->Network.ArmLeft.Receive;
	ComLink->ArmRightSendPort = Config->Network.ArmRight.Send;
	ComLink->ArmRightReceivePort = Config->Network.ArmRight.Receive;
	ComLink->HeadSendPort = Config->Network.Head.Send;
	ComLink->HeadReceivePort = Config->Network.Head.Receive;

	FReceiverConfig GstConfig;
	GstConfig.Port = Config->Stream.Port;
	GstConfig.FeedbackPort = Config->Stream.FeedbackPort;
	GstConfig.SenderIP = Config->Stream.RemoteIP;
	GstConfig.ReportIntervalMs = Config->Stream.ReportIntervalMs;
	GstConfig.StatusPort = Config->Stream.StatusPort;
	VideoFeed->RegisterSource(TEXT("AvatarStream"), MakeUnique<FGStreamerSource>(GstConfig));

	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(GetController())) {
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())) {
			Subsystem->AddMappingContext(InputMappingContext, 0);
		}
	}

	LeftTracked->bDrawDebugRay = true;
	RightTracked->bDrawDebugRay = true;

	UIBinder->Initialize(UIWidgetClass, VRCamera, FVector2D(1280.0f, 720.0f), 690.0f, 1);
	UIBinder->BindPlot(FName("latencyPlot"), LatencyHistory.GetSamplesPtr(), nullptr, LatencyHistory.Capacity(), LatencyHistory.GetHeadPtr(), 0.0f, 40.0f);
	UIBinder->BindPlot(FName("jitterPlot"), JitterHistory.GetSamplesPtr(), nullptr, JitterHistory.Capacity(), JitterHistory.GetHeadPtr(), 0.0f, 20.0f);

	if (TrayWidgetClass) {
		TrayBinder->Initialize(TrayWidgetClass, VRCamera, FVector2D(1280.0f, 720.0f), Tray.LayerDistance, 2, Tray.CollapsedWidth, Tray.ExpandedWidth, Tray.VerticalOffset);
		TrayBinder->SetExpansion(0.0f);
		TrayBinder->SetLayerOpacity(Tray.CollapsedOpacity);
	}

	UpdateButtonStates();
}

void AOperatorPawn::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);

	UpdateStateMachine();

	FVideoSourceStats Stats = VideoFeed->GetStreamStats();
	LatencyHistory.Push(Stats.OneWayLatencyMs);
	JitterHistory.Push(Stats.JitterMs);
	LossHistory.Push(Stats.PacketLossPercent);

	const FGazeData& GazeData = Gaze->GetGazeData();
	UIBinder->SetGazeInput(GazeData);
	UpdateTray(DeltaTime, GazeData);

	bool bVideoLive = VideoFeed->IsReceiving();
	UIBinder->SetText(FName("video_value"), bVideoLive ? TEXT("LIVE") : TEXT("OFFLINE"));
	UIBinder->SetTextColor(FName("video_value"), bVideoLive ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetVisibility(FName("videoLost"), !bVideoLive);

	bool bGazeConnected = Gaze->IsTrackerConnected();
	UIBinder->SetText(FName("input_value"), bGazeConnected ? TEXT("CONNECTED") : TEXT("NOT FOUND"));
	UIBinder->SetTextColor(FName("input_value"), bGazeConnected ? FLinearColor::Green : FLinearColor::Red);

	UIBinder->SetText(FName("operator_state_value"), StateToString(OperatorState_));
	UIBinder->SetText(FName("avatar_state_value"), StateToString(ComLink->GetAvatarState()));

	UIBinder->SetText(FName("LeftGearValue"), FString::Printf(TEXT("%d"), LeftTracked->GetScaleFactor()));
	UIBinder->SetText(FName("LeftClutchValue"), FString::Printf(TEXT("%.0f%%"), LeftTracked->GetClutchFactor() * 100.0f));
	UIBinder->SetText(FName("RightGearValue"), FString::Printf(TEXT("%d"), RightTracked->GetScaleFactor()));
	UIBinder->SetText(FName("RightClutchValue"), FString::Printf(TEXT("%.0f%%"), RightTracked->GetClutchFactor() * 100.0f));

	static const TArray<FString> HealthLabels = { TEXT("NO SIGNAL"), TEXT("NORMAL"), TEXT("DEGRADED"), TEXT("CRITICAL"), TEXT("STALE") };
	static const TArray<FLinearColor> HealthColors = { FLinearColor::Gray, FLinearColor::Green, FLinearColor::Yellow, FLinearColor::Red, FLinearColor::Gray };

	uint8 HealthIdx = Stats.StreamHealthState;
	if (HealthIdx < HealthLabels.Num())
	{
		UIBinder->SetText(FName("stream_health_value"), *HealthLabels[HealthIdx]);
		UIBinder->SetTextColor(FName("stream_health_value"), HealthColors[HealthIdx]);
	}

	if (LeftTracked->IsGraspHeld() && !bLeftWasGrasping) {
		SoundFeedback->PlayAtLocation(ESoundType::Confirm, LeftController->GetComponentLocation());
	}
	bLeftWasGrasping = LeftTracked->IsGraspHeld();

	if (RightTracked->IsGraspHeld() && !bRightWasGrasping) {
		SoundFeedback->PlayAtLocation(ESoundType::Confirm, RightController->GetComponentLocation());
	}
	bRightWasGrasping = RightTracked->IsGraspHeld();
}

void AOperatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) {
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}


// ============================================================
// Tray
// ============================================================
void AOperatorPawn::UpdateTray(float DeltaTime, const FGazeData& GazeData) {
	if (!TrayBinder) return;
	Tray.Update(DeltaTime, GazeData, TrayBinder);
	TrayBinder->SetGazeInput(GazeData);
	FName TrayPressed = TrayBinder->ConsumePress();
	if (TrayPressed != FName()) {
		HandleTrayPress(TrayPressed);
	}
}

void AOperatorPawn::HandleTrayPress(FName ButtonPressed) {
	UE_LOG(LogTemp, Log, TEXT("TrayBinder: press=%s"), *ButtonPressed.ToString());
}


// ============================================================
// State machine
// ============================================================
void AOperatorPawn::UpdateStateMachine() {
	if (CheckEmergencyStop()) {
		TransitionTo(ESysState::Idle);
		ComLink->SendStateRequest(SysState::IDLE);
		return;
	}

	if (OperatorState_ != ESysState::Offline && !ComLink->IsAvatarAlive()) {
		TransitionTo(ESysState::Offline);
		SoundFeedback->Play(ESoundType::Warning);
		return;
	}

	FName ButtonPressed = UIBinder->ConsumePress();
	FName ButtonRejected = UIBinder->ConsumeRejection();
	if (ButtonRejected != FName()) {
		SoundFeedback->Play(ESoundType::Reject);
	}
	ESysState AvatarState = ComLink->GetAvatarState();

	switch (OperatorState_) {
	case ESysState::Offline:
		if (ComLink->IsAvatarAlive() && Gaze->IsTrackerConnected()) {
			TransitionTo(ESysState::Idle);
		}
		break;

	case ESysState::Idle:
		if (!ComLink->IsAvatarAlive()) {
			TransitionTo(ESysState::Offline);
		}
		else if (ButtonPressed == FName("startButton")) {
			ComLink->SendStateRequest(SysState::HOMING);
			TransitionTo(ESysState::Homing);
		}
		break;

	case ESysState::Homing:
		if (ButtonPressed == FName("startButton")) {
			ComLink->SendStateRequest(SysState::IDLE);
			TransitionTo(ESysState::Idle);
		}
		else if (AvatarState == ESysState::Awaiting) {
			CaptureControllerOrigins();
			TransitionTo(ESysState::Awaiting);
		}
		break;

	case ESysState::Awaiting:
		if (ButtonPressed == FName("startButton")) {
			ComLink->SendStateRequest(SysState::IDLE);
			TransitionTo(ESysState::Idle);
		}
		else if (ButtonPressed == FName("engageButton")) {
			CaptureControllerOrigins();
			ComLink->SendStateRequest(SysState::ENGAGED);
			TransitionTo(ESysState::Engaged);
		}
		break;

	case ESysState::Engaged:
		if (ButtonPressed == FName("startButton")) {
			ComLink->SendStateRequest(SysState::IDLE);
			TransitionTo(ESysState::Idle);
		}
		else if (ButtonPressed == FName("engageButton")) {
			ComLink->SendStateRequest(SysState::PAUSED);
			TransitionTo(ESysState::Paused);
		}
		else {
			SendArmCommands();
			SendHeadCommand();
		}
		break;

	case ESysState::Paused:
		if (ButtonPressed == FName("startButton")) {
			ComLink->SendStateRequest(SysState::IDLE);
			TransitionTo(ESysState::Idle);
		}
		else if (ButtonPressed == FName("engageButton")) {
			CaptureControllerOrigins();
			ComLink->SendStateRequest(SysState::ENGAGED);
			TransitionTo(ESysState::Engaged);
		}
		break;

	default:
		break;
	}
}

void AOperatorPawn::TransitionTo(ESysState NewState) {
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: %d -> %d"), static_cast<int>(OperatorState_), static_cast<int>(NewState));
	SoundFeedback->Play(ESoundType::Transition);
	OperatorState_ = NewState;
	UpdateButtonStates();
}

void AOperatorPawn::UpdateButtonStates() {
	switch (OperatorState_) {

	case ESysState::Offline:
		UIBinder->SetButtonLocked(FName("startButton"), true);
		UIBinder->SetButtonLocked(FName("engageButton"), true);
		UIBinder->SetText(FName("startLabel"), TEXT("Start"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	case ESysState::Idle:
		UIBinder->SetButtonLocked(FName("startButton"), false);
		UIBinder->SetButtonLocked(FName("engageButton"), true);
		UIBinder->SetText(FName("startLabel"), TEXT("Start"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	case ESysState::Homing:
		UIBinder->SetButtonLocked(FName("startButton"), false);
		UIBinder->SetButtonLocked(FName("engageButton"), true);
		UIBinder->SetText(FName("startLabel"), TEXT("Stop"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	case ESysState::Awaiting:
		UIBinder->SetButtonLocked(FName("startButton"), false);
		UIBinder->SetButtonLocked(FName("engageButton"), false);
		UIBinder->SetText(FName("startLabel"), TEXT("Stop"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	case ESysState::Engaged:
		UIBinder->SetButtonLocked(FName("startButton"), false);
		UIBinder->SetButtonLocked(FName("engageButton"), false);
		UIBinder->SetText(FName("startLabel"), TEXT("Stop"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Pause"));
		break;

	case ESysState::Paused:
		UIBinder->SetButtonLocked(FName("startButton"), false);
		UIBinder->SetButtonLocked(FName("engageButton"), false);
		UIBinder->SetText(FName("startLabel"), TEXT("Stop"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	default:
		break;
	}
}

bool AOperatorPawn::CheckEmergencyStop() {
	if (OperatorState_ == ESysState::Offline || OperatorState_ == ESysState::Idle) {
		return false;
	}
	bool bStop = LeftTracked->IsMenuPressed() || RightTracked->IsMenuPressed();
	if (bStop) {
		SoundFeedback->Play(ESoundType::Warning);
		LeftTracked->ConsumeMenuPress();
		RightTracked->ConsumeMenuPress();
	}
	return bStop;
}

void AOperatorPawn::CaptureControllerOrigins() {
	LeftTracked->CaptureOrigin();
	RightTracked->CaptureOrigin();

	if (VRCamera) {
		HMDOrigin_ = VRCamera->GetComponentTransform();
		bHMDOriginValid_ = true;
	}
}

void AOperatorPawn::SendArmCommands() {
	if (LeftTracked->IsTracking() && !LeftTracked->IsFullClutch()) {
		FControllerDeltaPose Delta = LeftTracked->GetDeltaPose();
		ArmCommandMsg Msg{};
		CoordConvert::UnrealToProtocolFloat(Delta.Translation, Msg.position[0], Msg.position[1], Msg.position[2]);
		CoordConvert::UnrealToProtocolQuatFloat(Delta.Rotation, Msg.quaternion[0], Msg.quaternion[1], Msg.quaternion[2], Msg.quaternion[3]);
		Msg.gripper = LeftTracked->IsGraspHeld() ? 1.0f : 0.0f;
		ComLink->SendArmCommand(Msg, 0);
	}

	if (RightTracked->IsTracking() && !RightTracked->IsFullClutch()) {
		FControllerDeltaPose Delta = RightTracked->GetDeltaPose();
		ArmCommandMsg Msg{};
		CoordConvert::UnrealToProtocolFloat(Delta.Translation, Msg.position[0], Msg.position[1], Msg.position[2]);
		CoordConvert::UnrealToProtocolQuatFloat(Delta.Rotation, Msg.quaternion[0], Msg.quaternion[1], Msg.quaternion[2], Msg.quaternion[3]);
		Msg.gripper = RightTracked->IsGraspHeld() ? 1.0f : 0.0f;
		ComLink->SendArmCommand(Msg, 1);

		const FString QuatText = FString::Printf(TEXT("w:%.3f x:%.3f y:%.3f z:%.3f"), Msg.quaternion[0], Msg.quaternion[1], Msg.quaternion[2], Msg.quaternion[3]);
		UIBinder->SetText(FName("angleDelta"), QuatText);
	}
}

void AOperatorPawn::SendHeadCommand() {
	if (!bHMDOriginValid_ || !VRCamera) return;

	FTransform CurrentHMD = VRCamera->GetComponentTransform();
	FQuat DeltaQuat = HMDOrigin_.GetRotation().Inverse() * CurrentHMD.GetRotation();
	FRotator DeltaRot = DeltaQuat.Rotator();

	HeadCommandMsg Msg{};
	Msg.pan = static_cast<float>(FMath::DegreesToRadians(-DeltaRot.Yaw));
	Msg.tilt = static_cast<float>(FMath::DegreesToRadians(DeltaRot.Pitch));
	ComLink->SendHeadCommand(Msg);
}
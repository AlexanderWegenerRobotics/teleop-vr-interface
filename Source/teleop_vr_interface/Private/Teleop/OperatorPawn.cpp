#include "Teleop/OperatorPawn.h"
#include "Video/GStreamerSource.h"
#include "Teleop/TeleOpConfig.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/Paths.h"
#include "Shared/annotation_msg.hpp"

namespace {
struct FGazeSampleMsg {
    uint64_t frame_id     = 0;
    float    gaze_px_x    = 0.f;
    float    gaze_px_y    = 0.f;
    uint64_t timestamp_ns = 0;
    MSGPACK_DEFINE_MAP(frame_id, gaze_px_x, gaze_px_y, timestamp_ns)
};
}

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
	LeftTracked = CreateDefaultSubobject<UTrackedControllerComponent>(TEXT("LeftTracked"));
	RightTracked = CreateDefaultSubobject<UTrackedControllerComponent>(TEXT("RightTracked"));
	//VoiceAnnotator = CreateDefaultSubobject<UVoiceAnnotatorComponent>(TEXT("VoiceAnnotator"));
	GhostOverlay = CreateDefaultSubobject<UGhostOverlayComponent>(TEXT("GhostOverlay"));
	GhostOverlay->SetCamera(VRCamera);
	GhostOverlay->SetComLink(ComLink);
	GhostOverlay->SetRightHand(RightController);
	GhostOverlay->SetRightTracked(RightTracked);

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
	GstConfig.SenderIP         = Config->Stream.RemoteIP;
	GstConfig.ReportIntervalMs = Config->Stream.ReportIntervalMs;
	GstConfig.StatusPort       = Config->Stream.StatusPort;
	VideoFeed->RegisterSource(TEXT("AvatarStream"), MakeUnique<FGStreamerSource>(GstConfig));
	VideoFeed->SetStereoMode(Config->Stream.bStereo);
	GhostOverlay->SetStereoMode(Config->Stream.bStereo);

	Super::BeginPlay();

	if (Config->Stream.bStereo) {
		VideoFeed->SetGhostTextures(GhostOverlay->GetRenderTargetLeft(), GhostOverlay->GetRenderTargetRight());
	}

	//VoiceAnnotator->OnAnnotationReceived.AddUObject(this, &AOperatorPawn::HandleVoiceAnnotation);

	if (APlayerController* PC = Cast<APlayerController>(GetController())) {
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())) {
			Subsystem->AddMappingContext(InputMappingContext, 0);
		}
	}

	LeftTracked->bDrawDebugRay = true;
	RightTracked->bDrawDebugRay = true;

	UIBinder->Initialize(UIWidgetClass, VRCamera, FVector2D(1280.0f, 800.0f), 690.0f, 1);
	UIBinder->SetVisibility(FName("statsPanel"), false);
	
	const float LatencyWarn = Config->Hud.LatencyWarningMs;
	UIBinder->BindPlot(FName("videoLatencyPlot"), LatencyHistory.GetSamplesPtr(), nullptr, LatencyHistory.Capacity(), LatencyHistory.GetHeadPtr(), 0.0f, LatencyWarn * 2.0f);
	UIBinder->SetPlotThreshold(FName("videoLatencyPlot"), LatencyWarn);

	const float DataLatencyWarn = Config->Hud.DataLatencyWarningMs;
	UIBinder->BindPlot(FName("dataLatencyPlot"), JitterHistory.GetSamplesPtr(), nullptr, JitterHistory.Capacity(), JitterHistory.GetHeadPtr(), 0.0f, DataLatencyWarn * 2.0f);
	UIBinder->SetPlotThreshold(FName("dataLatencyPlot"), DataLatencyWarn);

	ComLink->RegisterHandler("device_event", [this](const FReliableEnvelope& Env) {
		std::map<std::string, msgpack::object> Fields;
		Env.payload.convert(Fields);
		auto DevIt = Fields.find("device");
		auto EvtIt = Fields.find("event");
		if (DevIt == Fields.end() || EvtIt == Fields.end()) return;

		std::string Device = DevIt->second.as<std::string>();
		std::string Event = EvtIt->second.as<std::string>();

		if (Event == "reset_complete") {
			if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("ARM_RESET_COMPLETE device=%s"), UTF8_TO_TCHAR(Device.c_str())));
			if (Device == "arm_left") {
				LeftTracked->CaptureOrigin();
				SendArmResume("arm_left");
				LeftArmResetState_ = EArmResetState::Idle;
			}
			if (Device == "arm_right") {
				RightTracked->CaptureOrigin();
				SendArmResume("arm_right");
				RightArmResetState_ = EArmResetState::Idle;
			}
			UpdateButtonStates();
			SoundFeedback->Play(ESoundType::Transition);
		}
		});

	UpdateButtonStates();

	float AspectRatio = 1280.f / 720.f;
	FGazeProjection::ComputeQuadSize(VideoFeed->PlaneDistance, VideoFeed->FOVCoverage, AspectRatio, VideoQuadWidth_, VideoQuadHeight_);
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		VideoLogger_ = GetWorld()->SpawnActor<AVideoLogger>(AVideoLogger::StaticClass(), FTransform::Identity, Params);
	}

	if (VideoLogger_) {
		FVector2D VideoUVOffset = FVector2D::ZeroVector;
		FVector2D VideoUVSize   = FVector2D::UnitVector;
		if (VideoFeed->IsStereoMode()) {
			VideoUVOffset = FVector2D(0.0f, 0.0f);
			VideoUVSize   = FVector2D(0.5f, 1.0f);
		}
		VideoLogger_->AddLayerSource([this]() -> UTexture* { return VideoFeed->GetVideoTexture(); }, 0, VideoUVOffset, VideoUVSize);
		if (VideoFeed->IsStereoMode()) {
			VideoLogger_->AddLayerSource([this]() -> UTexture* { return static_cast<UTexture*>(GhostOverlay->GetRenderTargetLeft()); }, 1);
		} else {
			VideoLogger_->AddLayerSource([this]() -> UTexture* { return static_cast<UTexture*>(GhostOverlay->GetRenderTarget()); }, 1);
		}
		VideoLogger_->AddLayerSource([this]() -> UTexture* { return static_cast<UTexture*>(UIBinder->GetRenderTarget()); }, 2);

		VideoLogger_->QuadWidth = VideoQuadWidth_;
		VideoLogger_->QuadHeight = VideoQuadHeight_;
		VideoLogger_->PlaneDistance = VideoFeed->PlaneDistance;

		UE_LOG(LogTemp, Log, TEXT("OperatorPawn: VideoLogger spawned"));
	}

	if (VideoLogger_) {
		VideoLogger_->StartLogging();
	}

	Logger_ = MakeUnique<FTeleOpLogger>();
	FString SessionDir = Logger_->Open(FPaths::ProjectDir() / LogBaseDirectory);
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: logging to %s"), *SessionDir);

	SessionStartTime_ = FPlatformTime::Seconds();
}


void AOperatorPawn::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	if (VideoLogger_) VideoLogger_->StopLogging(TEXT("EndPlay"));
	if (Logger_) Logger_->Close();
	Super::EndPlay(EndPlayReason);
}


void AOperatorPawn::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);

	UpdateStateMachine();

	FVideoSourceStats Stats = VideoFeed->GetStreamStats();
	LatencyHistory.Push(Stats.OneWayLatencyMs);
	JitterHistory.Push(ComLink->GetArmStateLatencyMs(0));
	LossHistory.Push(Stats.PacketLossPercent);

	const FGazeData& GazeData = Gaze->GetGazeData();
	UIBinder->SetGazeInput(GazeData);

	if (ComLink->GetAvatarState() == ESysState::Engaged) UIBinder->SetImageColor(FName("avatar_torso"), FLinearColor::Green);
	else if (OperatorState_ == ESysState::Homing || OperatorState_ == ESysState::Awaiting)
		UIBinder->SetImageColor(FName("avatar_torso"), FLinearColor(255, 128, 13));
	else UIBinder->SetImageColor(FName("avatar_torso"), FLinearColor::Red);

	UIBinder->SetImageColor(FName("avatar_eye"), VideoFeed->IsReceiving() ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetImageColor(FName("avatar_left_arm"), ComLink->GetArmRemoteState(0) == SysState::ENGAGED ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetImageColor(FName("avatar_right_arm"), ComLink->GetArmRemoteState(1) == SysState::ENGAGED ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetImageColor(FName("avatar_head"), ComLink->IsHeadAlive() ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetVisibility(FName("videoLostInfo"), !VideoFeed->IsReceiving());

	UIBinder->SetImageColor(FName("operator_eye"), Gaze->IsTrackerConnected() ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetImageColor(FName("operator_left_arm"), LeftTracked->IsTracking() ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetImageColor(FName("operator_right_arm"), RightTracked->IsTracking() ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetImageColor(FName("operator_head"), bHMDOriginValid_ ? FLinearColor::Green : FLinearColor::Red);

	ESysState av_state = ComLink->GetAvatarState();
	if (av_state == ESysState::Engaged) UIBinder->SetImageColor(FName("operator_torso"), FLinearColor::Green);
	else if (av_state == ESysState::Homing || av_state == ESysState::Awaiting)
		UIBinder->SetImageColor(FName("operator_torso"), FLinearColor(255, 128, 13));
	else UIBinder->SetImageColor(FName("operator_torso"), FLinearColor::Red);

	static const TArray<FString> HealthLabels = { TEXT("NO SIGNAL"), TEXT("NORMAL"), TEXT("DEGRADED"), TEXT("CRITICAL"), TEXT("STALE") };
	static const TArray<FLinearColor> HealthColors = { FLinearColor::Gray, FLinearColor::Green, FLinearColor::Yellow, FLinearColor::Red, FLinearColor::Gray };

	uint8 HealthIdx = Stats.StreamHealthState;
	if (HealthIdx < HealthLabels.Num()) {
		UIBinder->SetText(FName("stream_health_value"), *HealthLabels[HealthIdx]);
		UIBinder->SetTextColor(FName("stream_health_value"), HealthColors[HealthIdx]);
	}

	UpdateInfoBar();

	bool bLeftGrasping  = LeftTracked->IsGraspHeld();
	bool bRightGrasping = RightTracked->IsGraspHeld();

	if (bLeftGrasping && !bLeftWasGrasping) {
		SoundFeedback->PlayAtLocation(ESoundType::Confirm, LeftController->GetComponentLocation());
	}
	if (bRightGrasping && !bRightWasGrasping) {
		SoundFeedback->PlayAtLocation(ESoundType::Confirm, RightController->GetComponentLocation());
	}

	if (Logger_) {
		// --- gear change events ---
		uint8 LeftGear  = LeftTracked->GetScaleFactor();
		uint8 RightGear = RightTracked->GetScaleFactor();
		if (LeftGear != PrevLeftGear_) {
			Logger_->LogEvent(FString::Printf(TEXT("GEAR_CHANGE side=left gear=%d"), LeftGear));
			PrevLeftGear_ = LeftGear;
		}
		if (RightGear != PrevRightGear_) {
			Logger_->LogEvent(FString::Printf(TEXT("GEAR_CHANGE side=right gear=%d"), RightGear));
			PrevRightGear_ = RightGear;
		}

		// --- clutch transition events ---
		bool bLeftClutch  = LeftTracked->IsFullClutch();
		bool bRightClutch = RightTracked->IsFullClutch();
		if (bLeftClutch != bPrevLeftClutch_) {
			Logger_->LogEvent(FString::Printf(TEXT("CLUTCH side=left state=%s"), bLeftClutch ? TEXT("engaged") : TEXT("disengaged")));
			bPrevLeftClutch_ = bLeftClutch;
		}
		if (bRightClutch != bPrevRightClutch_) {
			Logger_->LogEvent(FString::Printf(TEXT("CLUTCH side=right state=%s"), bRightClutch ? TEXT("engaged") : TEXT("disengaged")));
			bPrevRightClutch_ = bRightClutch;
		}

		// --- grasp transition events ---
		if (bLeftGrasping != bLeftWasGrasping) {
			Logger_->LogEvent(FString::Printf(TEXT("GRASP side=left state=%s"),
				bLeftGrasping ? TEXT("held") : TEXT("released")));
		}
		if (bRightGrasping != bRightWasGrasping) {
			Logger_->LogEvent(FString::Printf(TEXT("GRASP side=right state=%s"),
				bRightGrasping ? TEXT("held") : TEXT("released")));
		}

		// --- stream row ---
		FStreamRow Row;
		Row.TimestampNs   = FTeleOpLogger::NowNs();
		Row.OperatorState = static_cast<uint8>(OperatorState_);

		Row.LeftClutch = LeftTracked->GetClutchFactor();
		Row.LeftGear   = LeftGear;
		Row.LeftGrasp  = bLeftGrasping ? 1.f : 0.f;

		Row.RightClutch = RightTracked->GetClutchFactor();
		Row.RightGear   = RightGear;
		Row.RightGrasp  = bRightGrasping ? 1.f : 0.f;

		// Controller delta poses in protocol coordinates (always, regardless of clutch)
		{
			FControllerDeltaPose L = LeftTracked->GetDeltaPose();
			CoordConvert::UnrealToProtocolFloat(L.Translation, Row.LeftPx, Row.LeftPy, Row.LeftPz);
			CoordConvert::UnrealToProtocolQuatFloat(L.Rotation, Row.LeftQw, Row.LeftQx, Row.LeftQy, Row.LeftQz);

			FControllerDeltaPose R = RightTracked->GetDeltaPose();
			CoordConvert::UnrealToProtocolFloat(R.Translation, Row.RightPx, Row.RightPy, Row.RightPz);
			CoordConvert::UnrealToProtocolQuatFloat(R.Rotation, Row.RightQw, Row.RightQx, Row.RightQy, Row.RightQz);
		}

		Row.HeadPan  = LastHeadPan_;
		Row.HeadTilt = LastHeadTilt_;

		Row.VideoLatencyMs = Stats.OneWayLatencyMs;
		Row.VideoJitterMs  = Stats.JitterMs;
		Row.VideoLossPct   = Stats.PacketLossPercent;
		Row.VideoFps       = Stats.CurrentFPS;

		Row.DataLatencyMs = ComLink->GetArmStateLatencyMs(0);
		Row.DataMsgRateHz = ComLink->GetArmMsgRateHz(0);

		Logger_->WriteStreamRow(Row);
	}

	bLeftWasGrasping  = bLeftGrasping;
	bRightWasGrasping = bRightGrasping;

	if (VideoLogger_ && VideoLogger_->IsLogging()) {
		VideoLogger_->SubmitFrame(BuildFrameBundle());
	}
}

void AOperatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) {
	Super::SetupPlayerInputComponent(PlayerInputComponent);
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
		if (Logger_) Logger_->LogEvent(TEXT("BUTTON_REJECTED button=") + ButtonRejected.ToString());
	}
	if (ButtonPressed != FName() && Logger_) {
		Logger_->LogEvent(TEXT("BUTTON_PRESS button=") + ButtonPressed.ToString());
	}
	ESysState AvatarState = ComLink->GetAvatarState();


	switch (OperatorState_) {
	case ESysState::Offline:
		if (ComLink->IsAvatarAlive() && Gaze->IsTrackerConnected()) {
			if (bPendingVoiceReengage_) {
				ComLink->SendStateRequest(SysState::HOMING);
				TransitionTo(ESysState::Homing);
			} else {
				TransitionTo(ESysState::Idle);
			}
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
			if (bPendingVoiceReengage_) {
				ComLink->SendStateRequest(SysState::ENGAGED);
				bAvatarConfirmedEngaged_ = false;
				bPendingVoiceReengage_ = false;
				TransitionTo(ESysState::Engaged);
			} else {
				TransitionTo(ESysState::Awaiting);
			}
		}
		break;

	case ESysState::Awaiting:
		if (ButtonPressed == FName("startButton")) {
			ComLink->SendStateRequest(SysState::IDLE);
			TransitionTo(ESysState::Idle);
		}
		else if (bPendingVoiceReengage_ || ButtonPressed == FName("engageButton")) {
			CaptureControllerOrigins();
			ComLink->SendStateRequest(SysState::ENGAGED);
			bAvatarConfirmedEngaged_ = false;
			bPendingVoiceReengage_ = false;
			TransitionTo(ESysState::Engaged);
		}
		break;

	case ESysState::Engaged:
		if (!bAvatarConfirmedEngaged_ && AvatarState == ESysState::Engaged) {
			bAvatarConfirmedEngaged_ = true;
		}

		if (ButtonPressed == FName("startButton")) {
			ComLink->SendStateRequest(SysState::IDLE);
			TransitionTo(ESysState::Idle);
		}
		else if (ButtonPressed == FName("engageButton")) {
			ComLink->SendStateRequest(SysState::PAUSED);
			TransitionTo(ESysState::Paused);
		}
		else if (ButtonPressed == FName("resetButtonLeft") && LeftArmResetState_ == EArmResetState::Idle) {
			SendArmReset("arm_left");
			LeftArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("resetButtonRight") && RightArmResetState_ == EArmResetState::Idle) {
			SendArmReset("arm_right");
			RightArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("resetButton")) {
			SendResetAll();
			if (LeftArmResetState_ == EArmResetState::Idle) LeftArmResetState_ = EArmResetState::Recovering;
			if (RightArmResetState_ == EArmResetState::Idle) RightArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (bAvatarConfirmedEngaged_ && AvatarState == ESysState::Awaiting) {
			// Avatar dropped back to awaiting after confirming engaged (e.g. arm fault).
			CaptureControllerOrigins();
			bAvatarConfirmedEngaged_ = false;
			TransitionTo(ESysState::Awaiting);
		}
		else {
			SendArmCommands();
			SendHeadCommand();
			SendGazeSample();
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
			bAvatarConfirmedEngaged_ = false;
			TransitionTo(ESysState::Engaged);
		}
		else if (ButtonPressed == FName("resetButtonLeft") && LeftArmResetState_ == EArmResetState::Idle) {
			SendArmReset("arm_left");
			LeftArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("resetButtonRight") && RightArmResetState_ == EArmResetState::Idle) {
			SendArmReset("arm_right");
			RightArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("resetButton")) {
			SendResetAll();
			if (LeftArmResetState_ == EArmResetState::Idle) LeftArmResetState_ = EArmResetState::Recovering;
			if (RightArmResetState_ == EArmResetState::Idle) RightArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		break;

	default:
		break;
	}

	if (ButtonPressed == FName("statisticsButton")) {
		bStatsVisible_ = !bStatsVisible_;
		UIBinder->SetVisibility(FName("statsPanel"), bStatsVisible_);
	}
}

void AOperatorPawn::UpdateInfoBar() {
	// Episode number
	UIBinder->SetText(FName("ep_value"), FString::Printf(TEXT("%03d"), EpisodeCount_));

	// Session elapsed time formatted as H:MM:SS
	int32 ElapsedSec = static_cast<int32>(FPlatformTime::Seconds() - SessionStartTime_);
	int32 Hours      = ElapsedSec / 3600;
	int32 Minutes    = (ElapsedSec % 3600) / 60;
	int32 Seconds    = ElapsedSec % 60;
	UIBinder->SetText(FName("session_time_value"), FString::Printf(TEXT("%d:%02d:%02d"), Hours, Minutes, Seconds));

	// Use first arm that has live data (right=1 preferred, fall back to left=0)
	float LatencyMs = ComLink->GetArmStateLatencyMs(1);
	if (LatencyMs <= 0.f) LatencyMs = ComLink->GetArmStateLatencyMs(0);
	if (LatencyMs > 0.f)
		UIBinder->SetText(FName("latency_value"), FString::Printf(TEXT("%.1f ms"), LatencyMs));
	else
		UIBinder->SetText(FName("latency_value"), TEXT("-- ms"));

	FLinearColor DotColor;
	if      (LatencyMs <= 0.f) DotColor = FLinearColor::Gray;
	else if (LatencyMs < 30.f) DotColor = FLinearColor::Green;
	else if (LatencyMs < 80.f) DotColor = FLinearColor::Yellow;
	else                       DotColor = FLinearColor::Red;
	UIBinder->SetTextColor(FName("latency_dot"), DotColor);
}

void AOperatorPawn::TransitionTo(ESysState NewState) {
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: %d -> %d"), static_cast<int>(OperatorState_), static_cast<int>(NewState));
	if (Logger_) {
		Logger_->LogEvent(FString::Printf(TEXT("STATE_TRANSITION old=%s new=%s"),*StateToString(OperatorState_), *StateToString(NewState)));
	}
	if (NewState == ESysState::Engaged) ++EpisodeCount_;
	if (NewState == ESysState::Idle)    bPendingVoiceReengage_ = false;
	SoundFeedback->Play(ESoundType::Transition);
	OperatorState_ = NewState;
	UpdateButtonStates();
}

void AOperatorPawn::UpdateButtonStates() {
	bool bCanReset = (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused);
	bool bAnyRecovering = LeftArmResetState_ == EArmResetState::Recovering
		|| RightArmResetState_ == EArmResetState::Recovering;

	auto ApplyResetButton = [&](FName Button, FName Label, EArmResetState ResetState, const TCHAR* Side) {
		switch (ResetState) {
		case EArmResetState::Idle:
			UIBinder->SetButtonToggled(Button, false);
			UIBinder->SetButtonLocked(Button, !bCanReset);
			UIBinder->SetText(Label, FString::Printf(TEXT("Reset %s"), Side));
			break;
		case EArmResetState::Recovering:
		case EArmResetState::AwaitingResume:
			UIBinder->SetButtonToggled(Button, true);   // shows Pressed image (amber tint)
			UIBinder->SetButtonLocked(Button, true);
			UIBinder->SetText(Label, FString::Printf(TEXT("Resetting %s..."), Side));
			break;
		}
		};

	ApplyResetButton(FName("resetButtonLeft"), FName("resetLabelLeft"), LeftArmResetState_, TEXT("L"));
	ApplyResetButton(FName("resetButtonRight"), FName("resetLabelRight"), RightArmResetState_, TEXT("R"));

	bool bBothIdle = LeftArmResetState_ == EArmResetState::Idle && RightArmResetState_ == EArmResetState::Idle;
	bool bResetting = !bBothIdle;
	UIBinder->SetButtonToggled(FName("resetButton"), bResetting);
	UIBinder->SetButtonLocked(FName("resetButton"), !bCanReset || bResetting);
	UIBinder->SetText(FName("resetLabel"), TEXT("Reset All"));

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
		UIBinder->SetButtonLocked(FName("startButton"), bAnyRecovering);
		UIBinder->SetButtonLocked(FName("engageButton"), bAnyRecovering);
		UIBinder->SetText(FName("startLabel"), TEXT("Stop"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Pause"));
		break;

	case ESysState::Paused:
		UIBinder->SetButtonLocked(FName("startButton"), bAnyRecovering);
		UIBinder->SetButtonLocked(FName("engageButton"), bAnyRecovering);
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
		if (Logger_) Logger_->LogEvent(TEXT("EMERGENCY_STOP"));
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
	bool bLeftActive = LeftArmResetState_ == EArmResetState::Idle && ComLink->GetArmRemoteState(0) == SysState::ENGAGED;
	bool bRightActive = RightArmResetState_ == EArmResetState::Idle && ComLink->GetArmRemoteState(1) == SysState::ENGAGED;

	FQuat HMDYawQuat = FQuat::Identity;
	if (bHMDOriginValid_) {
		float CaptureYaw = HMDOrigin_.GetRotation().Rotator().Yaw;
		HMDYawQuat = FQuat(FRotator(0.f, CaptureYaw, 0.f));
	}

	if (bLeftActive && LeftTracked->IsTracking() && !LeftTracked->IsFullClutch()) {
		ArmCommandMsg Msg{};
		FControllerDeltaPose Delta = LeftTracked->GetDeltaPose();
		FVector LocalTranslation = HMDYawQuat.UnrotateVector(Delta.Translation);
		CoordConvert::UnrealToProtocolFloat(LocalTranslation, Msg.position[0], Msg.position[1], Msg.position[2]);
		CoordConvert::UnrealToProtocolQuatFloat(Delta.Rotation, Msg.quaternion[0], Msg.quaternion[1], Msg.quaternion[2], Msg.quaternion[3]);
		Msg.gripper = LeftTracked->IsGraspHeld() ? 1.0f : 0.0f;
		ComLink->SendArmCommand(Msg, 0);
	}

	if (bRightActive && RightTracked->IsTracking() && !RightTracked->IsFullClutch()) {
		ArmCommandMsg Msg{};
		FControllerDeltaPose Delta = RightTracked->GetDeltaPose();
		FVector LocalTranslation = HMDYawQuat.UnrotateVector(Delta.Translation);
		CoordConvert::UnrealToProtocolFloat(LocalTranslation, Msg.position[0], Msg.position[1], Msg.position[2]);
		CoordConvert::UnrealToProtocolQuatFloat(Delta.Rotation, Msg.quaternion[0], Msg.quaternion[1], Msg.quaternion[2], Msg.quaternion[3]);
		Msg.gripper = RightTracked->IsGraspHeld() ? 1.0f : 0.0f;
		ComLink->SendArmCommand(Msg, 1);
	}
}

void AOperatorPawn::SendHeadCommand() {
	if (!bHMDOriginValid_ || !VRCamera) return;

	FTransform CurrentHMD = VRCamera->GetComponentTransform();
	FQuat DeltaQuat = HMDOrigin_.GetRotation().Inverse() * CurrentHMD.GetRotation();
	FRotator DeltaRot = DeltaQuat.Rotator();

	HeadCommandMsg Msg{};
	Msg.pan  = static_cast<float>(FMath::DegreesToRadians(-DeltaRot.Yaw));
	Msg.tilt = static_cast<float>(FMath::DegreesToRadians(DeltaRot.Pitch));
	LastHeadPan_  = Msg.pan;
	LastHeadTilt_ = Msg.tilt;
	ComLink->SendHeadCommand(Msg);
}

void AOperatorPawn::SendArmReset(const std::string& DeviceName) {
	msgpack::sbuffer Buf;
	msgpack::pack(Buf, std::map<std::string, std::string>{{"device", DeviceName}});
	ComLink->SendReliable("arm_reset", Buf, true);
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: arm_reset -> %s"), UTF8_TO_TCHAR(DeviceName.c_str()));
	if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("ARM_RESET device=%s"), UTF8_TO_TCHAR(DeviceName.c_str())));
}

void AOperatorPawn::SendArmResume(const std::string& DeviceName) {
	msgpack::sbuffer Buf;
	msgpack::pack(Buf, std::map<std::string, std::string>{{"device", DeviceName}});
	ComLink->SendReliable("arm_resume", Buf, true);
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: arm_resume -> %s"), UTF8_TO_TCHAR(DeviceName.c_str()));
	if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("ARM_RESUME device=%s"), UTF8_TO_TCHAR(DeviceName.c_str())));
}

void AOperatorPawn::SendResetAll() {
	msgpack::sbuffer Buf;
	msgpack::pack(Buf, std::map<std::string, std::string>{{"reason", "operator_reset_all"}});
	ComLink->SendReliable("reset_all", Buf, true);
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: reset_all"));
	if (Logger_) Logger_->LogEvent(TEXT("ARM_RESET device=all"));
}

void AOperatorPawn::SendGazeSample(){
	uint64 FrameId = VideoFeed->GetLastFrameId();
	if (FrameId == 0) return;

	const FGazeData& GazeData = Gaze->GetGazeData();
	if (!GazeData.bIsValid) return;

	if (!VRCamera) return;

	FVector2D GazeUV;
	if (!FGazeProjection::Project(GazeData, VRCamera->GetComponentTransform(), VideoFeed->PlaneDistance, VideoQuadWidth_, VideoQuadHeight_, GazeUV))
		return;

	UTexture2D* Tex = VideoFeed->GetVideoTexture();
	float W = Tex ? static_cast<float>(Tex->GetSizeX()) : 1280.f;
	float H = Tex ? static_cast<float>(Tex->GetSizeY()) : 720.f;

	const FDateTime NowUtc = FDateTime::UtcNow();
	uint64_t NowNs = static_cast<uint64_t>(NowUtc.ToUnixTimestamp()) * 1000000000ULL + static_cast<uint64_t>(NowUtc.GetMillisecond()) * 1000000ULL;

	FGazeSampleMsg Msg;
	Msg.frame_id     = static_cast<uint64_t>(FrameId);
	Msg.gaze_px_x    = GazeUV.X * W;
	Msg.gaze_px_y    = GazeUV.Y * H;
	Msg.timestamp_ns = NowNs;

	msgpack::sbuffer Buf;
	msgpack::pack(Buf, Msg);
	ComLink->SendReliable("gaze_sample", Buf, false);
}

void AOperatorPawn::HandleVoiceAnnotation(const FVoiceAnnotation& Ann) {
	if (Ann.Type == 0) {
		if (Ann.Label.Equals(TEXT("start"), ESearchCase::IgnoreCase)) {
			if (OperatorState_ == ESysState::Idle) {
				ComLink->SendStateRequest(SysState::HOMING);
				TransitionTo(ESysState::Homing);
			}
		} else if (Ann.Label.Equals(TEXT("engage"), ESearchCase::IgnoreCase)) {
			if (OperatorState_ == ESysState::Awaiting) {
				CaptureControllerOrigins();
				ComLink->SendStateRequest(SysState::ENGAGED);
				bAvatarConfirmedEngaged_ = false;
				TransitionTo(ESysState::Engaged);
			} else if (OperatorState_ == ESysState::Paused) {
				CaptureControllerOrigins();
				ComLink->SendStateRequest(SysState::ENGAGED);
				bAvatarConfirmedEngaged_ = false;
				TransitionTo(ESysState::Engaged);
			}
		} else if (Ann.Label.Equals(TEXT("stop"), ESearchCase::IgnoreCase)) {
			ComLink->SendStateRequest(SysState::IDLE);
			TransitionTo(ESysState::Idle);
		} else if (Ann.Label.Equals(TEXT("pause"), ESearchCase::IgnoreCase)) {
			if (OperatorState_ == ESysState::Engaged) {
				ComLink->SendStateRequest(SysState::PAUSED);
				TransitionTo(ESysState::Paused);
			}
		} else if (Ann.Label.Equals(TEXT("reset"), ESearchCase::IgnoreCase)) {
			if (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused) {
				SendResetAll();
				if (LeftArmResetState_  == EArmResetState::Idle) LeftArmResetState_  = EArmResetState::Recovering;
				if (RightArmResetState_ == EArmResetState::Idle) RightArmResetState_ = EArmResetState::Recovering;
				bPendingVoiceReengage_ = true;
				UpdateButtonStates();
			}
		}
		if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("VOICE_CMD label=%s conf=%.2f"), *Ann.Label, Ann.Confidence));
	} else {
		AnnotationMsg Msg;
		Msg.timestamp  = Ann.Timestamp;
		Msg.label      = TCHAR_TO_UTF8(*Ann.Label);
		Msg.atype      = Ann.Type;
		Msg.confidence = Ann.Confidence;
		Msg.score      = Ann.Score;
		Msg.frame_id   = static_cast<uint64_t>(VideoFeed->GetLastFrameId());

		msgpack::sbuffer Buf;
		msgpack::pack(Buf, Msg);
		ComLink->SendReliable("annotation", Buf, false);
		if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("VOICE_ANN label=%s type=%d conf=%.2f"), *Ann.Label, Ann.Type, Ann.Confidence));
	}
}

FFrameBundle AOperatorPawn::BuildFrameBundle() const {
	FFrameBundle Bundle;

	const FDateTime NowUtc = FDateTime::UtcNow();
	const int64 UnixSec = NowUtc.ToUnixTimestamp();
	Bundle.UnixTime = static_cast<double>(UnixSec)
		+ static_cast<double>(NowUtc.GetMillisecond()) / 1000.0;

	Bundle.SenderTimeNs = VideoFeed->GetSenderTimeNs();
	Bundle.FrameIdx = 0;

	const FGazeData& GazeData = Gaze->GetGazeData();
	FVector2D GazeUV(0.5f, 0.5f);
	bool bHit = false;

	if (VRCamera) {
		bHit = FGazeProjection::Project(GazeData, VRCamera->GetComponentTransform(), VideoFeed->PlaneDistance, VideoQuadWidth_, VideoQuadHeight_, GazeUV);
	}

	Bundle.GazeUV = GazeUV;
	Bundle.GazeConfidence = GazeData.Confidence;
	Bundle.bGazeValid = bHit && GazeData.bIsValid;

	return Bundle;
}
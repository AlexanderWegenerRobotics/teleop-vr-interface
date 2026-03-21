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
	UIBinder = CreateDefaultSubobject<UWidgetBinder>(TEXT("UIBinder"));

	static ConstructorHelpers::FClassFinder<UUserWidget> UIClass(TEXT("/Game/UI/WBP_DebugPanel.WBP_DebugPanel_C"));
	if (UIClass.Succeeded()) {
		UIWidgetClass = UIClass.Class;
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("Did not find debug panel"));
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
	VideoFeed->RegisterSource(TEXT("AvatarStream"), MakeUnique<FGStreamerSource>(GstConfig));

	Super::BeginPlay();

	UIBinder->Initialize(UIWidgetClass, VRCamera, FVector2D(1280.0f, 720.0f), 690.0f, 1);
	UIBinder->BindPlot(FName("latencyPlot"), LatencyHistory.GetSamplesPtr(), nullptr, LatencyHistory.Capacity(), LatencyHistory.GetHeadPtr(), 0.0f, 200.0f);
	UIBinder->BindPlot(FName("jitterPlot"), JitterHistory.GetSamplesPtr(), nullptr, JitterHistory.Capacity(), JitterHistory.GetHeadPtr(), 0.0f, 50.0f);
}

void AOperatorPawn::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);

	FVideoSourceStats Stats = VideoFeed->GetStreamStats();
	LatencyHistory.Push(Stats.OneWayLatencyMs);
	JitterHistory.Push(Stats.JitterMs);
	LossHistory.Push(Stats.PacketLossPercent);

	const FGazeData& GazeData = Gaze->GetGazeData();
	UIBinder->SetGazeInput(GazeData);

	bool bVideoLive = VideoFeed->IsReceiving();
	UIBinder->SetText(FName("video_value"), bVideoLive ? TEXT("LIVE") : TEXT("OFFLINE"));
	UIBinder->SetTextColor(FName("video_value"), bVideoLive ? FLinearColor::Green : FLinearColor::Red);

	bool bGazeConnected = Gaze->IsTrackerConnected();
	UIBinder->SetText(FName("input_value"), bGazeConnected ? TEXT("CONNECTED") : TEXT("NOT FOUND"));
	UIBinder->SetTextColor(FName("input_value"), bGazeConnected ? FLinearColor::Green : FLinearColor::Red);

	UIBinder->SetText(FName("testInput"), UIBinder->IsWinkActive() ? TEXT("INPUT") : TEXT("NO INPUT"));
	UIBinder->SetTextColor(FName("testInput"), UIBinder->IsWinkActive() ? FLinearColor::Green : FLinearColor::Red);
}

void AOperatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) {
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void AOperatorPawn::updateStateMachine() {

}
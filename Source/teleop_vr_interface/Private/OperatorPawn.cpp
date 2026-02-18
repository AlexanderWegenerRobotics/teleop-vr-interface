#include "OperatorPawn.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputMappingContext.h" 
#include "UObject/ConstructorHelpers.h"
#include "GStreamerSource.h"

AOperatorPawn::AOperatorPawn() {
	PrimaryActorTick.bCanEverTick = true;

	// VR origin — the root that represents the physical play space origin
	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	SetRootComponent(VROrigin);

	// Camera locked to HMD
	VRCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("VRCamera"));
	VRCamera->SetupAttachment(VROrigin);
	VRCamera->bLockToHmd = true;

	// Motion controllers
	LeftController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftController"));
	LeftController->SetupAttachment(VROrigin);
	LeftController->SetTrackingSource(EControllerHand::Left);

	RightController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightController"));
	RightController->SetupAttachment(VROrigin);
	RightController->SetTrackingSource(EControllerHand::Right);

	// this is a player-possessed pawn, make sure it is kept as is
	AutoPossessAI = EAutoPossessAI::Disabled;
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	PoseMapper = CreateDefaultSubobject<UPoseMapper>(TEXT("PoseMapper"));

	// Load input assets for PoseMapper
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> IMC(TEXT("/Game/Input/IMC_PoseMapper.IMC_PoseMapper"));
	// Trigger
	static ConstructorHelpers::FObjectFinder<UInputAction> LeftTrig(TEXT("/Game/Input/IA_LeftTrigger.IA_LeftTrigger"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightTrig(TEXT("/Game/Input/IA_RightTrigger.IA_RightTrigger"));
	// Grip
	static ConstructorHelpers::FObjectFinder<UInputAction> LeftGrip(TEXT("/Game/Input/IA_LeftGrip.IA_LeftGrip"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightGrip(TEXT("/Game/Input/IA_RightGrip.IA_RightGrip"));

	if (IMC.Succeeded()) PoseMapper->PoseMappingContext = IMC.Object;
	if (LeftTrig.Succeeded()) PoseMapper->LeftTriggerAction = LeftTrig.Object;
	if (RightTrig.Succeeded()) PoseMapper->RightTriggerAction = RightTrig.Object;
	if (LeftGrip.Succeeded()) PoseMapper->LeftGripAction = LeftGrip.Object;
	if (RightGrip.Succeeded()) PoseMapper->RightGripAction = RightGrip.Object;

	ComLink = CreateDefaultSubobject<UComLink>(TEXT("ComLink"));
	VideoFeed = CreateDefaultSubobject<UVideoFeedComponent>(TEXT("VideoFeed"));
	HUD = CreateDefaultSubobject<UHUDComponent>(TEXT("HUD"));
	State = CreateDefaultSubobject<UStateComponent>(TEXT("State"));
}

void AOperatorPawn::BeginPlay() {
	FGStreamerSource::FConfig GstConfig;
	GstConfig.Port = 5000;
	GstConfig.bUseHardwareDecoder = false;
	GstConfig.SRTLatencyMs = 125;
	VideoFeed->RegisterSource(TEXT("LiveStream"), MakeUnique<FGStreamerSource>(GstConfig));

	Super::BeginPlay();

	HUD->RegisterPanel(FName("Control"), LoadClass<UHUDPanelBase>(nullptr, TEXT("/Game/UI/WBP_ControlPanel.WBP_ControlPanel_C")),
		EHUDPlacement::Center, FVector2D(1280.0f, 720.0f), true);

	HUD->RegisterPanel(FName("Debug"), LoadClass<UHUDPanelBase>(nullptr, TEXT("/Game/UI/WBP_DebugPanel.WBP_DebugPanel_C")),
		EHUDPlacement::Center, FVector2D(1280.0f, 720.0f), true);

	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition(0.0f, EOrientPositionSelector::OrientationAndPosition);
	UE_LOG(LogTemp, Log, TEXT("TeleOpPawn::BeginPlay - VR pawn initialized"));
}

void AOperatorPawn::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);
}

void AOperatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) {
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

void AOperatorPawn::OnRightTriggerPressed(){ if (HUD) HUD->PressUI(); }
void AOperatorPawn::OnRightTriggerReleased(){ if (HUD) HUD->ReleaseUI(); }
void AOperatorPawn::OnGripPressed(){ if (State) State->OnGripPressed(); }
void AOperatorPawn::OnGripReleased(){ if (State) State->OnGripReleased(); }
void AOperatorPawn::UIEngage() { if (State) State->RequestEngage(); }
void AOperatorPawn::UIStop() { if (State) State->RequestStop(); }
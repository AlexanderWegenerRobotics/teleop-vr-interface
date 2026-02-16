#include "OperatorPawn.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputMappingContext.h" 
#include "UObject/ConstructorHelpers.h"

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
	static ConstructorHelpers::FObjectFinder<UInputAction> LeftTrig(TEXT("/Game/Input/IA_LeftTrigger.IA_LeftTrigger"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightTrig(TEXT("/Game/Input/IA_RightTrigger.IA_RightTrigger"));

	if (IMC.Succeeded()) PoseMapper->PoseMappingContext = IMC.Object;
	if (LeftTrig.Succeeded()) PoseMapper->LeftTriggerAction = LeftTrig.Object;
	if (RightTrig.Succeeded()) PoseMapper->RightTriggerAction = RightTrig.Object;

	ComLink = CreateDefaultSubobject<UComLink>(TEXT("ComLink"));
}

void AOperatorPawn::BeginPlay()
{
	Super::BeginPlay();
	
	// Reset HMD orientation and position to align with the VR origin
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition(0.0f, EOrientPositionSelector::OrientationAndPosition);

	UE_LOG(LogTemp, Log, TEXT("TeleOpPawn::BeginPlay - VR pawn initialized"));
}

void AOperatorPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AOperatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}


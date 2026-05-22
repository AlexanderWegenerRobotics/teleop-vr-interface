#include "TeleOpGameMode.h"
#include "Teleop/OperatorPawn.h"
#include "GameFramework/PlayerController.h"

ATeleOpGameMode::ATeleOpGameMode()
{
	DefaultPawnClass = AOperatorPawn::StaticClass();
}

// Force the operator pawn to spawn (or possess) with zero yaw so that
// controller world-frame deltas align with the protocol world axes regardless
// of how the pawn / PlayerStart is placed in the level.
void ATeleOpGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	if (NewPlayer)
	{
		if (APawn* Pawn = NewPlayer->GetPawn())
		{
			Pawn->SetActorRotation(FRotator::ZeroRotator);
		}
	}
}

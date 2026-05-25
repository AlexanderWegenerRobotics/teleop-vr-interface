#include "TeleOpGameMode.h"
#include "Teleop/OperatorPawn.h"
#include "GameFramework/PlayerController.h"

ATeleOpGameMode::ATeleOpGameMode()
{
	DefaultPawnClass = AOperatorPawn::StaticClass();
}

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

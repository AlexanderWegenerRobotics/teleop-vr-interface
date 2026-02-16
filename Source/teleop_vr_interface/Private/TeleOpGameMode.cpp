#include "TeleOpGameMode.h"
#include "OperatorPawn.h"

ATeleOpGameMode::ATeleOpGameMode()
{
	DefaultPawnClass = AOperatorPawn::StaticClass();
}
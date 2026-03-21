#include "TeleOpGameMode.h"
#include "Teleop/OperatorPawn.h"

ATeleOpGameMode::ATeleOpGameMode()
{
	DefaultPawnClass = AOperatorPawn::StaticClass();
}
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ATeleOpGameMode.generated.h"

UCLASS()
class TELEOP_VR_INTERFACE_API AATeleOpGameMode : public AActor
{
	GENERATED_BODY()
	
public:	
	AATeleOpGameMode();

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;

};

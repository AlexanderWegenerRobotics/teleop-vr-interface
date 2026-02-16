#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StateComponent.generated.h"

UCLASS()
class TELEOP_VR_INTERFACE_API AStateComponent : public AActor
{
	GENERATED_BODY()
	
public:	
	AStateComponent();

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;

};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PoseMapper.generated.h"

UCLASS()
class TELEOP_VR_INTERFACE_API APoseMapper : public AActor
{
	GENERATED_BODY()
	
public:	
	APoseMapper();

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;

};

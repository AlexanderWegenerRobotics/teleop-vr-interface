#include "StateComponent.h"


AStateComponent::AStateComponent(){
	PrimaryActorTick.bCanEverTick = true;

}

void AStateComponent::BeginPlay()
{
	Super::BeginPlay();
	
}


void AStateComponent::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}


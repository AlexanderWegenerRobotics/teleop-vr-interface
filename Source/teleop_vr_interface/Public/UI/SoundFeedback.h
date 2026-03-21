#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sound/SoundBase.h"
#include "SoundFeedback.generated.h"

UENUM(BlueprintType)
enum class ESoundType : uint8 {
	Confirm,
	Reject,
	Click,
	Warning,
	Transition
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API USoundFeedback : public UActorComponent {
	GENERATED_BODY()

public:
	USoundFeedback();

	virtual void BeginPlay() override;

	void Play(ESoundType Type);
	void PlayAtLocation(ESoundType Type, FVector Location);
	void SetMuted(bool bMute);

private:
	void LoadSound(ESoundType Type, const TCHAR* Path);

	UPROPERTY()
	TMap<ESoundType, USoundBase*> Sounds_;

	bool bMuted_ = false;
};
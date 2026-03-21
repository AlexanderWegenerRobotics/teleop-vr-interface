#include "UI/SoundFeedback.h"
#include "Kismet/GameplayStatics.h"

USoundFeedback::USoundFeedback() {
	PrimaryComponentTick.bCanEverTick = false;
}

void USoundFeedback::BeginPlay() {
	Super::BeginPlay();

	LoadSound(ESoundType::Confirm, TEXT("/Game/Sounds/confirm.confirm"));
	LoadSound(ESoundType::Reject, TEXT("/Game/Sounds/reject.reject"));
	LoadSound(ESoundType::Click, TEXT("/Game/Sounds/click.click"));
	LoadSound(ESoundType::Warning, TEXT("/Game/Sounds/warning.warning"));
	LoadSound(ESoundType::Transition, TEXT("/Game/Sounds/positive.positive"));
}

void USoundFeedback::Play(ESoundType Type) {
	if (bMuted_) return;
	if (USoundBase** Found = Sounds_.Find(Type)) {
		if (*Found) {
			UGameplayStatics::PlaySound2D(GetWorld(), *Found);
		}
	}
}

void USoundFeedback::PlayAtLocation(ESoundType Type, FVector Location) {
	if (bMuted_) return;
	if (USoundBase** Found = Sounds_.Find(Type)) {
		if (*Found) {
			UGameplayStatics::PlaySoundAtLocation(GetWorld(), *Found, Location);
		}
	}
}

void USoundFeedback::SetMuted(bool bMute) {
	bMuted_ = bMute;
}

void USoundFeedback::LoadSound(ESoundType Type, const TCHAR* Path) {
	USoundBase* Sound = Cast<USoundBase>(StaticLoadObject(USoundBase::StaticClass(), nullptr, Path));
	if (Sound) {
		Sounds_.Add(Type, Sound);
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("SoundFeedback: failed to load sound for type %d from %s"),
			static_cast<int>(Type), Path);
	}
}
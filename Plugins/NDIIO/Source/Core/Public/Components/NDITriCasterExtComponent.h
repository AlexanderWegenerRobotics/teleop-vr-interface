/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <CoreMinimal.h>
#include <UObject/UnrealType.h>
#include <Objects/Media/NDIMediaSender.h>
#include <Components/ActorComponent.h>

#include "NDITriCasterExtComponent.generated.h"


USTRUCT(BlueprintType, Blueprintable, Category = "NDI IO", META = (DisplayName = "NDI TricasterExt"))
struct NDIIO_API FTriCasterExt
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="TricasterExt")
	FString Value;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="TricasterExt")
	TMap<FName,FString> KeyValues;
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FNDIEventDelegate_OnTriCasterExt, AActor*, Actor, UObject*, Object, FString, PropertyElementName, FString, PropertyValueStr, FTimespan, EasingDuration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIEventDelegate_OnTriCasterExtCustom, const FTriCasterExt&, TCData);


UCLASS(BlueprintType, Blueprintable, Category = "NDI IO",
	   META = (DisplayName = "NDI TricasterExt Component", BlueprintSpawnableComponent))
class UTriCasterExtComponent : public UActorComponent
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Enable TricasterExt", AllowPrivateAccess = true), Category="TricasterExt")
	bool EnableTriCasterExt = true;

	UPROPERTY(BlueprintReadWrite, EditInstanceOnly, Category = "NDI IO", META = (DisplayName = "NDI Media Source", AllowPrivateAccess = true))
	UNDIMediaSender* NDIMediaSource = nullptr;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="NDI Events", META = (DisplayName = "On TricasterExt", AllowPrivateAccess = true))
	FNDIEventDelegate_OnTriCasterExt OnTriCasterExt;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="NDI Events", META = (DisplayName = "On TricasterExt Custom", AllowPrivateAccess = true))
	FNDIEventDelegate_OnTriCasterExtCustom OnTriCasterExtCustom;

public:
	/** Call with the TriCasterExt metadata received from an NDI media sender */
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Receive Metadata From Sender"))
	void ReceiveMetaDataFromSender(UNDIMediaSender* Sender, FString Data);

public:
	UTriCasterExtComponent();
	virtual ~UTriCasterExtComponent();

	/**
		Initialize this component with the required media source to receive metadata from.
		Returns false, if the MediaSource is already been set. This is usually the case when this component is
		initialized in Blueprints.
	*/
	bool Initialize(UNDIMediaSender* InMediaSource = nullptr);

	void TriCasterExt(AActor* Actor, UObject* Object, FProperty* Property, FString PropertyElementName, FString PropertyValueStr, FTimespan EasingDuration);
	void TriCasterExtCustom(const FTriCasterExt& TCData);

protected:
	virtual void InitializeComponent() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	TSharedPtr<class NDIXmlParser> NDIMetadataParser;

	struct FTriCasterExtInterp
	{
		AActor* Actor;
		UObject* Object;
		FProperty* Property;
		FString PropertyElementName;
		FString PropertyValueStr;
		float EasingDuration;

		float EasingRemaining;
	};
	TArray<FTriCasterExtInterp> TriCasterExtInterp;
};

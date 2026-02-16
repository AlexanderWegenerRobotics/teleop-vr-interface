/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <CoreMinimal.h>
#include <GameFramework/Actor.h>
#include <Components/NDIViewportCaptureComponent.h>
#include <Components/NDIPTZControllerComponent.h>

#include "NDIBroadcastActor.generated.h"

/**
	A quick and easy way to capture the from the perspective of a camera that starts broadcasting the viewport
	immediate upon 'BeginPlay'
*/
UCLASS(BlueprintType, Blueprintable, Category = "NDI IO", META = (DisplayName = "NDI Broadcast Actor"))
class NDIIO_API ANDIBroadcastActor : public AActor, public IPTZControllableInterface
{
	GENERATED_UCLASS_BODY()

private:
	/**
		The NDI Media Sender representing the configuration of the network source to send audio, video, and metadata
	*/
	UPROPERTY(BlueprintReadWrite, EditInstanceOnly, Category = "NDI IO",
			  META = (DisplayName = "NDI Media Source", AllowPrivateAccess = true))
	UNDIMediaSender* NDIMediaSource = nullptr;

	/**
		A component used to capture an additional viewport for broadcasting over NDI
	*/
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "NDI IO",
			  META = (DisplayName = "Viewport Capture Component", AllowPrivateAccess = true))
	UNDIViewportCaptureComponent* ViewportCaptureComponent = nullptr;

	/**
		Component used for PTZ control
	*/
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "NDI IO",
			  META = (DisplayName = "PTZ Controller", AllowPrivateAccess = true))
	UPTZController* PTZController = nullptr;

public:
	virtual void BeginPlay() override;

	// IPTZControllableInterface
	virtual FPTZState GetPTZStateFromUE() const override;
	virtual void SetPTZStateToUE(const FPTZState& PTZState) override;
};

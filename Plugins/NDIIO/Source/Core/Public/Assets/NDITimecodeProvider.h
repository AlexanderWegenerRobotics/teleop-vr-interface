/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <GenlockedTimecodeProvider.h>

#include <Objects/Media/NDIMediaReceiver.h>

#include "NDITimecodeProvider.generated.h"


/**
	Timecode provider from an NDI source
*/
UCLASS(Blueprintable, editinlinenew, meta=(DisplayName="NDI Timecode Provider"))
class NDIIO_API UNDITimecodeProvider : public UGenlockedTimecodeProvider
{
	GENERATED_UCLASS_BODY()

private:
	/** The Receiver object used to get timecodes from */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "NDI IO",
			  META = (DisplayName = "NDI Media Source", AllowPrivateAccess = true))
	UNDIMediaReceiver* NDIMediaSource = nullptr;

public:
	//~ UTimecodeProvider interface
	virtual bool FetchTimecode(FQualifiedFrameTime& OutFrameTime) override;
	virtual ETimecodeProviderSynchronizationState GetSynchronizationState() const override;
	virtual bool Initialize(class UEngine* InEngine) override;
	virtual void Shutdown(class UEngine* InEngine) override;

	//~ UObject interface
	virtual void BeginDestroy() override;

private:
	void ReleaseResources();

private:
	FDelegateHandle VideoCaptureEventHandle;
	FDelegateHandle ConnectedEventHandle;
	FDelegateHandle DisconnectedEventHandle;

	mutable FCriticalSection StateSyncContext;
	ETimecodeProviderSynchronizationState State = ETimecodeProviderSynchronizationState::Closed;
	FQualifiedFrameTime MostRecentFrameTime;
};

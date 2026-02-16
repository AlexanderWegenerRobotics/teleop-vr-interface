/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include <Assets/NDITimecodeProvider.h>


UNDITimecodeProvider::UNDITimecodeProvider(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

bool UNDITimecodeProvider::FetchTimecode(FQualifiedFrameTime& OutFrameTime)
{
	FScopeLock Lock(&this->StateSyncContext);

	if (!IsValid(this->NDIMediaSource) ||
	    (GetSynchronizationState() != ETimecodeProviderSynchronizationState::Synchronized))
	{
		return false;
	}

	OutFrameTime = this->MostRecentFrameTime;

	return true;
}

ETimecodeProviderSynchronizationState UNDITimecodeProvider::GetSynchronizationState() const
{
	FScopeLock Lock(&this->StateSyncContext);

	if (!IsValid(this->NDIMediaSource))
		return ETimecodeProviderSynchronizationState::Closed;

	return this->State;
}

bool UNDITimecodeProvider::Initialize(UEngine* InEngine)
{
	this->State = ETimecodeProviderSynchronizationState::Closed;

	if (!IsValid(this->NDIMediaSource))
	{
		this->State = ETimecodeProviderSynchronizationState::Error;
		return false;
	}

	this->NDIMediaSource->Initialize(UNDIMediaReceiver::EUsage::Standalone);

	this->VideoCaptureEventHandle = this->NDIMediaSource->OnNDIReceiverVideoCaptureEvent.AddLambda([this](UNDIMediaReceiver* Receiver, const NDIlib_video_frame_v2_t& VideoFrame)
	{
		const FFrameRate Rate = Receiver->GetCurrentFrameRate();
		const FTimecode Timecode = Receiver->GetCurrentTimecode();

		FScopeLock Lock(&this->StateSyncContext);
		this->State = ETimecodeProviderSynchronizationState::Synchronized;
		this->MostRecentFrameTime = FQualifiedFrameTime(Timecode, Rate);
	});
	this->ConnectedEventHandle = this->NDIMediaSource->OnNDIReceiverConnectedEvent.AddLambda([this](UNDIMediaReceiver* Receiver)
	{
		FScopeLock Lock(&this->StateSyncContext);
		this->State = ETimecodeProviderSynchronizationState::Synchronizing;
	});
	this->DisconnectedEventHandle = this->NDIMediaSource->OnNDIReceiverDisconnectedEvent.AddLambda([this](UNDIMediaReceiver* Receiver)
	{
		FScopeLock Lock(&this->StateSyncContext);
		this->State = ETimecodeProviderSynchronizationState::Closed;
	});

	return true;
}

void UNDITimecodeProvider::Shutdown(UEngine* InEngine)
{
	ReleaseResources();
}


void UNDITimecodeProvider::BeginDestroy()
{
	ReleaseResources();

	Super::BeginDestroy();
}

void UNDITimecodeProvider::ReleaseResources()
{
	if(IsValid(this->NDIMediaSource))
	{
		this->NDIMediaSource->OnNDIReceiverVideoCaptureEvent.Remove(this->VideoCaptureEventHandle);
		this->NDIMediaSource->OnNDIReceiverConnectedEvent.Remove(this->ConnectedEventHandle);
		this->NDIMediaSource->OnNDIReceiverDisconnectedEvent.Remove(this->DisconnectedEventHandle);
	}
	this->VideoCaptureEventHandle.Reset();
	this->ConnectedEventHandle.Reset();
	this->DisconnectedEventHandle.Reset();

	this->State = ETimecodeProviderSynchronizationState::Closed;
}

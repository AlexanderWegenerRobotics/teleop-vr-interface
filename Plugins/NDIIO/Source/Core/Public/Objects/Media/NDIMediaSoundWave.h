/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <CoreMinimal.h>
#include <Sound/SoundWaveProcedural.h>

#include "NDIMediaSoundWave.generated.h"

/**
	Defines a SoundWave object used by an NDI Media Receiver object for capturing audio from
	a network source
*/
UCLASS(NotBlueprintable, Category = "NDI IO", META = (DisplayName = "NDI Media Sound Wave"))
class NDIIO_API UNDIMediaSoundWave : public USoundWaveProcedural
{
	GENERATED_UCLASS_BODY()

public:
	/**
		Set the Media Source of this object, so that when this object is called to 'GeneratePCMData' by the engine
		we can request the media source to provide the pcm data from the current connected source
	*/
	void SetConnectionSource(class UNDIMediaReceiver* InMediaSource = nullptr);

protected:
	/**
		Called by the engine to generate pcm data to be 'heard' by audio listener objects
	*/
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override final;

	virtual bool IsReadyForFinishDestroy() override final;

private:
	FCriticalSection SyncContext;
	class UNDIMediaReceiver* MediaSource = nullptr;
};

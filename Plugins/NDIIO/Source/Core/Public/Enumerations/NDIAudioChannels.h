/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <CoreMinimal.h>

#include "NDIAudioChannels.generated.h"

/**
	Receiver Bandwidth modes
*/
UENUM(BlueprintType, META = (DisplayName = "NDI Audio Channels"))
enum class ENDIAudioChannels : uint8
{
	/** Mono. */
	Mono = 0x00 UMETA(DisplayName = "Mono"),

	/** Stereo. */
	Stereo = 0x01 UMETA(DisplayName = "Stereo"),

	/** Whatever the number of channels in the source is. */
	Source = 0x02 UMETA(DisplayName = "Source"),
};
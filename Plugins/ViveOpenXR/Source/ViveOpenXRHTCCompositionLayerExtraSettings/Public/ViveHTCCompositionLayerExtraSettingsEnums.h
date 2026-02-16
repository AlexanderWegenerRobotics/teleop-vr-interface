// Copyright HTC Corporation. All Rights Reserved.

#pragma once
#include <stdint.h>
#include "ViveHTCCompositionLayerExtraSettingsEnums.generated.h"

UENUM(BlueprintType, Category = "ViveOpenXR|HTCCompositionLayerExtraSettings")
enum class ESharpeningMode : uint8 {
	FAST = 0,
	NORMAL = 1,
	QUALITY = 2,
	AUTOMATIC = 3,
};
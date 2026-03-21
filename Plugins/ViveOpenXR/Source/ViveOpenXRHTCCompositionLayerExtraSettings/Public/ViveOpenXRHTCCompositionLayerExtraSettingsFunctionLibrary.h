// Copyright HTC Corporation. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ViveHTCCompositionLayerExtraSettingsEnums.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ViveOpenXRHTCCompositionLayerExtraSettingsFunctionLibrary.generated.h"

/**
 * Blueprint function library for ViveOpenXR HTCCompositionLayerExtraSettings extension.
 */

UCLASS(ClassGroup = OpenXR)
class VIVEOPENXRHTCCOMPOSITIONLAYEREXTRASETTINGS_API UViveOpenXRHTCCompositionLayerExtraSettingsFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Sharpening Mode", Keywords = "ViveOpenXR HTCCompositionLayerExtraSettings Sharpening"), Category = "ViveOpenXR|HTCCompositionLayerExtraSettings")
	static bool SetSharpeningMode(ESharpeningMode Mode);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Sharpening Level", Keywords = "ViveOpenXR HTCCompositionLayerExtraSettings Sharpening", ClampMin = "0.0", ClampMax = "1.0"), Category = "ViveOpenXR|HTCCompositionLayerExtraSettings")
	static bool SetSharpeningLevel(float Level);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Project Settings Sharpening Mode", Keywords = "ViveOpenXR HTCCompositionLayerExtraSettings Sharpening"), Category = "ViveOpenXR|HTCCompositionLayerExtraSettings")
	static ESharpeningMode GetProjectSettingsSharpeningMode();

	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Project Settings Sharpening Level", Keywords = "ViveOpenXR HTCCompositionLayerExtraSettings Sharpening"), Category = "ViveOpenXR|HTCCompositionLayerExtraSettings")
	static float GetProjectSettingsSharpeningLevel();
};

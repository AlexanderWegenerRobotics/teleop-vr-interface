// Copyright HTC Corporation. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ViveOpenXRPassthrough.h"
#include "VivePassthroughEnums.h"
#include "ViveOpenXRPassthroughFunctionLibrary.generated.h"


UCLASS(ClassGroup = OpenXR)
class VIVEOPENXRPASSTHROUGH_API UViveOpenXRPassthroughFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Is Passthrough Enabled", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
    static bool IsPassthroughEnabled();

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Create Passthrough Underlay", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
	static FPassthroughHandle CreatePassthroughUnderlay(EXrPassthroughLayerForm inPassthroughLayerForm, bool AutoSwtich = true);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Switch Passthrough Underlay", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
	static bool SwitchPassthroughUnderlay(EXrPassthroughLayerForm inPassthroughLayerForm);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Destroy Passthrough Underlay", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
	static bool DestroyPassthroughUnderlay(FPassthroughHandle PassthroughHandle);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Passthrough Alpha", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
	static bool SetPassthroughAlpha(FPassthroughHandle PassthroughHandle, float alpha);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Passthrough Mesh", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
	static bool SetPassthroughMesh(FPassthroughHandle PassthroughHandle, const TArray<FVector>& vertices, const TArray<int32>& indices);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Passthrough Mesh Transform", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
	static bool SetPassthroughMeshTransform(FPassthroughHandle PassthroughHandle, EProjectedPassthroughSpaceType meshSpaceType, FTransform meshTransform);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Passthrough Mesh Transform Space", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
	static bool SetPassthroughMeshTransformSpace(FPassthroughHandle PassthroughHandle, EProjectedPassthroughSpaceType meshSpaceType);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Passthrough Mesh Transform Location", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
	static bool SetPassthroughMeshTransformLocation(FPassthroughHandle PassthroughHandle, FVector meshLocation);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Passthrough Mesh Transform Rotation", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
	static bool SetPassthroughMeshTransformRotation(FPassthroughHandle PassthroughHandle, FRotator meshRotation);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Passthrough Mesh Transform Scale", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
	static bool SetPassthroughMeshTransformScale(FPassthroughHandle PassthroughHandle, FVector meshScale);
	
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Passthrough Image Rate", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
	static ConfigurationRateType GetPassthroughImageRate();

	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Passthrough Image Quality", Keywords = "ViveOpenXR Passthrough"), Category = "ViveOpenXR|Passthrough")
	static float GetPassthroughImageQuality();

	static FViveOpenXRPassthrough* GetViveOpenXRPassthroughModulePtr();
};
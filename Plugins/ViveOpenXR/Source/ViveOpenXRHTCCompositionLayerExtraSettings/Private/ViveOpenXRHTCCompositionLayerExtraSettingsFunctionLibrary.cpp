// Copyright HTC Corporation. All Rights Reserved.


#include "ViveOpenXRHTCCompositionLayerExtraSettingsFunctionLibrary.h"
#include "ViveOpenXRHTCCompositionLayerExtraSettingsModule.h"
#include "OpenXRHMD.h"

static FViveOpenXRHTCCompositionLayerExtraSettings* FViveOpenXRHTCCompositionLayerExtraSettingsPtr = nullptr;

FViveOpenXRHTCCompositionLayerExtraSettings* GetViveOpenXRHTCCompositionLayerExtraSettingsModulePtr()
{
	if (FViveOpenXRHTCCompositionLayerExtraSettingsPtr != nullptr)
	{
		return FViveOpenXRHTCCompositionLayerExtraSettingsPtr;
	}
	else
	{
		if (GEngine->XRSystem.IsValid())
		{
			auto HMD = static_cast<FOpenXRHMD*>(GEngine->XRSystem->GetHMDDevice());
			for (IOpenXRExtensionPlugin* Module : HMD->GetExtensionPlugins())
			{
				if (Module->GetDisplayName() == TEXT("ViveOpenXRHTCCompositionLayerExtraSettings"))
				{
					FViveOpenXRHTCCompositionLayerExtraSettingsPtr = static_cast<FViveOpenXRHTCCompositionLayerExtraSettings*>(Module);
					break;
				}
			}
		}
		return FViveOpenXRHTCCompositionLayerExtraSettingsPtr;
	}
}

bool UViveOpenXRHTCCompositionLayerExtraSettingsFunctionLibrary::SetSharpeningMode(ESharpeningMode Mode)
{
	if (!GetViveOpenXRHTCCompositionLayerExtraSettingsModulePtr())
	{
		UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Warning, TEXT("SetSharpeningMode false"));
		return false;
	}
	UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("SetSharpeningMode true"));

	return GetViveOpenXRHTCCompositionLayerExtraSettingsModulePtr()->CompositionLayerSetSharpeningMode(Mode);
}

bool UViveOpenXRHTCCompositionLayerExtraSettingsFunctionLibrary::SetSharpeningLevel(float Level)
{
	if (!GetViveOpenXRHTCCompositionLayerExtraSettingsModulePtr())
	{
		UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Warning, TEXT("SetSharpeningLevel false"));
		return false;
	}
	UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("SetSharpeningLevel true"));

	return GetViveOpenXRHTCCompositionLayerExtraSettingsModulePtr()->CompositionLayerSetSharpeningLevel(Level);
}

ESharpeningMode UViveOpenXRHTCCompositionLayerExtraSettingsFunctionLibrary::GetProjectSettingsSharpeningMode()
{
	if (!GetViveOpenXRHTCCompositionLayerExtraSettingsModulePtr())
	{
		UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Warning, TEXT("GetProjectSettingsSharpeningMode failed"));
		return ESharpeningMode::NORMAL;
	}
	UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("GetProjectSettingsSharpeningMode Success"));

	return GetViveOpenXRHTCCompositionLayerExtraSettingsModulePtr()->GetProjectSettingsSharpeningMode();
}

float UViveOpenXRHTCCompositionLayerExtraSettingsFunctionLibrary::GetProjectSettingsSharpeningLevel()
{
	if (!GetViveOpenXRHTCCompositionLayerExtraSettingsModulePtr())
	{
		UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Warning, TEXT("GetProjectSettingsSharpeningLevel failed"));
		return 0;
	}
	UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("GetProjectSettingsSharpeningLevel Success"));

	return GetViveOpenXRHTCCompositionLayerExtraSettingsModulePtr()->GetProjectSettingsSharpeningLevel();
}
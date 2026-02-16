// Copyright HTC Corporation. All Rights Reserved.

#include "ViveOpenXRHTCCompositionLayerExtraSettingsModule.h"
#include "OpenXRCore.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogViveOpenXRHTCCompositionLayerExtraSettings);

void FViveOpenXRHTCCompositionLayerExtraSettings::StartupModule()
{
	check(GConfig && GConfig->IsReadyForUse());
	FString modeName;
	if (GConfig->GetString(TEXT("/Script/ViveOpenXRRuntimeSettings.ViveOpenXRRuntimeSettings"), TEXT("bEnableSharpening"), modeName, GEngineIni))
	{
		if (modeName.Equals("False"))
		{
			m_bEnableHTCCompositionLayerExtraSettings = false;
		}
		else if (modeName.Equals("True"))
		{
			m_bEnableHTCCompositionLayerExtraSettings = true;
		}
	}

	float SharpeningLevel;
	if (GConfig->GetFloat(TEXT("/Script/ViveOpenXRRuntimeSettings.ViveOpenXRRuntimeSettings"), TEXT("SharpeningLevel"), SharpeningLevel, GEngineIni))
	{
		ProjectSettingsSharpeningLevel = SharpeningLevel;
	}

	FString SharpeningModeName;
	if (GConfig->GetString(TEXT("/Script/ViveOpenXRRuntimeSettings.ViveOpenXRRuntimeSettings"), TEXT("SharpeningMode"), SharpeningModeName, GEngineIni))
	{
		if (SharpeningModeName == "FAST")
		{
			ProjectSettingsSharpeningMode = ESharpeningMode::FAST;
		}
		else if (SharpeningModeName == "QUALITY")
		{
			ProjectSettingsSharpeningMode = ESharpeningMode::QUALITY;
		}
		else if (SharpeningModeName == "AUTOMATIC")
		{
			ProjectSettingsSharpeningMode = ESharpeningMode::AUTOMATIC;
		}
		else
		{
			ProjectSettingsSharpeningMode = ESharpeningMode::NORMAL;
		}
	}

	if (m_bEnableHTCCompositionLayerExtraSettings)
	{
		UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("Enable HTCCompositionLayerExtraSettings."));
	}
	else
	{
		UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("Disable HTCCompositionLayerExtraSettings."));
		return;
	}

	RegisterOpenXRExtensionModularFeature();
	UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("StartupModule() Finished."));
}

void FViveOpenXRHTCCompositionLayerExtraSettings::PostCreateInstance(XrInstance InInstance)
{
	if (!m_bEnableHTCCompositionLayerExtraSettings) return;
	UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("Entry HTCCompositionLayerExtraSettings PostCreateInstance."));

	m_XrInstance = InInstance;
}

bool FViveOpenXRHTCCompositionLayerExtraSettings::GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	if (m_bEnableHTCCompositionLayerExtraSettings)
	{
		UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("GetRequiredExtensions() Add HTCCompositionLayerExtraSettings Extension Name %s."), ANSI_TO_TCHAR(XR_HTC_COMPOSITION_LAYER_EXTRA_SETTINGS_EXTENSION_NAME));
		OutExtensions.Add(XR_HTC_COMPOSITION_LAYER_EXTRA_SETTINGS_EXTENSION_NAME);
	}
	return true;
}

void FViveOpenXRHTCCompositionLayerExtraSettings::PostGetSystem(XrInstance InInstance, XrSystemId InSystem)
{
	if (!m_bEnableHTCCompositionLayerExtraSettings) return;
	m_XrSystemId = InSystem;
	XR_ENSURE(xrGetInstanceProcAddr(InInstance, "xrGetSystemProperties", (PFN_xrVoidFunction*)&FHTCCompositionLayerExtraSettings_ext.xrGetSystemProperties));

	XrSystemCompositionLayerExtraSettingsPropertiesHTC CompositionLayerExtraSettingsSystemProperties;
	CompositionLayerExtraSettingsSystemProperties.type = XR_TYPE_SYSTEM_COMPOSITION_LAYER_EXTRA_SETTINGS_PROPERTIES_HTC;
	CompositionLayerExtraSettingsSystemProperties.next = NULL;

	XrSystemProperties systemProperties;
	systemProperties.type = XrStructureType::XR_TYPE_SYSTEM_PROPERTIES;
	systemProperties.next = &CompositionLayerExtraSettingsSystemProperties;

	XR_ENSURE(FHTCCompositionLayerExtraSettings_ext.xrGetSystemProperties(InInstance, InSystem, &systemProperties));
	{
		if (CompositionLayerExtraSettingsSystemProperties.supportsSharpening == 0) {
			UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("Entry HTCCompositionLayerExtraSettings sharpening unsupported."));
			m_supportsSharpening = false;
		}
		else
		{
			UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("Entry HTCCompositionLayerExtraSettings support sharpening."));
			m_supportsSharpening = true;
		}
	}
}

void FViveOpenXRHTCCompositionLayerExtraSettings::PostCreateSession(XrSession InSession)
{
	if (!m_bEnableHTCCompositionLayerExtraSettings) return;
	UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("Entry HTCCompositionLayerExtraSettings PostCreateSession."));

	m_Session = InSession;
}

const void* FViveOpenXRHTCCompositionLayerExtraSettings::OnBeginSession(XrSession InSession, const void* InNext)
{
	UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("Entry HTCCompositionLayerExtraSettings OnBeginSession."));
	if (!m_bEnableHTCCompositionLayerExtraSettings || !m_supportsSharpening) return InNext;

	if (!m_bSharpeningInitialization)
	{
		m_bSharpeningInitialization = true;
		XrCompositionLayerSharpeningSettingHTC* newCompositionLayerSharpeningSettingHTC = new XrCompositionLayerSharpeningSettingHTC;
		newCompositionLayerSharpeningSettingHTC->type = XR_TYPE_COMPOSITION_LAYER_SHARPENING_SETTING_HTC;
		newCompositionLayerSharpeningSettingHTC->next = nullptr;
		newCompositionLayerSharpeningSettingHTC->sharpeningLevel = ProjectSettingsSharpeningLevel;
		switch (ProjectSettingsSharpeningMode)
		{
		case ESharpeningMode::FAST:
			newCompositionLayerSharpeningSettingHTC->mode = XrSharpeningModeHTC::XR_SHARPENING_MODE_FAST_HTC;
			break;
		case ESharpeningMode::NORMAL:
			newCompositionLayerSharpeningSettingHTC->mode = XrSharpeningModeHTC::XR_SHARPENING_MODE_NORMAL_HTC;
			break;
		case ESharpeningMode::QUALITY:
			newCompositionLayerSharpeningSettingHTC->mode = XrSharpeningModeHTC::XR_SHARPENING_MODE_QUALITY_HTC;
			break;
		case ESharpeningMode::AUTOMATIC:
			newCompositionLayerSharpeningSettingHTC->mode = XrSharpeningModeHTC::XR_SHARPENING_MODE_AUTOMATIC_HTC;
			break;
		}

		XrCompositionLayerSharpeningSettingHTCPtr = newCompositionLayerSharpeningSettingHTC;
	}
	
	UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("HTCCompositionLayerExtraSettings OnBeginSession End"));

	return InNext;
}

const void* FViveOpenXRHTCCompositionLayerExtraSettings::OnEndProjectionLayer(XrSession InSession, int32 InLayerIndex, const void* InNext, XrCompositionLayerFlags& OutFlags)
{
	if (!m_bEnableHTCCompositionLayerExtraSettings || !XrCompositionLayerSharpeningSettingHTCPtr) return InNext;
	XrCompositionLayerSharpeningSettingHTCPtr->next = InNext;
	return reinterpret_cast<const void*>(XrCompositionLayerSharpeningSettingHTCPtr);
}

void FViveOpenXRHTCCompositionLayerExtraSettings::OnDestroySession(XrSession InSession)
{
	if (!m_bEnableHTCCompositionLayerExtraSettings) return;
	UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("Entry HTCCompositionLayerExtraSettings OnDestroySession."));

	delete XrCompositionLayerSharpeningSettingHTCPtr;
	XrCompositionLayerSharpeningSettingHTCPtr = nullptr;

	m_supportsSharpening = false;

	m_XrInstance = XR_NULL_HANDLE;
	m_Session = XR_NULL_HANDLE;
	m_XrSystemId = XR_NULL_SYSTEM_ID;
}

ESharpeningMode FViveOpenXRHTCCompositionLayerExtraSettings::GetProjectSettingsSharpeningMode()
{
	return ProjectSettingsSharpeningMode;
}

float FViveOpenXRHTCCompositionLayerExtraSettings::GetProjectSettingsSharpeningLevel()
{
	return ProjectSettingsSharpeningLevel;
}

bool FViveOpenXRHTCCompositionLayerExtraSettings::CompositionLayerSetSharpeningMode(ESharpeningMode Mode)
{
	if (!m_bEnableHTCCompositionLayerExtraSettings) return false;
	UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("HTCCompositionLayerExtraSettings CompositionLayerSetSharpeningMode."));

	if (XrCompositionLayerSharpeningSettingHTCPtr)
	{
		switch (Mode)
		{
		case ESharpeningMode::FAST:
			XrCompositionLayerSharpeningSettingHTCPtr->mode = XrSharpeningModeHTC::XR_SHARPENING_MODE_FAST_HTC;
			break;
		case ESharpeningMode::NORMAL:
			XrCompositionLayerSharpeningSettingHTCPtr->mode = XrSharpeningModeHTC::XR_SHARPENING_MODE_NORMAL_HTC;
			break;
		case ESharpeningMode::QUALITY:
			XrCompositionLayerSharpeningSettingHTCPtr->mode = XrSharpeningModeHTC::XR_SHARPENING_MODE_QUALITY_HTC;
			break;
		case ESharpeningMode::AUTOMATIC:
			XrCompositionLayerSharpeningSettingHTCPtr->mode = XrSharpeningModeHTC::XR_SHARPENING_MODE_AUTOMATIC_HTC;
			break;
		}
		UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("Set XrCompositionLayerSharpeningSettingHTC Sharpening Mode"));
		return true;
	}
	else
	{
		UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Warning, TEXT("XrCompositionLayerSharpeningSettingHTC is null pointer"));
		return false;
	}
}

bool FViveOpenXRHTCCompositionLayerExtraSettings::CompositionLayerSetSharpeningLevel(float Level)
{
	if (!m_bEnableHTCCompositionLayerExtraSettings) return false;
	UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("HTCCompositionLayerExtraSettings CompositionLayerSetSharpeningLevel."));

	if (XrCompositionLayerSharpeningSettingHTCPtr)
	{
		XrCompositionLayerSharpeningSettingHTCPtr->sharpeningLevel = Level;
		UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, TEXT("Set XrCompositionLayerSharpeningSettingHTC Sharpening Level %f"), Level);
		return true;
	}
	else
	{
		UE_LOG(LogViveOpenXRHTCCompositionLayerExtraSettings, Warning, TEXT("XrCompositionLayerSharpeningSettingHTC is null pointer"));
		return false;
	}
}

IMPLEMENT_MODULE(FViveOpenXRHTCCompositionLayerExtraSettings, ViveOpenXRHTCCompositionLayerExtraSettings)

// Copyright HTC Corporation. All Rights Reserved.
#pragma once

#include "OpenXRCommon.h"
#include "ViveHTCCompositionLayerExtraSettingsEnums.h"

DECLARE_LOG_CATEGORY_EXTERN(LogViveOpenXRHTCCompositionLayerExtraSettings, Log, All);

struct FHTCCompositionLayerExtraSettingsExtesionDispatchTable
{
	PFN_xrGetSystemProperties xrGetSystemProperties;
};

class FViveOpenXRHTCCompositionLayerExtraSettings : public IModuleInterface, public IOpenXRExtensionPlugin
{
public:
	FViveOpenXRHTCCompositionLayerExtraSettings(){}
	virtual ~FViveOpenXRHTCCompositionLayerExtraSettings(){}

	/************************************************************************/
	/* IModuleInterface                                                     */
	/************************************************************************/
	virtual void StartupModule() override;
	virtual void ShutdownModule() override
	{
		UnregisterOpenXRExtensionModularFeature();
	}

	virtual FString GetDisplayName() override
	{
		return FString(TEXT("ViveOpenXRHTCCompositionLayerExtraSettings"));
	}

	/** IOpenXRExtensionPlugin implementation */
	virtual void PostCreateInstance(XrInstance InInstance) override;
	virtual bool GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
	virtual void PostGetSystem(XrInstance InInstance, XrSystemId InSystem) override;
	virtual void PostCreateSession(XrSession InSession) override;
	virtual const void* OnBeginSession(XrSession InSession, const void* InNext) override;
	virtual const void* OnEndProjectionLayer(XrSession InSession, int32 InLayerIndex, const void* InNext, XrCompositionLayerFlags& OutFlags) override;
	virtual void OnDestroySession(XrSession InSession) override;

	bool CompositionLayerSetSharpeningMode(ESharpeningMode Mode);
	bool CompositionLayerSetSharpeningLevel(float Level);
	
	ESharpeningMode GetProjectSettingsSharpeningMode();
	float GetProjectSettingsSharpeningLevel();

public:

private:
	bool m_bEnableHTCCompositionLayerExtraSettings = false;
	bool m_bSharpeningInitialization = false;
	ESharpeningMode ProjectSettingsSharpeningMode = ESharpeningMode::NORMAL;
	float ProjectSettingsSharpeningLevel = 0.1f;

	XrInstance m_XrInstance = XR_NULL_HANDLE;
	XrSession m_Session = XR_NULL_HANDLE;
	XrSystemId m_XrSystemId = XR_NULL_SYSTEM_ID;

	bool m_supportsSharpening = false;

	FHTCCompositionLayerExtraSettingsExtesionDispatchTable FHTCCompositionLayerExtraSettings_ext{};
	XrCompositionLayerSharpeningSettingHTC* XrCompositionLayerSharpeningSettingHTCPtr = nullptr;
};


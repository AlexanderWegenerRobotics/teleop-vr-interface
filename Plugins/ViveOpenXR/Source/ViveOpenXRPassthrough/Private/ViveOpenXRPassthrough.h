// Copyright HTC Corporation. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "OpenXRCommon.h"
#include "PassthroughHandle.h"
#include "PassthroughConfigurationAsyncAction.h"

DECLARE_LOG_CATEGORY_EXTERN(LogViveOpenXRPassthrough, Log, All);

struct FPassthroughDispatchTable_ext
{
	PFN_xrGetSystemProperties xrGetSystemProperties;
};

struct PassthroughData
{
	// OpenXR handle for passthrough
	XrPassthroughHTC Handle;

	// Pointer to composition layer info, transient as it's runtime-only
	XrCompositionLayerPassthroughHTC* projectedPassthroughCompositionLayerInfoPtr;

	// Pointer to mesh transform info, transient as it's runtime-only
	XrPassthroughMeshTransformInfoHTC* passthroughMeshTransformInfoPtr;

	bool Valid;
};

class FViveOpenXRPassthrough : public IModuleInterface, public IOpenXRExtensionPlugin
{
public:
	FViveOpenXRPassthrough(){}
	virtual ~FViveOpenXRPassthrough(){}

	/************************************************************************/
	/* IModuleInterface                                                     */
	/************************************************************************/
	virtual void StartupModule() override;
	virtual void ShutdownModule() override
	{
		UnregisterOpenXRExtensionModularFeature();
	}

	/** IOpenXRExtensionPlugin */
	virtual FString GetDisplayName() override
	{
		return FString(TEXT("ViveOpenXRPassthrough"));
	}

	/** IOpenXRExtensionPlugin implementation */
	virtual const void* OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext) override;
	virtual const void* OnBeginSession(XrSession InSession, const void* InNext) override;
	virtual bool GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
	virtual void PostGetSystem(XrInstance InInstance, XrSystemId InSystem) override;
	virtual bool GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
	virtual void PostCreateSession(XrSession InSession) override;
	virtual void UpdateDeviceLocations(XrSession InSession, XrTime DisplayTime, XrSpace TrackingSpace) override;
	virtual void UpdateCompositionLayers(XrSession InSession, TArray<const XrCompositionLayerBaseHeader*>& Headers) override;
	virtual void OnDestroySession(XrSession InSession) override;
	virtual const void* OnEndProjectionLayer(XrSession InSession, int32 InLayerIndex, const void* InNext, XrCompositionLayerFlags& OutFlags) override;
	virtual void OnEvent(XrSession InSession, const XrEventDataBaseHeader* InHeader) override;

	//Passthrough Underlay
	PassthroughData CreatePassthrough(XrPassthroughFormHTC layerForm);
	bool SwitchPassthrough(XrPassthroughFormHTC layerForm);
	bool DestroyPassthrough(XrPassthroughHTC PassthroughHandle);

	//PassthroughConfigurations
	float GetPassthroughQuality();
	bool SetPassthroughQuality(float QualityScale);
	int32 GetPassthroughRate();
	bool SetPassthroughRate(int32 Level);

	//For projected passthrough
	bool SetPassthroughAlpha(XrPassthroughHTC PassthroughHandle, float alpha);
	bool SetPassthroughMesh(XrPassthroughHTC PassthroughHandle, uint32_t inVertexCount, const XrVector3f* inVertexBuffer, uint32_t inIndexCount, const uint32_t* inIndexBuffer);
	bool SetPassthroughMeshTransform(XrPassthroughHTC PassthroughHandle, XrSpace meshSpace, XrPosef meshPose, XrVector3f meshScale);
	bool SetPassthroughMeshTransformSpace(XrPassthroughHTC PassthroughHandle, XrSpace meshSpace);
	bool SetPassthroughMeshTransformPosition(XrPassthroughHTC PassthroughHandle, XrVector3f meshPosition);
	bool SetPassthroughMeshTransformOrientation(XrPassthroughHTC PassthroughHandle, XrQuaternionf meshOrientation);
	bool SetPassthroughMeshTransformScale(XrPassthroughHTC PassthroughHandle, XrVector3f meshScale);

	XrSpace GetHeadlockXrSpace();
	XrSpace GetTrackingXrSpace();

	//Sync Event
	FPassthroughConfigurationQualityChangeCompleted QualityChangeSuccessEvent;
	FPassthroughConfigurationQualityChangeFailed QualityChangeFailureEvent;
	FPassthroughConfigurationRateChangeCompleted RateChangeSuccessEvent;
	FPassthroughConfigurationRateChangeFailed RateChangeFailureEvent;

public:
	bool m_bPassthroughConfigurationInitialization = false;
	bool m_bEnablePassthrough = false;
	bool m_supportsQualityConfiguration = false;
	bool m_supportsRateConfiguration = false;

	float ProjectSettingsQualityScale = 0.1f;
	float FromQualityScale = 0.1f;
	int32 ProjectSettingsRateType = 0;
	int32 SetRateType = 0;

private:
	XrInstance m_XrInstance = XR_NULL_HANDLE;
	XrSystemId m_XrSystemId = XR_NULL_SYSTEM_ID;
	XrSession m_Session = XR_NULL_HANDLE;

	XrSpace m_HeadLockSpace = XR_NULL_HANDLE;
	XrSpace m_BaseSpace = XR_NULL_HANDLE;

	XrPassthroughFormHTC m_CurrentLayerForm = XrPassthroughFormHTC::XR_PASSTHROUGH_FORM_MAX_ENUM_HTC;
	bool isPlanerPassthroughIsValid = false;
	bool isPlanerPassthroughCreated = false;
	bool isProjectedPassthroughCreated = false;
	//Passthrough configuration
	TArray<XrPassthroughConfigurationImageRateHTC> imageRates;

	//Projected passthrough handles
	TMap<XrPassthroughHTC, PassthroughData*> ProjectedPassthroughMap;
	PassthroughData ProjectedPassthroughDataHandle{ XR_NULL_HANDLE, nullptr, nullptr, false };

	//Planer passthrough handles
	XrPassthroughHTC planerPassthroughHandle = XR_NULL_HANDLE;
	PassthroughData PlanerPassthroughDataHandle{ XR_NULL_HANDLE, nullptr, nullptr, false };
	XrCompositionLayerPassthroughHTC* planerPassthroughCompositionLayerInfoPtr = nullptr;

	//OpenXR Function Ptrs
	PFN_xrCreatePassthroughHTC xrCreatePassthroughHTC = nullptr;
	PFN_xrDestroyPassthroughHTC xrDestroyPassthroughHTC = nullptr;
	PFN_xrEnumeratePassthroughImageRatesHTC xrEnumeratePassthroughImageRatesHTC = nullptr;
	PFN_xrGetPassthroughConfigurationHTC xrGetPassthroughConfigurationHTC = nullptr;
	PFN_xrSetPassthroughConfigurationHTC xrSetPassthroughConfigurationHTC = nullptr;

	XrPassthroughColorHTC passthroughColorInfo{};
	FPassthroughDispatchTable_ext FPassthrough_ext{};

};

static FViveOpenXRPassthrough* FViveOpenXRPassthroughPtr = nullptr;
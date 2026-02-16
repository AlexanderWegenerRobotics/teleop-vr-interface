// Copyright HTC Corporation. All Rights Reserved.

#include "ViveOpenXRPassthrough.h"
#include "PassthroughConfigurationAsyncAction.h"
#include "TimerManager.h"
#include "PassthroughHandle.h"
#include "HAL/Platform.h"

DEFINE_LOG_CATEGORY(LogViveOpenXRPassthrough);

struct rateType
{
	int32 Boost = 0;
	int32 Normal = 1;
};

void FViveOpenXRPassthrough::StartupModule()
{
	check(GConfig && GConfig->IsReadyForUse());
	FString modeName;
	if (GConfig->GetString(TEXT("/Script/ViveOpenXRRuntimeSettings.ViveOpenXRRuntimeSettings"), TEXT("bEnablePassthrough"), modeName, GEngineIni))
	{
		if (modeName.Equals("False"))
		{
			m_bEnablePassthrough = false;
		}
		else if (modeName.Equals("True"))
		{
			m_bEnablePassthrough = true;
		}
	}

	float QualityScale;
	if (GConfig->GetFloat(TEXT("/Script/ViveOpenXRRuntimeSettings.ViveOpenXRRuntimeSettings"), TEXT("PassthroughQualityScale"), QualityScale, GEngineIni))
	{
		ProjectSettingsQualityScale = QualityScale;
		FromQualityScale = QualityScale;
	}

	FString RateTypeName;
	if (GConfig->GetString(TEXT("/Script/ViveOpenXRRuntimeSettings.ViveOpenXRRuntimeSettings"), TEXT("PassthroughRateType"), RateTypeName, GEngineIni))
	{
		if (RateTypeName == "Normal")
		{
			ProjectSettingsRateType = 0;
			SetRateType = ProjectSettingsRateType;
		}
		else
		{
			ProjectSettingsRateType = 1;
			SetRateType = ProjectSettingsRateType;
		}
	}

	if (m_bEnablePassthrough)
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Enable Passthrough."));
	}
	else
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Disable Passthrough."));
		return;
	}

	RegisterOpenXRExtensionModularFeature();
	FViveOpenXRPassthroughPtr = this;
	UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("StartupModule() Finished."));
}

const void* FViveOpenXRPassthrough::OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext)
{
	if (!m_bEnablePassthrough) return InNext;

	ProjectedPassthroughMap.Empty();
	//UPassthroughSetQualityAsyncActionPtr = UPassthroughSetQualityAsyncAction::
	UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Entry Passthrough OnCreateSession."));
	XR_ENSURE(xrGetInstanceProcAddr(InInstance, "xrCreatePassthroughHTC", (PFN_xrVoidFunction*)&xrCreatePassthroughHTC));
	XR_ENSURE(xrGetInstanceProcAddr(InInstance, "xrDestroyPassthroughHTC", (PFN_xrVoidFunction*)&xrDestroyPassthroughHTC));

#if PLATFORM_ANDROID
	//Passthrough configuration
	XR_ENSURE(xrGetInstanceProcAddr(InInstance, "xrEnumeratePassthroughImageRatesHTC", (PFN_xrVoidFunction*)&xrEnumeratePassthroughImageRatesHTC));
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("xrEnumeratePassthroughImageRatesHTC get function pointer"));
	}
	XR_ENSURE(xrGetInstanceProcAddr(InInstance, "xrGetPassthroughConfigurationHTC", (PFN_xrVoidFunction*)&xrGetPassthroughConfigurationHTC));
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("xrGetPassthroughConfigurationHTC get function pointer"));
	}
	XR_ENSURE(xrGetInstanceProcAddr(InInstance, "xrSetPassthroughConfigurationHTC", (PFN_xrVoidFunction*)&xrSetPassthroughConfigurationHTC));
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("xrSetPassthroughConfigurationHTC get function pointer"));
	}
#endif

	return InNext;
}

const void* FViveOpenXRPassthrough::OnBeginSession(XrSession InSession, const void* InNext)
{
	if (!m_bEnablePassthrough) return InNext;
	
	UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Entry Passthrough OnBeginSession InSession %llu."), InSession);

	static FName SystemName(TEXT("OpenXR"));
	
	uint32_t ReferenceSpacesCount;
	XR_ENSURE(xrEnumerateReferenceSpaces(InSession, 0, &ReferenceSpacesCount, nullptr));

	TArray<XrReferenceSpaceType> Spaces;
	Spaces.SetNum(ReferenceSpacesCount);
	for (auto& SpaceIter : Spaces)
		SpaceIter = XR_REFERENCE_SPACE_TYPE_VIEW;
	XR_ENSURE(xrEnumerateReferenceSpaces(InSession, (uint32_t)Spaces.Num(), &ReferenceSpacesCount, Spaces.GetData()));
	ensure(ReferenceSpacesCount == Spaces.Num());

	XrSpace HmdSpace = XR_NULL_HANDLE;
	XrReferenceSpaceCreateInfo SpaceInfo;

	ensure(Spaces.Contains(XR_REFERENCE_SPACE_TYPE_VIEW));
	SpaceInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	SpaceInfo.next = nullptr;
	SpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	SpaceInfo.poseInReferenceSpace = ToXrPose(FTransform::Identity);
	XR_ENSURE(xrCreateReferenceSpace(InSession, &SpaceInfo, &m_HeadLockSpace));

	if (xrEnumeratePassthroughImageRatesHTC)
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("xrEnumeratePassthroughImageRatesHTC get function pointer"));
		uint32_t imageRateCountOutput = 0;
		XR_ENSURE(xrEnumeratePassthroughImageRatesHTC(InSession, 0, &imageRateCountOutput, imageRates.GetData()));
		{
			UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Get rate count"));
			uint32_t imageRateCountInput = imageRateCountOutput;
			for (uint32_t i = 0; i < imageRateCountOutput; i++)
			{
				XrPassthroughConfigurationImageRateHTC imageRate;
				imageRates.Emplace(imageRate);
			}
			XR_ENSURE(xrEnumeratePassthroughImageRatesHTC(InSession, imageRateCountInput, &imageRateCountOutput, imageRates.GetData()));
			{
				UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Get inage rates"));
			}
		}
	}
	else
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("xrEnumeratePassthroughImageRatesHTC is null pointer"));
	}

	if (!m_bPassthroughConfigurationInitialization) {
		m_bPassthroughConfigurationInitialization = true;
		SetPassthroughRate(ProjectSettingsRateType);
		SetPassthroughQuality(ProjectSettingsQualityScale);
	}
	
	return InNext;
}

void FViveOpenXRPassthrough::OnEvent(XrSession InSession, const XrEventDataBaseHeader* InHeader)
{
	if (InHeader->type == XR_TYPE_EVENT_DATA_PASSTHROUGH_CONFIGURATION_IMAGE_RATE_CHANGED_HTC)
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("OnEvent XR_TYPE_EVENT_DATA_PASSTHROUGH_CONFIGURATION_IMAGE_RATE_CHANGED_HTC"));
		const XrEventDataPassthroughConfigurationImageRateChangedHTC& ConfigurationImageRate_changed_event = *reinterpret_cast<XrEventDataPassthroughConfigurationImageRateChangedHTC*>(&InHeader);

		if (SetRateType == 0) {
			if (RateChangeSuccessEvent.IsBound())
			{
				UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("RateChangeSuccessEvent Normal"));
				RateChangeSuccessEvent.Broadcast(ConfigurationRateType::Boost, ConfigurationRateType::Normal);
			}
			else
			{
				UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("RateChangeSuccessEvent Not Bound"));
			}
		}
		else 
		{
			if (RateChangeSuccessEvent.IsBound())
			{
				UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("RateChangeSuccessEvent Boost"));
				RateChangeSuccessEvent.Broadcast(ConfigurationRateType::Normal, ConfigurationRateType::Boost);
			}
			else
			{
				UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("RateChangeSuccessEvent Not Bound"));
			}
		}
	}
	else if (InHeader->type == XR_TYPE_EVENT_DATA_PASSTHROUGH_CONFIGURATION_IMAGE_QUALITY_CHANGED_HTC)
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("OnEvent XR_TYPE_EVENT_DATA_PASSTHROUGH_CONFIGURATION_IMAGE_QUALITY_CHANGED_HTC"));
		const XrEventDataPassthroughConfigurationImageQualityChangedHTC& ConfigurationImageQuality_changed_event = *reinterpret_cast<XrEventDataPassthroughConfigurationImageQualityChangedHTC*>(&InHeader);
		if (QualityChangeSuccessEvent.IsBound())
		{
			UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("QualityChangeSuccessEvent"));
			QualityChangeSuccessEvent.Broadcast(FromQualityScale, GetPassthroughQuality());
			FromQualityScale = GetPassthroughQuality();
		}
		else
		{
			UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("QualityChangeSuccessEvent Not Bound"));
		}
	}
}

int32 FViveOpenXRPassthrough::GetPassthroughRate()
{
	rateType type;
	if (xrGetPassthroughConfigurationHTC && m_supportsRateConfiguration)
	{
		XrPassthroughConfigurationImageRateHTC rateConfig;
		rateConfig.type = XR_TYPE_PASSTHROUGH_CONFIGURATION_IMAGE_RATE_HTC;
		XR_ENSURE(xrGetPassthroughConfigurationHTC(m_Session, (XrPassthroughConfigurationBaseHeaderHTC*)(&rateConfig)));
		{
			int32 rateLevel = 0;
			for (int32 i = 0; i < imageRates.Num(); i++)
			{
				if (imageRates[i].dstImageRate == rateConfig.dstImageRate && imageRates[i].srcImageRate == rateConfig.srcImageRate)
				{
					rateLevel = i;
					return rateLevel;
				}
			}
			return rateLevel;
		}
	}
	UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("xrGetPassthroughConfigurationHTC null pointer"));
	return type.Normal;
}

float FViveOpenXRPassthrough::GetPassthroughQuality()
{
	if (xrGetPassthroughConfigurationHTC && m_supportsQualityConfiguration)
	{
		XrPassthroughConfigurationImageQualityHTC qualityConfig;
		qualityConfig.type = XR_TYPE_PASSTHROUGH_CONFIGURATION_IMAGE_QUALITY_HTC;
		XR_ENSURE(xrGetPassthroughConfigurationHTC(m_Session, (XrPassthroughConfigurationBaseHeaderHTC*)(&qualityConfig)));
		{
			return qualityConfig.scale;
		}
	}
	UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("xrGetPassthroughConfigurationHTC null pointer"));
	return 0.0f;
}

bool FViveOpenXRPassthrough::SetPassthroughQuality(float QualityScale)
{
	UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("SetPassthroughQuality"));
	if (m_supportsQualityConfiguration)
	{
		if (xrSetPassthroughConfigurationHTC)
		{
			XrPassthroughConfigurationImageQualityHTC qualityConfig;
			qualityConfig.type = XR_TYPE_PASSTHROUGH_CONFIGURATION_IMAGE_QUALITY_HTC;
			qualityConfig.scale = QualityScale;
			XR_ENSURE(xrSetPassthroughConfigurationHTC(m_Session, (XrPassthroughConfigurationBaseHeaderHTC*)(&qualityConfig)));
			{
				UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("SetPassthroughQuality Success"));
				return true;
			}
		}
		UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("xrSetPassthroughConfigurationHTC null pointer"));
		if (QualityChangeFailureEvent.IsBound())
			QualityChangeFailureEvent.Broadcast();
		return false;
	}
	UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("xrSetPassthroughConfigurationHTC nunsupported"));
	if (QualityChangeFailureEvent.IsBound())
		QualityChangeFailureEvent.Broadcast();
	return false;
}

bool FViveOpenXRPassthrough::SetPassthroughRate(int32 Level)
{
	UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("SetPassthroughRate"));
	if (xrEnumeratePassthroughImageRatesHTC)
	{
		uint32_t imageRateCountOutput = 0;
		XR_ENSURE(xrEnumeratePassthroughImageRatesHTC(m_Session, 0, &imageRateCountOutput, imageRates.GetData()));
		{
			UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("imageRateCountOutput %d"), imageRateCountOutput);
			imageRates.Empty();
			uint32_t imageRateCountInput = imageRateCountOutput;
			for (uint32_t i = 0; i < imageRateCountOutput; i++)
			{
				XrPassthroughConfigurationImageRateHTC imageRate;
				imageRates.Emplace(imageRate);
			}
			XR_ENSURE(xrEnumeratePassthroughImageRatesHTC(m_Session, imageRateCountInput, &imageRateCountOutput, imageRates.GetData()));
		}
	}
	else
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("xrEnumeratePassthroughImageRatesHTC is null pointer"));
		if (RateChangeFailureEvent.IsBound())
			RateChangeFailureEvent.Broadcast();
		return false;
	}

	if (m_supportsRateConfiguration && !imageRates.IsEmpty())
	{
		if (xrSetPassthroughConfigurationHTC)
		{
			XrPassthroughConfigurationImageRateHTC rateConfig;
			rateConfig.type = XR_TYPE_PASSTHROUGH_CONFIGURATION_IMAGE_RATE_HTC;
			rateConfig.srcImageRate = imageRates[Level].srcImageRate;
			rateConfig.dstImageRate = imageRates[Level].dstImageRate;
			XR_ENSURE(xrSetPassthroughConfigurationHTC(m_Session, (XrPassthroughConfigurationBaseHeaderHTC*)(&rateConfig)));
			{
				UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("SetPassthroughRate Success"));
				SetRateType = Level;
				return true;
			}
		}
		UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("xrSetPassthroughConfigurationHTC null pointer"));
		if (RateChangeFailureEvent.IsBound())
			RateChangeFailureEvent.Broadcast();
		return false;
	}
	UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("xrSetPassthroughConfigurationHTC nunsupported"));
	if (RateChangeFailureEvent.IsBound())
		RateChangeFailureEvent.Broadcast();
	return false;
}

void FViveOpenXRPassthrough::OnDestroySession(XrSession InSession)
{	
	UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Entry Passthrough OnDestroySession."));
	if (m_bEnablePassthrough)
	{
		m_bPassthroughConfigurationInitialization = false;

		m_supportsQualityConfiguration = false;
		m_supportsRateConfiguration = false;
		m_CurrentLayerForm = XR_PASSTHROUGH_FORM_MAX_ENUM_HTC;

		m_HeadLockSpace = XR_NULL_HANDLE;
		m_BaseSpace = XR_NULL_HANDLE;
		m_Session = XR_NULL_HANDLE;
		// Destroy planer passthrough
		if (planerPassthroughHandle) {
			XR_ENSURE(xrDestroyPassthroughHTC(planerPassthroughHandle));
			planerPassthroughHandle = XR_NULL_HANDLE;
			isPlanerPassthroughCreated = false;
			isPlanerPassthroughIsValid = false;
			PlanerPassthroughDataHandle = { XR_NULL_HANDLE, nullptr, nullptr, false };
		}
		
		// Destroy projected passthroughs
		for (auto & PassthroughData : ProjectedPassthroughMap)
		{
			if(PassthroughData.Key)
				XR_ENSURE(xrDestroyPassthroughHTC(PassthroughData.Key));
			PassthroughData.Value->Handle = XR_NULL_HANDLE;
			PassthroughData.Value->Valid = false;
			delete PassthroughData.Value->passthroughMeshTransformInfoPtr;
			delete PassthroughData.Value->projectedPassthroughCompositionLayerInfoPtr;
		}
		ProjectedPassthroughDataHandle = { XR_NULL_HANDLE, nullptr, nullptr, false };
		isProjectedPassthroughCreated = false;
		ProjectedPassthroughMap.Empty();
	}
}

bool FViveOpenXRPassthrough::GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	if (m_bEnablePassthrough)
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("GetRequiredExtensions() Add Passthrough Extension Name %s."), ANSI_TO_TCHAR(XR_HTC_PASSTHROUGH_EXTENSION_NAME));
		OutExtensions.Add(XR_HTC_PASSTHROUGH_EXTENSION_NAME);
	}
	return true;
}

bool FViveOpenXRPassthrough::GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	if (m_bEnablePassthrough)
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("GetOptionalExtensions() Add Passthrough Extension Name %s."), ANSI_TO_TCHAR(XR_HTC_PASSTHROUGH_CONFIGURATION_EXTENSION_NAME));
		OutExtensions.Add(XR_HTC_PASSTHROUGH_CONFIGURATION_EXTENSION_NAME);
	}
	return true;
}

void FViveOpenXRPassthrough::PostGetSystem(XrInstance InInstance, XrSystemId InSystem)
{
	if (!m_bEnablePassthrough) return;

	m_XrSystemId = InSystem;
	XR_ENSURE(xrGetInstanceProcAddr(InInstance, "xrGetSystemProperties", (PFN_xrVoidFunction*)&FPassthrough_ext.xrGetSystemProperties));

	XrSystemPassthroughConfigurationPropertiesHTC SystemPassthroughConfigurationPropertiesHTC{ XR_TYPE_SYSTEM_PASSTHROUGH_CONFIGURATION_PROPERTIES_HTC };
	SystemPassthroughConfigurationPropertiesHTC.next = NULL;
	XrSystemProperties systemProperties{ XR_TYPE_SYSTEM_PROPERTIES, &SystemPassthroughConfigurationPropertiesHTC };

	XR_ENSURE(FPassthrough_ext.xrGetSystemProperties(InInstance, m_XrSystemId, &systemProperties));
	{
		if (SystemPassthroughConfigurationPropertiesHTC.supportsImageQuality) {
			m_supportsQualityConfiguration = true;
		}

		if (SystemPassthroughConfigurationPropertiesHTC.supportsImageRate) {
			m_supportsRateConfiguration = true;
		}
	}
}

void FViveOpenXRPassthrough::PostCreateSession(XrSession InSession)
{
	m_Session = InSession;
}

void FViveOpenXRPassthrough::UpdateDeviceLocations(XrSession InSession, XrTime DisplayTime, XrSpace TrackingSpace)
{
	m_BaseSpace = TrackingSpace;
}

PassthroughData FViveOpenXRPassthrough::CreatePassthrough(XrPassthroughFormHTC layerForm)
{
	if (!m_bEnablePassthrough)
	{
		if (layerForm == XR_PASSTHROUGH_FORM_PLANAR_HTC)
			return PlanerPassthroughDataHandle;
		else
			return ProjectedPassthroughDataHandle;
	}
	
	if (!isPlanerPassthroughCreated && layerForm == XR_PASSTHROUGH_FORM_PLANAR_HTC)
	{
		XrPassthroughCreateInfoHTC createInfo{ XR_TYPE_PASSTHROUGH_CREATE_INFO_HTC };

		createInfo.form = layerForm;

		//Create Planer Passthrough handle
		if (!XR_ENSURE(xrCreatePassthroughHTC(m_Session, &createInfo, &planerPassthroughHandle)))
		{
			UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("Create planer passthrough failed."));
			return PlanerPassthroughDataHandle;
		}

		PlanerPassthroughDataHandle.Handle = planerPassthroughHandle;
		PlanerPassthroughDataHandle.Valid = true;

		XrCompositionLayerPassthroughHTC* newPassthroughCompositionLayerInfoPtr = new XrCompositionLayerPassthroughHTC();
		newPassthroughCompositionLayerInfoPtr->type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_HTC;
		newPassthroughCompositionLayerInfoPtr->next = nullptr;
		newPassthroughCompositionLayerInfoPtr->layerFlags = 0;
		newPassthroughCompositionLayerInfoPtr->layerFlags |= 1UL << XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		newPassthroughCompositionLayerInfoPtr->space = XR_NULL_HANDLE;
		newPassthroughCompositionLayerInfoPtr->passthrough = planerPassthroughHandle;

		XrPassthroughColorHTC newPassthroughColor = { XR_TYPE_PASSTHROUGH_COLOR_HTC, nullptr, 1.0 }; //default value
		newPassthroughCompositionLayerInfoPtr->color = newPassthroughColor;

		planerPassthroughCompositionLayerInfoPtr = newPassthroughCompositionLayerInfoPtr;
		isPlanerPassthroughCreated = true;
		isPlanerPassthroughIsValid = true;
		return PlanerPassthroughDataHandle;
	}
	else if(isPlanerPassthroughCreated && layerForm == XR_PASSTHROUGH_FORM_PLANAR_HTC)
	{
		return PlanerPassthroughDataHandle;
	}

	if (!isProjectedPassthroughCreated && layerForm == XR_PASSTHROUGH_FORM_PROJECTED_HTC)
	{
		XrPassthroughHTC projectedPassthroughHandle;
		XrPassthroughCreateInfoHTC createInfo{ XR_TYPE_PASSTHROUGH_CREATE_INFO_HTC };

		createInfo.form = layerForm;

		//Create Projected Passthrough handle
		if (!XR_ENSURE(xrCreatePassthroughHTC(m_Session, &createInfo, &projectedPassthroughHandle)))
		{
			UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("Create projected passthrough failed."));
			return ProjectedPassthroughDataHandle;
		}

		ProjectedPassthroughDataHandle.Handle = projectedPassthroughHandle;
		ProjectedPassthroughDataHandle.Valid = true;

		XrCompositionLayerPassthroughHTC* newPassthroughCompositionLayerInfoPtr = new XrCompositionLayerPassthroughHTC();
		newPassthroughCompositionLayerInfoPtr->type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_HTC;
		newPassthroughCompositionLayerInfoPtr->next = nullptr;
		newPassthroughCompositionLayerInfoPtr->layerFlags = 0;
		newPassthroughCompositionLayerInfoPtr->layerFlags |= 1UL << XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		newPassthroughCompositionLayerInfoPtr->space = XR_NULL_HANDLE;
		newPassthroughCompositionLayerInfoPtr->passthrough = projectedPassthroughHandle;

		XrPassthroughColorHTC newPassthroughColor = { XR_TYPE_PASSTHROUGH_COLOR_HTC, nullptr, 1.0 }; //default value
		newPassthroughCompositionLayerInfoPtr->color = newPassthroughColor;

		ProjectedPassthroughDataHandle.projectedPassthroughCompositionLayerInfoPtr = newPassthroughCompositionLayerInfoPtr;

		XrPassthroughMeshTransformInfoHTC* newPassthroughMeshTransformInfoPtr = new XrPassthroughMeshTransformInfoHTC();
		newPassthroughMeshTransformInfoPtr->type = XR_TYPE_PASSTHROUGH_MESH_TRANSFORM_INFO_HTC;
		newPassthroughMeshTransformInfoPtr->next = nullptr;
		newPassthroughMeshTransformInfoPtr->vertexCount = 0;
		newPassthroughMeshTransformInfoPtr->vertices = nullptr;
		newPassthroughMeshTransformInfoPtr->indexCount = 0;
		newPassthroughMeshTransformInfoPtr->indices = nullptr;
		newPassthroughMeshTransformInfoPtr->baseSpace = XR_NULL_HANDLE;
		newPassthroughMeshTransformInfoPtr->time = 0;
		newPassthroughMeshTransformInfoPtr->pose = ToXrPose(FTransform::Identity);
		newPassthroughMeshTransformInfoPtr->scale = ToXrVector(FVector::One());

		ProjectedPassthroughDataHandle.passthroughMeshTransformInfoPtr = newPassthroughMeshTransformInfoPtr;

		ProjectedPassthroughMap.Add(projectedPassthroughHandle ,&ProjectedPassthroughDataHandle);
		isProjectedPassthroughCreated = true;
		return ProjectedPassthroughDataHandle;
	}
	else if(isProjectedPassthroughCreated && layerForm == XR_PASSTHROUGH_FORM_PROJECTED_HTC)
	{
		return ProjectedPassthroughDataHandle;
	}

	if (layerForm == XR_PASSTHROUGH_FORM_PLANAR_HTC)
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Unexpected Error"));
		return PlanerPassthroughDataHandle;

	}
	else
	{
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Unexpected Error"));
		return ProjectedPassthroughDataHandle;
	}
}

bool FViveOpenXRPassthrough::SwitchPassthrough(XrPassthroughFormHTC layerForm)
{
	if (!m_bEnablePassthrough) return false;

	m_CurrentLayerForm = layerForm;

	return true;
}

void FViveOpenXRPassthrough::UpdateCompositionLayers(XrSession InSession, TArray<const XrCompositionLayerBaseHeader*>& Headers)
{	
	if (!m_bEnablePassthrough) return;
#if PLATFORM_WINDOWS
	if (m_CurrentLayerForm == XR_PASSTHROUGH_FORM_PLANAR_HTC)
	{
		if (planerPassthroughCompositionLayerInfoPtr)
			Headers.Insert(reinterpret_cast<const XrCompositionLayerBaseHeader*>(planerPassthroughCompositionLayerInfoPtr), 0);
		else
			UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("UpdateCompositionLayers during EndFrame: planerPassthroughCompositionLayerInfoPtr null reference."));
	}
	else if (m_CurrentLayerForm == XR_PASSTHROUGH_FORM_PROJECTED_HTC)
	{
		for (const TPair<XrPassthroughHTC, PassthroughData*>& PassthroughData : ProjectedPassthroughMap)
		{
			if (PassthroughData.Value->projectedPassthroughCompositionLayerInfoPtr && PassthroughData.Value->Handle && PassthroughData.Value->Valid)
				Headers.Insert(reinterpret_cast<const XrCompositionLayerBaseHeader*>(PassthroughData.Value->projectedPassthroughCompositionLayerInfoPtr), 0);
			else
				UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("UpdateCompositionLayers during EndFrame: projectedPassthroughCompositionLayerInfoPtr null reference."));
		}
	}
#endif
}

bool FViveOpenXRPassthrough::DestroyPassthrough(XrPassthroughHTC PassthroughHandle)
{
	if (!m_bEnablePassthrough) return false;

	if (PassthroughHandle == planerPassthroughHandle)
	{
		XR_ENSURE(xrDestroyPassthroughHTC(planerPassthroughHandle));

		planerPassthroughHandle = XR_NULL_HANDLE;
		isPlanerPassthroughCreated = false;
		isPlanerPassthroughIsValid = false;
		PlanerPassthroughDataHandle = { XR_NULL_HANDLE, nullptr, nullptr, false };
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Planer passthrough destroyed."));
		return true;
	}
	else if(ProjectedPassthroughMap.Contains(PassthroughHandle))
	{
		ProjectedPassthroughMap[PassthroughHandle]->Valid = false;
		XR_ENSURE(xrDestroyPassthroughHTC(PassthroughHandle));
		PassthroughHandle = XR_NULL_HANDLE;
		isProjectedPassthroughCreated = false;
		ProjectedPassthroughMap.Empty();
		ProjectedPassthroughDataHandle = { XR_NULL_HANDLE, nullptr, nullptr, false };
		UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Projected passthrough destroyed."));
		return true;
	}
	else
	{
		UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("Passthrough handle invalid."));
		return false;
	}
}

const void* FViveOpenXRPassthrough::OnEndProjectionLayer(XrSession InSession, int32 InLayerIndex, const void* InNext, XrCompositionLayerFlags& OutFlags)
{
	if (!isPlanerPassthroughIsValid && planerPassthroughCompositionLayerInfoPtr) 
	{
		delete planerPassthroughCompositionLayerInfoPtr;
		planerPassthroughCompositionLayerInfoPtr = nullptr;
	}

	for (auto& PassthroughData : ProjectedPassthroughMap)
	{
		if (PassthroughData.Value->Handle && !PassthroughData.Value->Valid) 
		{
			if (PassthroughData.Value->passthroughMeshTransformInfoPtr) {
				delete PassthroughData.Value->passthroughMeshTransformInfoPtr;
				PassthroughData.Value->passthroughMeshTransformInfoPtr = nullptr;
			}	
			if (PassthroughData.Value->projectedPassthroughCompositionLayerInfoPtr) {
				delete PassthroughData.Value->projectedPassthroughCompositionLayerInfoPtr;
				PassthroughData.Value->projectedPassthroughCompositionLayerInfoPtr = nullptr;
			}
		}
	}

	if (m_CurrentLayerForm == XR_PASSTHROUGH_FORM_PLANAR_HTC)
	{
		if (isPlanerPassthroughIsValid && planerPassthroughCompositionLayerInfoPtr)
		{
			OutFlags |= XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			planerPassthroughCompositionLayerInfoPtr->next = InNext;
#if PLATFORM_ANDROID
			return reinterpret_cast<const void*>(planerPassthroughCompositionLayerInfoPtr);
#endif
		}
	}
	else if (m_CurrentLayerForm == XR_PASSTHROUGH_FORM_PROJECTED_HTC)
	{
		for (auto& PassthroughData : ProjectedPassthroughMap)
		{
			if (PassthroughData.Value->Handle && PassthroughData.Value->Valid)
			{
				if (PassthroughData.Value->projectedPassthroughCompositionLayerInfoPtr && PassthroughData.Value->passthroughMeshTransformInfoPtr) 
				{
					PassthroughData.Value->passthroughMeshTransformInfoPtr->next = InNext;
					PassthroughData.Value->projectedPassthroughCompositionLayerInfoPtr->next = PassthroughData.Value->passthroughMeshTransformInfoPtr;
					OutFlags |= XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
#if PLATFORM_ANDROID
					return reinterpret_cast<const void*>(PassthroughData.Value->projectedPassthroughCompositionLayerInfoPtr);
#endif
				}
			}
		}
	}

	return InNext;
}

//For projected passthrough
bool FViveOpenXRPassthrough::SetPassthroughAlpha(XrPassthroughHTC PassthroughHandle, float alpha)
{
	if (!m_bEnablePassthrough) return false;

	if (PassthroughHandle == planerPassthroughHandle)
	{
		if(planerPassthroughCompositionLayerInfoPtr)
		{
			planerPassthroughCompositionLayerInfoPtr->color.alpha = alpha;
			return true;
		}
		else
		{
			UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("planerPassthroughCompositionLayerInfoPtr null reference."));
			return false;
		}
	}
	else if (ProjectedPassthroughMap.Contains(PassthroughHandle))
	{
		if (ProjectedPassthroughMap[PassthroughHandle]->projectedPassthroughCompositionLayerInfoPtr)
		{
			ProjectedPassthroughMap[PassthroughHandle]->projectedPassthroughCompositionLayerInfoPtr->color.alpha = alpha;
			return true;
		}
		else
		{
			UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("projectedPassthroughCompositionLayerInfoPtr null reference."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("Passthrough handle invalid."));
		return false;
	}
}

bool FViveOpenXRPassthrough::SetPassthroughMesh(XrPassthroughHTC PassthroughHandle, uint32_t inVertexCount, const XrVector3f* inVertexBuffer, uint32_t inIndexCount, const uint32_t* inIndexBuffer)
{
	if (!m_bEnablePassthrough) return false;

	if (ProjectedPassthroughMap.Contains(PassthroughHandle))
	{
		if (ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr)
		{
			ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr->vertexCount = inVertexCount;
			ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr->vertices = inVertexBuffer;
			ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr->indexCount = inIndexCount;
			ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr->indices = inIndexBuffer;
			return true;
		}
		else
		{
			UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("passthroughMeshTransformInfoPtr null reference."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("Passthrough handle invalid."));
		return false;
	}
}


bool FViveOpenXRPassthrough::SetPassthroughMeshTransform(XrPassthroughHTC PassthroughHandle, XrSpace meshSpace, XrPosef meshPose, XrVector3f meshScale)
{
	if (!m_bEnablePassthrough) return false;

	if (ProjectedPassthroughMap.Contains(PassthroughHandle))
	{
		if (ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr)
		{
			ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr->baseSpace = meshSpace;
			ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr->pose = meshPose;
			ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr->scale = meshScale;
			return true;
		}
		else
		{
			UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("passthroughMeshTransformInfoPtr null reference."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("Passthrough handle invalid."));
		return false;
	}
}


bool FViveOpenXRPassthrough::SetPassthroughMeshTransformSpace(XrPassthroughHTC PassthroughHandle, XrSpace meshSpace)
{
	if (!m_bEnablePassthrough) return false;

	if (ProjectedPassthroughMap.Contains(PassthroughHandle))
	{
		if (ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr)
		{
			ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr->baseSpace = meshSpace;
			return true;
		}
		else
		{
			UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("passthroughMeshTransformInfoPtr null reference."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("Passthrough handle invalid."));
		return false;
	}
}


bool FViveOpenXRPassthrough::SetPassthroughMeshTransformPosition(XrPassthroughHTC PassthroughHandle, XrVector3f meshPosition)
{
	if (!m_bEnablePassthrough) return false;

	if (ProjectedPassthroughMap.Contains(PassthroughHandle))
	{
		if (ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr)
		{
			ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr->pose.position = meshPosition;
			return true;
		}
		else
		{
			UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("passthroughMeshTransformInfoPtr null reference."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("Passthrough handle invalid."));
		return false;
	}
}

bool FViveOpenXRPassthrough::SetPassthroughMeshTransformOrientation(XrPassthroughHTC PassthroughHandle, XrQuaternionf meshOrientation)
{
	if (!m_bEnablePassthrough) return false;

	if (ProjectedPassthroughMap.Contains(PassthroughHandle))
	{
		if (ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr)
		{
			ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr->pose.orientation = meshOrientation;
			return true;
		}
		else
		{
			UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("passthroughMeshTransformInfoPtr null reference."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("Passthrough handle invalid."));
		return false;
	}
}


bool FViveOpenXRPassthrough::SetPassthroughMeshTransformScale(XrPassthroughHTC PassthroughHandle, XrVector3f meshScale)
{
	if (!m_bEnablePassthrough) return false;

	if (ProjectedPassthroughMap.Contains(PassthroughHandle))
	{
		if (ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr)
		{
			ProjectedPassthroughMap[PassthroughHandle]->passthroughMeshTransformInfoPtr->scale = meshScale;
			return true;
		}
		else
		{
			UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("passthroughMeshTransformInfoPtr null reference."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("Passthrough handle invalid."));
		return false;
	}
}

XrSpace FViveOpenXRPassthrough::GetHeadlockXrSpace()
{
	return m_HeadLockSpace;
}

XrSpace FViveOpenXRPassthrough::GetTrackingXrSpace()
{
	return m_BaseSpace;
}

IMPLEMENT_MODULE(FViveOpenXRPassthrough, ViveOpenXRPassthrough)
// Copyright HTC Corporation. All Rights Reserved.

#include "ViveOpenXRAnchorModule.h"
#include "OpenXRCore.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformTime.h"
#include <string>

DEFINE_LOG_CATEGORY(ViveOXRAnchor);

FOpenXRHMD* FViveOpenXRAnchor::hmd = nullptr;
FViveOpenXRAnchor* FViveOpenXRAnchor::instance = nullptr;

FOpenXRHMD* FViveOpenXRAnchor::HMD() {
	if (hmd != nullptr)
		return hmd;
	if (GEngine->XRSystem.IsValid())
	{
		hmd = static_cast<FOpenXRHMD*>(GEngine->XRSystem->GetHMDDevice());
	}
	return hmd;
}

FViveOpenXRAnchor* FViveOpenXRAnchor::Instance()
{
	if (instance != nullptr)
	{
		return instance;
	}
	else
	{
		if (GEngine->XRSystem.IsValid() && HMD() != nullptr)
		{
			for (IOpenXRExtensionPlugin* Module : HMD()->GetExtensionPlugins())
			{
				if (Module->GetDisplayName() == TEXT("ViveOpenXRAnchor"))
				{
					instance = static_cast<FViveOpenXRAnchor*>(Module);
					break;
				}
			}
		}
		return instance;
	}
}


void FViveOpenXRAnchor::StartupModule()
{
	check(GConfig && GConfig->IsReadyForUse());
	FString modeName;
	if (GConfig->GetString(TEXT("/Script/ViveOpenXRRuntimeSettings.ViveOpenXRRuntimeSettings"), TEXT("bEnableAnchor"), modeName, GEngineIni))
	{
		if (modeName.Equals("False"))
		{
			m_bEnableAnchor = false;
		}
		else if (modeName.Equals("True"))
		{
			m_bEnableAnchor = true;
		}
	}
	else
		m_bEnableAnchor = false;


	if (m_bEnableAnchor)
	{
		UE_LOG(ViveOXRAnchor, Log, TEXT("Enable Anchor."));
		instance = this;
	}
	else
	{
		UE_LOG(ViveOXRAnchor, Log, TEXT("Disable Anchor."));
		instance = nullptr;

		return;
	}

	RegisterOpenXRExtensionModularFeature();
	UE_LOG(ViveOXRAnchor, Log, TEXT("StartupModule() Finished."));
}

void FViveOpenXRAnchor::ShutdownModule()
{
	instance = nullptr;
	if (m_bEnableAnchor)
		UnregisterOpenXRExtensionModularFeature();
}

FString FViveOpenXRAnchor::GetDisplayName()
{
	return FString(TEXT("ViveOpenXRAnchor"));
}

bool FViveOpenXRAnchor::GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	UE_LOG(ViveOXRAnchor, Log, TEXT("GetRequiredExtensions() Add Anchor Extension Name %s."), ANSI_TO_TCHAR(XR_HTC_ANCHOR_EXTENSION_NAME));
	OutExtensions.Add(XR_HTC_ANCHOR_EXTENSION_NAME);
	return true;
}

bool FViveOpenXRAnchor::GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions) {
	UE_LOG(ViveOXRAnchor, Log, TEXT("GetOptionalExtensions() Add Anchor Persistence Extension Name %s."), ANSI_TO_TCHAR(XR_HTC_ANCHOR_PERSISTENCE_EXTENSION_NAME));
	OutExtensions.Add(XR_HTC_ANCHOR_PERSISTENCE_EXTENSION_NAME);
	return true;
}

const void* FViveOpenXRAnchor::OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext)
{
	if (!m_bEnableAnchor) return InNext;
	if (HMD() == nullptr) return InNext;

	UE_LOG(ViveOXRAnchor, Log, TEXT("Entry Anchor OnCreateSession."));

	PFN_xrGetInstanceProcAddr GetIPA = xrGetInstanceProcAddr;
	bool useMockRuntime = false;

	if (!HMD()->IsExtensionEnabled(XR_HTC_ANCHOR_EXTENSION_NAME) && !useMockRuntime)
		return InNext;

	m_bExtAnchorEnabled = true;

	XR_ENSURE(GetIPA(InInstance, "xrLocateSpace", (PFN_xrVoidFunction*)&xrLocateSpace));
	XR_ENSURE(GetIPA(InInstance, "xrDestroySpace", (PFN_xrVoidFunction*)&xrDestroySpace));
	XR_ENSURE(GetIPA(InInstance, "xrCreateSpatialAnchorHTC", (PFN_xrVoidFunction*)&xrCreateSpatialAnchorHTC));
	XR_ENSURE(GetIPA(InInstance, "xrGetSpatialAnchorNameHTC", (PFN_xrVoidFunction*)&xrGetSpatialAnchorNameHTC));

	XrSystemAnchorPropertiesHTC systemAnchorProperties = {};
	systemAnchorProperties.type = XR_TYPE_SYSTEM_ANCHOR_PROPERTIES_HTC;
	systemAnchorProperties.next = nullptr;
	XrSystemProperties systemProperties = {};
	systemProperties.type = XR_TYPE_SYSTEM_PROPERTIES;
	systemProperties.next = &systemAnchorProperties;
	XrResult result = xrGetSystemProperties(InInstance, InSystem, &systemProperties);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("OnCreateSession() xrGetSystemProperties failed with result %d."), result);
	}
	else
	{
		isAnchorSupported = systemAnchorProperties.supportsAnchor > 0;
		UE_LOG(ViveOXRAnchor, Log, TEXT("OnCreateSession() Is Anchor support: %d."), systemAnchorProperties.supportsAnchor);
	}
	if (useMockRuntime)
		isAnchorSupported = true;

	if (!hmd->IsExtensionEnabled(XR_HTC_ANCHOR_PERSISTENCE_EXTENSION_NAME) && !useMockRuntime)
	{
		UE_LOG(ViveOXRAnchor, Warning, TEXT("Extension Anchor Persistence is NOT enabled."));
		return InNext;
	}

	m_bExtAnchorPersistenceEnabled = true;

	if (!hmd->IsExtensionEnabled(XR_EXT_FUTURE_EXTENSION_NAME) && !useMockRuntime)
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Extension Future is NOT enabled. Thus, Anchor Persistence is not suported."));
		return InNext;
	}

	auto futureMod = FViveOpenXRFuture::Instance();

	if (futureMod == nullptr)
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to get Future Module."));
		return InNext;
	}

	// Initialize new function pointers
	XR_ENSURE(GetIPA(InInstance, "xrAcquirePersistedAnchorCollectionAsyncHTC", (PFN_xrVoidFunction*)&xrAcquirePersistedAnchorCollectionAsyncHTC));
	XR_ENSURE(GetIPA(InInstance, "xrAcquirePersistedAnchorCollectionCompleteHTC", (PFN_xrVoidFunction*)&xrAcquirePersistedAnchorCollectionCompleteHTC));
		XR_ENSURE(GetIPA(InInstance, "xrReleasePersistedAnchorCollectionHTC", (PFN_xrVoidFunction*)&xrReleasePersistedAnchorCollectionHTC));
	XR_ENSURE(GetIPA(InInstance, "xrPersistSpatialAnchorAsyncHTC", (PFN_xrVoidFunction*)&xrPersistSpatialAnchorAsyncHTC));
	XR_ENSURE(GetIPA(InInstance, "xrPersistSpatialAnchorCompleteHTC", (PFN_xrVoidFunction*)&xrPersistSpatialAnchorCompleteHTC));
	XR_ENSURE(GetIPA(InInstance, "xrUnpersistSpatialAnchorHTC", (PFN_xrVoidFunction*)&xrUnpersistSpatialAnchorHTC));
	XR_ENSURE(GetIPA(InInstance, "xrEnumeratePersistedAnchorNamesHTC", (PFN_xrVoidFunction*)&xrEnumeratePersistedAnchorNamesHTC));
	XR_ENSURE(GetIPA(InInstance, "xrCreateSpatialAnchorFromPersistedAnchorAsyncHTC", (PFN_xrVoidFunction*)&xrCreateSpatialAnchorFromPersistedAnchorAsyncHTC));
	XR_ENSURE(GetIPA(InInstance, "xrCreateSpatialAnchorFromPersistedAnchorCompleteHTC", (PFN_xrVoidFunction*)&xrCreateSpatialAnchorFromPersistedAnchorCompleteHTC));
	XR_ENSURE(GetIPA(InInstance, "xrClearPersistedAnchorsHTC", (PFN_xrVoidFunction*)&xrClearPersistedAnchorsHTC));
	XR_ENSURE(GetIPA(InInstance, "xrGetPersistedAnchorPropertiesHTC", (PFN_xrVoidFunction*)&xrGetPersistedAnchorPropertiesHTC));
	XR_ENSURE(GetIPA(InInstance, "xrExportPersistedAnchorHTC", (PFN_xrVoidFunction*)&xrExportPersistedAnchorHTC));
	XR_ENSURE(GetIPA(InInstance, "xrImportPersistedAnchorHTC", (PFN_xrVoidFunction*)&xrImportPersistedAnchorHTC));
	XR_ENSURE(GetIPA(InInstance, "xrGetPersistedAnchorNameFromBufferHTC", (PFN_xrVoidFunction*)&xrGetPersistedAnchorNameFromBufferHTC));

	isAnchorPersistenceSupported = true;
	return InNext;
}

void FViveOpenXRAnchor::PostCreateSession(XrSession InSession)
{
	UE_LOG(ViveOXRAnchor, Log, TEXT("Entry Anchor PostCreateSession InSession %llu."), InSession);
	m_Session = InSession;
}

XrResult FViveOpenXRAnchor::LocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location)
{
	if (xrLocateSpace == nullptr)
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("LocateSpace() xrLocateSpace is nullptr."));
		return XR_ERROR_HANDLE_INVALID;
	}
	return xrLocateSpace(space, baseSpace, time, location);
}

XrResult FViveOpenXRAnchor::DestroySpace(XrSpace space)
{
	if (xrDestroySpace == nullptr)
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("DestroySpace() xrDestroySpace is nullptr."));
		return XR_ERROR_HANDLE_INVALID;
	}
	return xrDestroySpace(space);
}

bool FViveOpenXRAnchor::CreateSpatialAnchor(const XrSpatialAnchorCreateInfoHTC* createInfo, XrSpace* anchor)
{
	UE_LOG(ViveOXRAnchor, Log, TEXT("CreateSpatialAnchor()"));
	if (xrCreateSpatialAnchorHTC == nullptr)
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("CreateSpatialAnchor() xrCreateSpatialAnchorHTC is nullptr."));
		return false;
	}

	if (anchor == nullptr)
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("CreateSpatialAnchor() anchor is nullptr."));
		return false;
	}

	*anchor = 0;

	XrResult result = xrCreateSpatialAnchorHTC(m_Session, createInfo, anchor);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("CreateSpatialAnchor() xrCreateSpatialAnchorHTC failed with result %d."), result);
		return false;
	}
	return true;
}

bool FViveOpenXRAnchor::GetSpatialAnchorName(XrSpace anchor, XrSpatialAnchorNameHTC* name)
{
	if (xrGetSpatialAnchorNameHTC == nullptr)
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("GetSpatialAnchorName() xrGetSpatialAnchorNameHTC is nullptr."));
		return false;
	}

	if (name == nullptr)
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("GetSpatialAnchorName() name is nullptr."));
		return false;
	}

	name->name[0] = 0;

	XrResult result = xrGetSpatialAnchorNameHTC(anchor, name);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("GetSpatialAnchorName() xrGetSpatialAnchorNameHTC failed with result %d."), result);
		return false;
	}
	return true;
}

XrSpatialAnchorCreateInfoHTC FViveOpenXRAnchor::MakeCreateInfo(const FVector& loc, const FQuat& rot, XrSpace baseSpace, FString name, float worldToMeterScale)
{
	XrSpatialAnchorCreateInfoHTC createInfo = {};
	createInfo.type = XR_TYPE_SPATIAL_ANCHOR_CREATE_INFO_HTC;
	createInfo.next = nullptr;
	createInfo.space = baseSpace;
	createInfo.poseInSpace = XrPosef{ ToXrQuat(rot), ToXrVector(loc, worldToMeterScale) };
	createInfo.name.name[0] = 0;
	std::string str = TCHAR_TO_ANSI(*name);
	int l = str.length();
	size_t m = l < XR_MAX_SPATIAL_ANCHOR_NAME_SIZE_HTC - 1 ? l : XR_MAX_SPATIAL_ANCHOR_NAME_SIZE_HTC - 1;

#if PLATFORM_ANDROID
	strncpy(createInfo.name.name, str.c_str(), m);
#else
	strncpy_s(createInfo.name.name, str.c_str(), m);
#endif
	createInfo.name.name[m] = 0;

	return createInfo;
}

bool FViveOpenXRAnchor::LocateAnchor(XrSpace anchor, FRotator& rotation, FVector& translation)
{
	if (!HMD()) return false;
	if (xrLocateSpace == nullptr) return false;
	XrTime time = hmd->GetDisplayTime();
	XrSpace baseSpace = hmd->GetTrackingSpace();

	XrSpaceLocation loc{};

	auto result = xrLocateSpace((XrSpace)anchor, baseSpace, time, &loc);
	if (XR_FAILED(result)) {
		UE_LOG(ViveOXRAnchor, Error, TEXT("LocateAnchor() xrLocateSpace failed.  result=%d."), result);
		return false;
	}

	if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0 ||
		(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0) {
		UE_LOG(ViveOXRAnchor, Error, TEXT("LocateAnchor() locationFlags has not valid bits. Flags=%016llX"), loc.locationFlags);
		return false;
	}

	translation = ToFVector(loc.pose.position, hmd->GetWorldToMetersScale());
	auto rot = ToFQuat(loc.pose.orientation);
	rot.Normalize();
	rotation = FRotator(rot);
	return true;
}

FString FViveOpenXRAnchor::GenerateAnchorName(FString prefix) {
	uint32_t fc = GFrameCounter;
	fc %= 10000;
	uint32_t time = ((uint32_t)(FPlatformTime::Seconds() * 1000)) % 1000000;
	// This name will have a fixed length.
	FString name = prefix + FString::Printf(TEXT("_%04u_%06u_VOXR"), fc, time);
	return name;
}

FString FViveOpenXRAnchor::ToFString(const XrSpatialAnchorNameHTC& xrName)
{
	ANSICHAR name[XR_MAX_SPATIAL_ANCHOR_NAME_SIZE_HTC];
#if PLATFORM_ANDROID
	strncpy(name, xrName.name, XR_MAX_SPATIAL_ANCHOR_NAME_SIZE_HTC - 1);
#else
	strncpy_s(name, xrName.name, XR_MAX_SPATIAL_ANCHOR_NAME_SIZE_HTC - 1);
#endif
	name[XR_MAX_SPATIAL_ANCHOR_NAME_SIZE_HTC - 1] = 0;

	return FString(FUTF8ToTCHAR(name));
}

XrSpatialAnchorNameHTC FViveOpenXRAnchor::FromFString(const FString& name)
{
	XrSpatialAnchorNameHTC xrName = {};
	std::string anchorNameStr(TCHAR_TO_UTF8(*name));
#if PLATFORM_ANDROID
	strncpy(xrName.name, anchorNameStr.c_str(), XR_MAX_SPATIAL_ANCHOR_NAME_SIZE_HTC - 1);
#else	
	strncpy_s(xrName.name, anchorNameStr.c_str(), XR_MAX_SPATIAL_ANCHOR_NAME_SIZE_HTC - 1);
#endif
	xrName.name[XR_MAX_SPATIAL_ANCHOR_NAME_SIZE_HTC - 1] = 0;
	return xrName;
}

XrSpatialAnchorFromPersistedAnchorCreateInfoHTC FViveOpenXRAnchor::MakeSpatialAnchorFromPersistedAnchorCreateInfo(XrPersistedAnchorCollectionHTC collection, const FString persistedAnchorName, const FString anchorName)
{
	XrSpatialAnchorFromPersistedAnchorCreateInfoHTC createInfo = {};
	createInfo.type = XR_TYPE_SPATIAL_ANCHOR_FROM_PERSISTED_ANCHOR_CREATE_INFO_HTC;
	createInfo.next = nullptr;
	createInfo.persistedAnchorCollection = collection;
	createInfo.persistedAnchorName = FromFString(persistedAnchorName);
	createInfo.spatialAnchorName = FromFString(anchorName);
	return createInfo;
}

// functions for the XR_HTC_anchor_persistence extension

XrResult FViveOpenXRAnchor::AcquirePersistedAnchorCollectionAsync(XrFutureEXT* future)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	if (future == nullptr) return XR_ERROR_VALIDATION_FAILURE;
	XrPersistedAnchorCollectionAcquireInfoHTC acquireInfo = {};
	acquireInfo.type = XR_TYPE_PERSISTED_ANCHOR_COLLECTION_ACQUIRE_INFO_HTC;
	acquireInfo.next = nullptr;

	auto ret = xrAcquirePersistedAnchorCollectionAsyncHTC(m_Session, &acquireInfo, future);
	return ret;
}

XrResult FViveOpenXRAnchor::AcquirePersistedAnchorCollectionComplete(XrFutureEXT future, XrPersistedAnchorCollectionAcquireCompletionHTC* completion)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrAcquirePersistedAnchorCollectionCompleteHTC(future, completion);
}


XrResult FViveOpenXRAnchor::ReleasePersistedAnchorCollection(XrPersistedAnchorCollectionHTC collection)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrReleasePersistedAnchorCollectionHTC(collection);
}

XrResult FViveOpenXRAnchor::PersistSpatialAnchorAsync(XrPersistedAnchorCollectionHTC collection, const XrSpatialAnchorPersistInfoHTC* info, XrFutureEXT* future)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrPersistSpatialAnchorAsyncHTC(collection, info, future);
}

XrResult FViveOpenXRAnchor::PersistSpatialAnchorComplete(XrFutureEXT future, XrFutureCompletionEXT* completion)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrPersistSpatialAnchorCompleteHTC(future, completion);
}


XrResult FViveOpenXRAnchor::UnpersistSpatialAnchor(XrPersistedAnchorCollectionHTC collection, const XrSpatialAnchorNameHTC* name)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrUnpersistSpatialAnchorHTC(collection, name);
}

XrResult FViveOpenXRAnchor::EnumeratePersistedAnchorNames(XrPersistedAnchorCollectionHTC collection, uint32_t nameCapacity, uint32_t* nameCount, XrSpatialAnchorNameHTC* names)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrEnumeratePersistedAnchorNamesHTC(collection, nameCapacity, nameCount, names);
}

XrResult FViveOpenXRAnchor::CreateSpatialAnchorFromPersistedAnchorAsync(const XrSpatialAnchorFromPersistedAnchorCreateInfoHTC* info, XrFutureEXT* future)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrCreateSpatialAnchorFromPersistedAnchorAsyncHTC(m_Session, info, future);
}

XrResult FViveOpenXRAnchor::CreateSpatialAnchorFromPersistedAnchorComplete(XrFutureEXT future, XrSpatialAnchorFromPersistedAnchorCreateCompletionHTC* completion)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrCreateSpatialAnchorFromPersistedAnchorCompleteHTC(future, completion);
}

XrResult FViveOpenXRAnchor::ClearPersistedAnchors(XrPersistedAnchorCollectionHTC collection)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrClearPersistedAnchorsHTC(collection);
}

XrResult FViveOpenXRAnchor::GetPersistedAnchorProperties(XrPersistedAnchorCollectionHTC collection, XrPersistedAnchorPropertiesGetInfoHTC* properties)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrGetPersistedAnchorPropertiesHTC(collection, properties);
}

XrResult FViveOpenXRAnchor::ExportPersistedAnchor(XrPersistedAnchorCollectionHTC collection, const XrSpatialAnchorNameHTC* name, uint32_t dataCapacity, uint32_t* dataCount, char* data)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrExportPersistedAnchorHTC(collection, name, dataCapacity, dataCount, data);
}

XrResult FViveOpenXRAnchor::ImportPersistedAnchor(XrPersistedAnchorCollectionHTC collection, uint32_t dataCount, const char* data)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrImportPersistedAnchorHTC(collection, dataCount, data);
}

XrResult FViveOpenXRAnchor::GetPersistedAnchorNameFromBuffer(XrPersistedAnchorCollectionHTC collection, uint32_t bufferCount, const char* buffer, XrSpatialAnchorNameHTC* name)
{
	if (!isAnchorPersistenceSupported) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrGetPersistedAnchorNameFromBufferHTC(collection, bufferCount, buffer, name);
}

IMPLEMENT_MODULE(FViveOpenXRAnchor, ViveOpenXRAnchor)


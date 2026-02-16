// Copyright HTC Corporation. All Rights Reserved.

#pragma once

// Must include earlier than Unreal's OpenXR header
#include "ViveOpenXRWrapper.h"
#include "IOpenXRExtensionPlugin.h"
#include "OpenXRHMD.h"
#include "ViveOpenXRFuture.h"

DECLARE_LOG_CATEGORY_EXTERN(ViveOXRAnchor, Log, All);

class VIVEOPENXRANCHOR_API FViveOpenXRAnchor : public IModuleInterface, public IOpenXRExtensionPlugin, public IOpenXRCustomAnchorSupport
{
public:
	FViveOpenXRAnchor() {}
	virtual ~FViveOpenXRAnchor() {}

	static FOpenXRHMD* HMD();
	static FViveOpenXRAnchor* Instance();

	/* IModuleInterface */

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** IOpenXRExtensionPlugin implementation */

	virtual FString GetDisplayName() override;
	virtual bool GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
	virtual bool GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
	virtual const void* OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext) override;
	virtual void PostCreateSession(XrSession InSession) override;

	virtual IOpenXRCustomAnchorSupport* GetCustomAnchorSupport() override;

	virtual void OnStartARSession(class UARSessionConfig* SessionConfig) override;

	/** Stop the AR system but leave its internal state intact. */
	virtual void OnPauseARSession() override;

	/** Stop the AR system and reset its internal state; this task must succeed. */
	virtual void OnStopARSession() override;

	/** IOpenXRCustomAnchorSupport implementation */
	virtual bool OnPinComponent(class UARPin* Pin, XrSession InSession, XrSpace TrackingSpace, XrTime DisplayTime, float worldToMeterScale) override;
	virtual void OnRemovePin(class UARPin* Pin) override;
	virtual void OnUpdatePin(class UARPin* Pin, XrSession InSession, XrSpace TrackingSpace, XrTime DisplayTime, float worldToMeterScale) override;
	virtual bool IsLocalPinSaveSupported() const override;
	virtual bool ArePinsReadyToLoad() override;
	virtual void LoadARPins(XrSession InSession, TFunction<UARPin* (FName)> OnCreatePin) override;
	virtual bool SaveARPin(XrSession InSession, FName InName, UARPin* InPin) override;
	virtual void RemoveSavedARPin(XrSession InSession, FName InName) override;
	virtual void RemoveAllSavedARPins(XrSession InSession) override;

public:
	// Space functions
	XrResult LocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location);
	XrResult DestroySpace(XrSpace space);

	// Extension functions
	bool CreateSpatialAnchor(const XrSpatialAnchorCreateInfoHTC* createInfo, XrSpace* anchor);
	bool GetSpatialAnchorName(XrSpace anchor, XrSpatialAnchorNameHTC* name);

	// Persisted Anchor functions
	XrResult AcquirePersistedAnchorCollectionAsync(XrFutureEXT* future);
	XrResult AcquirePersistedAnchorCollectionComplete(XrFutureEXT future, XrPersistedAnchorCollectionAcquireCompletionHTC* completion);
	XrResult ReleasePersistedAnchorCollection(XrPersistedAnchorCollectionHTC collection);
	XrResult PersistSpatialAnchorAsync(XrPersistedAnchorCollectionHTC collection, const XrSpatialAnchorPersistInfoHTC* info, XrFutureEXT* future);
	XrResult PersistSpatialAnchorComplete(XrFutureEXT future, XrFutureCompletionEXT* completion);
	XrResult UnpersistSpatialAnchor(XrPersistedAnchorCollectionHTC collection, const XrSpatialAnchorNameHTC* name);
	XrResult EnumeratePersistedAnchorNames(XrPersistedAnchorCollectionHTC collection, uint32_t nameCapacity, uint32_t* nameCount, XrSpatialAnchorNameHTC* names);
	XrResult CreateSpatialAnchorFromPersistedAnchorAsync(const XrSpatialAnchorFromPersistedAnchorCreateInfoHTC* info, XrFutureEXT* future);
	XrResult CreateSpatialAnchorFromPersistedAnchorComplete(XrFutureEXT future, XrSpatialAnchorFromPersistedAnchorCreateCompletionHTC* completion);
	XrResult ClearPersistedAnchors(XrPersistedAnchorCollectionHTC collection);
	XrResult GetPersistedAnchorProperties(XrPersistedAnchorCollectionHTC collection, XrPersistedAnchorPropertiesGetInfoHTC* properties);
	XrResult ExportPersistedAnchor(XrPersistedAnchorCollectionHTC collection, const XrSpatialAnchorNameHTC* name, uint32_t dataCapacity, uint32_t* dataCount, char* data);
	XrResult ImportPersistedAnchor(XrPersistedAnchorCollectionHTC collection, uint32_t dataCount, const char* data);
	XrResult GetPersistedAnchorNameFromBuffer(XrPersistedAnchorCollectionHTC collection, uint32_t bufferCount, const char* buffer, XrSpatialAnchorNameHTC* name);

public:
	// Helper functions
	XrSpatialAnchorCreateInfoHTC MakeCreateInfo(const FVector& InLocation, const FQuat& InRotation, XrSpace baseSpace, FString name, float worldToMeterScale);
	bool LocateAnchor(XrSpace anchor, FRotator& rotation, FVector& translation);
	inline bool IsSupported() const { return isAnchorSupported; }
	inline bool IsPersistenceSupported() const { return isAnchorPersistenceSupported; }
	inline void* GetARCollection() { return paCollectionAR; }

	XrSpatialAnchorFromPersistedAnchorCreateInfoHTC MakeSpatialAnchorFromPersistedAnchorCreateInfo(XrPersistedAnchorCollectionHTC collection, const FString persistedAnchorName, const FString anchorName);

	static FString GenerateAnchorName(FString prefix);
	static FString ToFString(const XrSpatialAnchorNameHTC& name);
	static XrSpatialAnchorNameHTC FromFString(const FString& name);

	void CheckPersistedAnchorCollectionAR();

private:
	static FOpenXRHMD* hmd;
	static FViveOpenXRAnchor* instance;

	bool m_bEnableAnchor = false;
	bool m_bExtAnchorEnabled = false;
	bool m_bExtAnchorPersistenceEnabled = false;
	XrSession m_Session = XR_NULL_HANDLE;
	bool isAnchorSupported = false;
	bool isAnchorPersistenceSupported = false;

	XrPersistedAnchorCollectionHTC paCollectionAR = nullptr;
	XrFutureEXT pacFuture = nullptr;
	bool isARRequestedPersistedAnchorCollection = false;
	double timeSinceARStarted;

	/* OpenXR Function Ptrs */
	PFN_xrLocateSpace xrLocateSpace = nullptr;
	PFN_xrDestroySpace xrDestroySpace = nullptr;

	PFN_xrCreateSpatialAnchorHTC xrCreateSpatialAnchorHTC = nullptr;
	PFN_xrGetSpatialAnchorNameHTC xrGetSpatialAnchorNameHTC = nullptr;

	// New function pointers for persisted anchors
	PFN_xrAcquirePersistedAnchorCollectionAsyncHTC xrAcquirePersistedAnchorCollectionAsyncHTC = nullptr;
	PFN_xrAcquirePersistedAnchorCollectionCompleteHTC xrAcquirePersistedAnchorCollectionCompleteHTC = nullptr;
	PFN_xrReleasePersistedAnchorCollectionHTC xrReleasePersistedAnchorCollectionHTC = nullptr;
	PFN_xrPersistSpatialAnchorAsyncHTC xrPersistSpatialAnchorAsyncHTC = nullptr;
	PFN_xrPersistSpatialAnchorCompleteHTC xrPersistSpatialAnchorCompleteHTC = nullptr;
	PFN_xrUnpersistSpatialAnchorHTC xrUnpersistSpatialAnchorHTC = nullptr;
	PFN_xrEnumeratePersistedAnchorNamesHTC xrEnumeratePersistedAnchorNamesHTC = nullptr;
	PFN_xrCreateSpatialAnchorFromPersistedAnchorAsyncHTC xrCreateSpatialAnchorFromPersistedAnchorAsyncHTC = nullptr;
	PFN_xrCreateSpatialAnchorFromPersistedAnchorCompleteHTC xrCreateSpatialAnchorFromPersistedAnchorCompleteHTC = nullptr;
	PFN_xrClearPersistedAnchorsHTC xrClearPersistedAnchorsHTC = nullptr;
	PFN_xrGetPersistedAnchorPropertiesHTC xrGetPersistedAnchorPropertiesHTC = nullptr;
	PFN_xrExportPersistedAnchorHTC xrExportPersistedAnchorHTC = nullptr;
	PFN_xrImportPersistedAnchorHTC xrImportPersistedAnchorHTC = nullptr;
	PFN_xrGetPersistedAnchorNameFromBufferHTC xrGetPersistedAnchorNameFromBufferHTC = nullptr;
};


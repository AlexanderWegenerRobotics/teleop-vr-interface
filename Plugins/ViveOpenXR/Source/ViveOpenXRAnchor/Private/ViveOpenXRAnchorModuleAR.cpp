// Copyright HTC Corporation. All Rights Reserved.

#include "ViveOpenXRAnchorModule.h"
#include "OpenXRCore.h"
#include "Modules/ModuleManager.h"
#include "ARBlueprintLibrary.h"
#include "IOpenXRARModule.h"
#include "ARPin.h"
#include "ViveOpenXRAnchorAsyncTask.h"

IOpenXRCustomAnchorSupport* FViveOpenXRAnchor::GetCustomAnchorSupport()
{
	//UE_LOG(ViveOXRAnchor, Log, TEXT("GetCustomAnchorSupport()"));
	return this;
}

void FViveOpenXRAnchor::OnStartARSession(class UARSessionConfig* SessionConfig) {
	UE_LOG(ViveOXRAnchor, Log, TEXT("OnStartARSession()"));
	timeSinceARStarted = FPlatformTime::Seconds();
	isARRequestedPersistedAnchorCollection = true;
	CheckPersistedAnchorCollectionAR();
}

void FViveOpenXRAnchor::OnPauseARSession() {
}

void FViveOpenXRAnchor::OnStopARSession() {
	UE_LOG(ViveOXRAnchor, Log, TEXT("OnStopARSession()"));
	isARRequestedPersistedAnchorCollection = false;
	if (paCollectionAR != nullptr) {
		ReleasePersistedAnchorCollection(paCollectionAR);
		paCollectionAR = nullptr;
	}
	timeSinceARStarted = 0;
}

void FViveOpenXRAnchor::CheckPersistedAnchorCollectionAR() {
	if (paCollectionAR != nullptr)
		return;
	if (!isAnchorPersistenceSupported) {
		UE_LOG(ViveOXRAnchor, Log, TEXT("CheckPersistedAnchorCollectionAR() Anchor persistence is not supported."));
		return;
	}

	if (paCollectionAR == nullptr && isARRequestedPersistedAnchorCollection) {
		if (pacFuture == nullptr) {
			auto ret = AcquirePersistedAnchorCollectionAsync(&pacFuture);
			if (XR_SUCCEEDED(ret)) {
				UE_LOG(ViveOXRAnchor, Log, TEXT("CheckPersistedAnchorCollectionAR() Persisted anchor collection is acquiring."));
			} else {
				pacFuture = nullptr;
				isARRequestedPersistedAnchorCollection = false;
				UE_LOG(ViveOXRAnchor, Error, TEXT("CheckPersistedAnchorCollectionAR() Persisted anchor collection creation is failed."));
			}
		} else {
			bool isReady = false;
			auto futureMod = FViveOpenXRFuture::Instance();
			if (futureMod == nullptr) return;
			auto ret = futureMod->PollFuture(pacFuture, isReady);
			if (XR_FAILED(ret)) {
				pacFuture = nullptr;
				isARRequestedPersistedAnchorCollection = false;
				return;
			}

			if (isReady)
			{
				XrPersistedAnchorCollectionAcquireCompletionHTC completion;
				ret = AcquirePersistedAnchorCollectionComplete(pacFuture, &completion);
				if (XR_SUCCEEDED(completion.futureResult)) {
					UE_LOG(ViveOXRAnchor, Log, TEXT("CheckPersistedAnchorCollectionAR() PAC=%p is acquired."), completion.persistedAnchorCollection);
					paCollectionAR = completion.persistedAnchorCollection;
				}
				else
					isARRequestedPersistedAnchorCollection = false;
				pacFuture = nullptr;
			}
		}
	}
}

bool FViveOpenXRAnchor::OnPinComponent(UARPin* Pin, XrSession InSession, XrSpace TrackingSpace, XrTime DisplayTime, float worldToMeterScale)
{
	UE_LOG(ViveOXRAnchor, Log, TEXT("OnPinComponent()"));
	
	auto t = Pin->GetLocalToTrackingTransform();
	auto loc = t.GetLocation();
	auto rot = t.GetRotation();

	auto debugName = Pin->GetDebugName().ToString();

	if (debugName.IsEmpty())
		debugName = "NoName";
	// XXX We cannot create a unique anchor name and pass the created name to upper level system.  Therefore its user's responsiblity to create unique name.
	// Create unique persisted anchor name
	// auto name = GenerateAnchorName(debugName);
	XrSpatialAnchorCreateInfoHTC createInfo = MakeCreateInfo(loc, rot, TrackingSpace, debugName, worldToMeterScale);
	XrSpace anchor = 0;
	if (!CreateSpatialAnchor(&createInfo, &anchor)) return false;
	Pin->SetNativeResource(anchor);
	return true;
}

void FViveOpenXRAnchor::OnRemovePin(UARPin* Pin)
{
	XrSpace anchor = (XrSpace)Pin->GetNativeResource();
	if (xrDestroySpace == nullptr) return;
	xrDestroySpace(anchor);
	Pin->SetPinnedComponent(nullptr);
	Pin->SetNativeResource(nullptr);
}

void FViveOpenXRAnchor::OnUpdatePin(UARPin* Pin, XrSession InSession, XrSpace TrackingSpace, XrTime DisplayTime, float worldToMeterScale)
{
	FRotator rotation;
	FVector translation;
	if (!LocateAnchor((XrSpace)Pin->GetNativeResource(), rotation, translation))
		return;
	Pin->OnTransformUpdated(FTransform(rotation, translation, FVector(1, 1, 1)));
}

bool FViveOpenXRAnchor::IsLocalPinSaveSupported() const
{
	//UE_LOG(ViveOXRAnchor, Log, TEXT("IsLocalPinSaveSupported()=%d"), isARRequestedPersistedAnchorCollection && isAnchorPersistenceSupported);
	return isARRequestedPersistedAnchorCollection && isAnchorPersistenceSupported;
}

bool FViveOpenXRAnchor::ArePinsReadyToLoad()
{
	// If AR session is not started, we don't need to check persisted anchor collection.
	if (!IsLocalPinSaveSupported()) return false;

	CheckPersistedAnchorCollectionAR();
	if (paCollectionAR == nullptr) {
		UE_LOG(ViveOXRAnchor, Log, TEXT("ArePinsReadyToLoad() = false"));
		return false;
	}

	return true;
}

void FViveOpenXRAnchor::LoadARPins(XrSession InSession, TFunction<UARPin* (FName)> OnCreatePin)
{
	UE_LOG(ViveOXRAnchor, Log, TEXT("LoadARPins()"));
	if (!IsLocalPinSaveSupported()) return;

	CheckPersistedAnchorCollectionAR();
	if (paCollectionAR == nullptr) {
		UE_LOG(ViveOXRAnchor, Error, TEXT("SaveARPin() Anchor persistence is not exist. Check IsLocalPinSaveSupported first."));
		return;
	}

	XrResult ret = XR_SUCCESS;
	uint32_t count = 0;
	TArray<XrSpatialAnchorNameHTC> AnchorNames;

	// enumerate persisted anchors
	xrEnumeratePersistedAnchorNamesHTC(paCollectionAR, 0, &count, nullptr);
	if (count != 0) {
		AnchorNames.SetNum(count);
		xrEnumeratePersistedAnchorNamesHTC(paCollectionAR, count, &count, AnchorNames.GetData());
	}
	else
	{
		UE_LOG(ViveOXRAnchor, Log, TEXT("LoadARPins() No Anchor name found: %d."), count);
	}

	UE_LOG(ViveOXRAnchor, Log, TEXT("LoadARPins() EnumeratePersistedAnchorCount: %d."), count);
	
	int created = 0;
	TArray<XrFutureEXT> list;
	// Get all PersistedAnchor from CollectionHTC
	for (auto name : AnchorNames)
	{
		FString paName = ToFString(name);
		UE_LOG(ViveOXRAnchor, Log, TEXT("LoadARPins() EnumeratePersistedAnchorName: %s."), *paName);
		FString aName = paName;
		if (aName.EndsWith(TEXT("_PA")))
		{
			aName = aName.Replace(TEXT("_PA"), TEXT(""));
		}

		UE_LOG(LogTemp, Log, TEXT("LoadARPins() Make spatial anchor: %s from persisted anchor: %s"), *aName, *paName);
		XrSpatialAnchorFromPersistedAnchorCreateInfoHTC createInfo =
			MakeSpatialAnchorFromPersistedAnchorCreateInfo(paCollectionAR, paName, aName);
		XrFutureEXT future;
		ret = CreateSpatialAnchorFromPersistedAnchorAsync(&createInfo, &future);
		if (ret == XR_SUCCESS) {
			list.Add(future);
		}
	}

	while (!list.IsEmpty()) {
		TArray<XrFutureEXT> nextList;
		int N = list.Num();
		for (int i = 0; i < N; i++)
		{
			auto future = list[i];
			if (future == nullptr) continue;
			bool isReady = false;
			auto futureMod = FViveOpenXRFuture::Instance();
			if (futureMod == nullptr) return;
			ret = futureMod->PollFuture(future, isReady);
			if (XR_FAILED(ret))
			{
				UE_LOG(ViveOXRAnchor, Error, TEXT("LoadARPins() PollFuture failed with result %d."), ret);
				continue;
			}
			if (!isReady)
			{
				nextList.Add(future);
				continue;
			}

			XrSpatialAnchorFromPersistedAnchorCreateCompletionHTC completion = {
				.type = XR_TYPE_SPATIAL_ANCHOR_FROM_PERSISTED_ANCHOR_CREATE_COMPLETION_HTC,
				.next = nullptr,
				.futureResult = XR_SUCCESS,
				.anchor = 0
			};

			XrResult completeResult = CreateSpatialAnchorFromPersistedAnchorComplete(future, &completion);
			if (XR_FAILED(completeResult) || XR_FAILED(completion.futureResult))
			{
				UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to create spatial anchor from persisted anchor: completeResult=%d futureResult=%d"), completeResult, completion.futureResult);
			}
			else
			{
				UE_LOG(ViveOXRAnchor, Log, TEXT("CreateSpatialAnchorFromPersistedAnchor is success. anchor=%p.  Invoke OnCreatePin."), completion.anchor);
				auto anchor = completion.anchor;
				XrSpatialAnchorNameHTC anchorName;
				FString aName;
				if (GetSpatialAnchorName(anchor, &anchorName))
					aName = ToFString(anchorName);
				// Set user defined name to ARPin
				UARPin* pin = OnCreatePin(FName(aName));
				if (pin != nullptr) {
					pin->SetNativeResource(anchor);
					created++;
				}
			}
		}
		list = nextList;
		FPlatformProcess::Sleep(0);
	}

	UE_LOG(ViveOXRAnchor, Log, TEXT("LoadARPins() Created %d ARPin(s)."), created);
}

bool FViveOpenXRAnchor::SaveARPin(XrSession InSession, FName InName, UARPin* InPin)
{
	UE_LOG(ViveOXRAnchor, Log, TEXT("SaveARPin()"));
	if (!IsLocalPinSaveSupported()) return false;

	CheckPersistedAnchorCollectionAR();
	if (paCollectionAR == nullptr) {
		UE_LOG(ViveOXRAnchor, Error, TEXT("SaveARPin() Anchor persistence is not exist. Check IsLocalPinSaveSupported first."));
		return false;
	}

	if (InPin == nullptr || InPin->GetNativeResource() == nullptr) {
		UE_LOG(ViveOXRAnchor, Error, TEXT("SaveARPin() Invalid ARPin."));
		return false;
	}

	FString aName = InName.ToString();
	FString paName;
	if (!aName.EndsWith(TEXT("_PA")))
	{
		paName = aName + TEXT("_PA");
	}

	UE_LOG(ViveOXRAnchor, Log, TEXT("SaveARPin() aName: %s"), *aName);
	UE_LOG(ViveOXRAnchor, Log, TEXT("SaveARPin() paName: %s"), *paName);

	// XXX We cannot create a unique anchor name and pass the created name to upper level system.  Therefore its user's responsiblity to create unique name.
	// Create unique persisted anchor name
	// name = GenerateAnchorName(name);

	XrSpatialAnchorPersistInfoHTC info = {
		XR_TYPE_SPATIAL_ANCHOR_PERSIST_INFO_HTC,
		nullptr,
		(XrSpace)InPin->GetNativeResource(),
		FromFString(paName)
	};

	XrFutureEXT future;
	auto ret = PersistSpatialAnchorAsync(paCollectionAR, &info, &future);

	while (true) {
		bool isReady = false;
		auto futureMod = FViveOpenXRFuture::Instance();
		if (futureMod == nullptr) return false;
		ret = futureMod->PollFuture(future, isReady);
		if (XR_FAILED(ret))
		{
			UE_LOG(ViveOXRAnchor, Error, TEXT("SaveARPin() PersistSpatialAnchor failed when pollFuture with result %d."), ret);
			break;
		}
		else if (isReady)
		{
			XrFutureCompletionEXT completion;
			ret = PersistSpatialAnchorComplete(future, &completion);
			if (XR_FAILED(ret)) {
				UE_LOG(ViveOXRAnchor, Error, TEXT("SaveARPin() PersistSpatialAnchor failed when complete with result %d."), ret);
				return false;
			}

			if (XR_FAILED(completion.futureResult)) {
				UE_LOG(ViveOXRAnchor, Error, TEXT("SaveARPin() PersistSpatialAnchor failed with result %d."), completion.futureResult);
				return false;
			}

			UE_LOG(ViveOXRAnchor, Log, TEXT("SaveARPin() PersistSpatialAnchor success."));
			return true;
		}
	}

	return false;
}

void FViveOpenXRAnchor::RemoveSavedARPin(XrSession InSession, FName InName)
{	
	UE_LOG(ViveOXRAnchor, Log, TEXT("RemoveSavedARPin()"));
	if (!IsLocalPinSaveSupported()) return;

	CheckPersistedAnchorCollectionAR();
	if (paCollectionAR == nullptr) {
		// If supported, when ARSession started, it should be created.  Developer Should check IsLocalPinSaveSupported first.
		UE_LOG(ViveOXRAnchor, Error, TEXT("SaveARPin() Anchor persistence is not exist. Check IsLocalPinSaveSupported first."));
		return;
	}

	// Add ARPin marker
	FString aName = InName.ToString();
	FString paName = aName;
	if (!paName.EndsWith(TEXT("_PA")))
	{
		paName += TEXT("_PA");
	}
	
	UE_LOG(ViveOXRAnchor, Log, TEXT("Remove : %s from Local"), *aName);
	UE_LOG(ViveOXRAnchor, Log, TEXT("Remove : %s from Collection"), *paName);

	XrSpatialAnchorNameHTC xrName = FromFString(paName);

	// Remove SpatialAnchor from collectionHTC
	auto ret = UnpersistSpatialAnchor(paCollectionAR, &xrName);
	if (ret != XR_SUCCESS) {
		UE_LOG(ViveOXRAnchor, Error, TEXT("RemoveSavedARPin() UnpersistSpatialAnchor failed with result %d."), ret);
	}

	UE_LOG(ViveOXRAnchor, Log, TEXT("Check collection removed"));
	uint32_t nameCount = 0;
	XrSpatialAnchorNameHTC* xrNames = new XrSpatialAnchorNameHTC[nameCount];
	FMemory::Memset(xrNames, 0, sizeof(XrSpatialAnchorNameHTC) * nameCount);
	XrResult result = EnumeratePersistedAnchorNames((XrPersistedAnchorCollectionHTC)paCollectionAR, nameCount, &nameCount, xrNames);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to enumerate persisted anchor names: %d"), result);
		delete[] xrNames;
	}

	UE_LOG(ViveOXRAnchor, Log, TEXT("Find: %d in collection"), nameCount);
	for (uint32_t i = 0; i < nameCount; i++)
	{
		FString anchorName = ToFString(xrNames[i]);
		UE_LOG(ViveOXRAnchor, Log, TEXT("EnumeratePersistedAnchorNames: %s"), *anchorName);
	}
}

void FViveOpenXRAnchor::RemoveAllSavedARPins(XrSession InSession)
{
	UE_LOG(ViveOXRAnchor, Log, TEXT("RemoveAllSavedARPins()"));
	if (!IsLocalPinSaveSupported()) return;
	
	CheckPersistedAnchorCollectionAR();
	if (paCollectionAR == nullptr) {
		// If supported, when ARSession started, it should be created.  Developer Should check IsLocalPinSaveSupported first.
		UE_LOG(ViveOXRAnchor, Error, TEXT("SaveARPin() Anchor persistence is not exist. Check IsLocalPinSaveSupported first."));
		return;
	}
	auto ret = ClearPersistedAnchors(paCollectionAR);
	if (ret != XR_SUCCESS) {
		UE_LOG(ViveOXRAnchor, Error, TEXT("RemoveAllSavedARPins() ClearPersistedAnchors failed with result %d."), ret);
	}
	UE_LOG(ViveOXRAnchor, Log, TEXT("Collection :%p"), paCollectionAR);
	UE_LOG(ViveOXRAnchor, Log, TEXT("Check collection all removed"));

	// Check collection removed
	uint32_t nameCount = 0;
	XrSpatialAnchorNameHTC* xrNames = new XrSpatialAnchorNameHTC[nameCount];
	FMemory::Memset(xrNames, 0, sizeof(XrSpatialAnchorNameHTC) * nameCount);
	XrResult result = EnumeratePersistedAnchorNames((XrPersistedAnchorCollectionHTC)paCollectionAR, nameCount, &nameCount, xrNames);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to enumerate persisted anchor names: %d"), result);
		delete[] xrNames;
	}
	UE_LOG(ViveOXRAnchor, Log, TEXT("Find: %d in collection"), nameCount);
	for (uint32_t i = 0; i < nameCount; i++)
	{
		FString name = ToFString(xrNames[i]);
		UE_LOG(ViveOXRAnchor, Log, TEXT("EnumeratePersistedAnchorNames: %s"), *name);
	}
}

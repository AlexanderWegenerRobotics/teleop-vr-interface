// ViveOpenXRAnchorFunctionLibrary.cpp
// Copyright (c) 2024 HTC Corporation. All Rights Reserved.

#include "ViveOpenXRAnchorPersistence.h"
#include "ViveOpenXRAnchorModule.h"
#include "ViveOpenXRAnchorAsyncTask.h"
#include "ViveOpenXRFuture.h"
#include "ViveOpenXRAnchorAsyncTask.h"

UPersistedAnchorCollection::UPersistedAnchorCollection() : collection(XR_NULL_HANDLE) {}

UPersistedAnchorCollection* UPersistedAnchorCollection::StartAcquirePACTask(XrFutureEXT future, UObject* outer)
{
	if (!outer)
		outer = GetTransientPackage();
	UPersistedAnchorCollection* task = NewObject<UPersistedAnchorCollection>(outer);
	task->future = future;
	AddTaskToQueue(task);
	return task;
}

// After acquired, the task will be added to the task queue.  And the OnFutureComplete will be called when the future is ready.
int UPersistedAnchorCollection::OnFutureComplete()
{
	UE_LOG(ViveOXRAnchor, Log, TEXT("UPersistedAnchorCollection::OnFutureComplete"));
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return XR_ERROR_INSTANCE_LOST;

	XrPersistedAnchorCollectionAcquireCompletionHTC completion = {
		.type = XR_TYPE_PERSISTED_ANCHOR_COLLECTION_ACQUIRE_COMPLETION_HTC,
		.next = nullptr,
		.futureResult = XR_SUCCESS,
		.persistedAnchorCollection = nullptr
	};

	collection = FViveOpenXRAnchor::Instance()->GetARCollection();
	UE_LOG(ViveOXRAnchor, Log, TEXT("AcquirePersistedAnchorCollectionComplete PAC=%p"), collection);
	return 0;  // No error on execution
}

bool UPersistedAnchorCollection::IsInitialized() const {
	return IsDone();
}

bool UPersistedAnchorCollection::IsAvailable() const {
	// Must check IsInitialized first.  Therefore if collection is null, not available.
	return collection != XR_NULL_HANDLE;
}

bool UPersistedAnchorCollection::CheckPAC()
{
	if (IsDone())
		return IsAvailable();

	WaitTask();
	return IsAvailable();
}


UPersistTask* UPersistedAnchorCollection::PersistSpatialAnchor(int64 anchor, const FString& name)
{
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return nullptr;
	if (!CheckPAC()) {
		return nullptr;
	}

	XrSpatialAnchorPersistInfoHTC persistInfo = {};
	persistInfo.type = XR_TYPE_SPATIAL_ANCHOR_PERSIST_INFO_HTC;
	persistInfo.anchor = (XrSpace)anchor;
	persistInfo.persistedAnchorName = mod->FromFString(name);
	XrFutureEXT futureLocal;
	XrResult result = mod->PersistSpatialAnchorAsync((XrPersistedAnchorCollectionHTC)collection, &persistInfo, &futureLocal);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to persist spatial anchor: %d"), result);
	}

	return UPersistTask::StartPersistTask(futureLocal, anchor, name, this);
}

bool UPersistedAnchorCollection::UnpersistSpatialAnchor(const FString& name)
{
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return false;
	if (!CheckPAC()) return false;

	XrSpatialAnchorNameHTC anchorName = mod->FromFString(name);

	XrResult result = mod->UnpersistSpatialAnchor((XrPersistedAnchorCollectionHTC)collection, &anchorName);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to unpersist spatial anchor: %d"), result);
		return false;
	}

	return true;
}

bool UPersistedAnchorCollection::EnumeratePersistedAnchorNames(TArray<FString>& anchorNames)
{
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return false;
	if (!CheckPAC()) return false;

	uint32_t nameCount = 0;
	XrResult result = mod->EnumeratePersistedAnchorNames((XrPersistedAnchorCollectionHTC)collection, 0, &nameCount, nullptr);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to enumerate persisted anchor names 1: %d"), result);
		return false;
	}

	anchorNames.SetNum(0);
	if (nameCount == 0)
		return true;

	XrSpatialAnchorNameHTC* xrNames = new XrSpatialAnchorNameHTC[nameCount];
	FMemory::Memset(xrNames, 0, sizeof(XrSpatialAnchorNameHTC) * nameCount);
	result = mod->EnumeratePersistedAnchorNames((XrPersistedAnchorCollectionHTC)collection, nameCount, &nameCount, xrNames);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to enumerate persisted anchor names 2: %d"), result);
		delete[] xrNames;
		return false;
	}

	for (uint32_t i = 0; i < nameCount; i++)
	{
		FString name = mod->ToFString(xrNames[i]);
		//UE_LOG(ViveOXRAnchor, Log, TEXT("EnumeratePersistedAnchorNames: %s"), *name);
		anchorNames.Add(name);
	}

	delete[] xrNames;
	return true;
}

UFromPersistedTask* UPersistedAnchorCollection::CreateSpatialAnchorFromPersistedAnchor(const FString& persistedAnchorName, const FString& anchorName)
{
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return nullptr;
	if (!CheckPAC()) return nullptr;

	XrSpatialAnchorFromPersistedAnchorCreateInfoHTC createInfo = {};
	createInfo.type = XR_TYPE_SPATIAL_ANCHOR_FROM_PERSISTED_ANCHOR_CREATE_INFO_HTC;
	createInfo.persistedAnchorCollection = (XrPersistedAnchorCollectionHTC)collection;
	createInfo.persistedAnchorName = mod->FromFString(persistedAnchorName);
	createInfo.spatialAnchorName = mod->FromFString(anchorName);

	XrFutureEXT futureLocal;
	XrResult result = mod->CreateSpatialAnchorFromPersistedAnchorAsync(&createInfo, &futureLocal);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to create spatial anchor from persisted anchor: %d"), result);
	}

	return UFromPersistedTask::StartFromPersistedTask(futureLocal, persistedAnchorName, anchorName, this);
}

bool UPersistedAnchorCollection::ClearPersistedAnchors()
{
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return false;
	if (!CheckPAC()) return false;

	XrResult result = mod->ClearPersistedAnchors((XrPersistedAnchorCollectionHTC)collection);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to clear persisted anchors: %d"), result);
		return false;
	}

	return true;
}

UExportTask* UPersistedAnchorCollection::ExportPersistedAnchor(const FString& name, const FString& folder)
{
	return UExportTask::StartExportTask(this, name, this, folder);
}

UImportTask* UPersistedAnchorCollection::ImportPersistedAnchor(const FString& taskName, const TArray<uint8>& data, const FString& filePath)
{
	auto task = UImportTask::StartImportTask(this, data, this, filePath);
	task->SetTaskName(taskName);
	return task;
}

bool UPersistedAnchorCollection::GetPersistedAnchorNameFromBuffer(const TArray<uint8>& Data, FString& name)
{
	name = "";
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return false;
	if (!CheckPAC()) return false;

	XrSpatialAnchorNameHTC xrName{};
	auto ret = mod->GetPersistedAnchorNameFromBuffer((XrPersistedAnchorCollectionHTC)collection, Data.Num(), (char*)Data.GetData(), &xrName);
	if (XR_FAILED(ret)) {
		UE_LOG(ViveOXRAnchor, Error, TEXT("GetPersistedAnchorNameFromBuffer return fail: %d"), ret);
		return false;
	}
	name = FViveOpenXRAnchor::ToFString(xrName);
	return true;
}

void UPersistedAnchorCollection::GetMaxPersistedAnchorCount(int& maxCount)
{
	maxCount = 0;
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return;
	if (!CheckPAC()) return;

	XrPersistedAnchorPropertiesGetInfoHTC info{};
	info.type = XR_TYPE_PERSISTED_ANCHOR_PROPERTIES_GET_INFO_HTC;
	info.next = nullptr;
	info.maxPersistedAnchorCount = 0;
	auto ret = mod->GetPersistedAnchorProperties((XrPersistedAnchorCollectionHTC)collection, &info);
	if (ret == XR_SUCCESS)
		maxCount = (int)info.maxPersistedAnchorCount;
}
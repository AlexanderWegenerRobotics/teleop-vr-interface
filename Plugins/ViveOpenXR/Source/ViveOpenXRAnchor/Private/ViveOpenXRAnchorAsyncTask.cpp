// ViveOpenXRAnchorFunctionLibrary.cpp
// Copyright (c) 2024 HTC Corporation. All Rights Reserved.

#include "ViveOpenXRAnchorAsyncTask.h"
#include "ViveOpenXRAnchorModule.h"
#include "ViveOpenXRAnchorFunctionLibrary.h"
#include "ViveOpenXRAnchorPersistence.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"

UExportTask::UExportTask()
    : collectionObj(nullptr), result((int)XR_ERROR_VALIDATION_FAILURE)
{
}

UExportTask* UExportTask::StartExportTask(UPersistedAnchorCollection* collection, const FString& anchorName, UObject* outer, const FString& folderName)
{
	if (!outer) outer = GetTransientPackage();
	UExportTask* task = NewObject<UExportTask>(outer);
	task->collectionObj = collection;
	task->persistedAnchorName = anchorName;
	task->folderPath = folderName;
	AddTaskToQueue(task);
	return task;
}

void UExportTask::ExecuteTask()
{
	UE_LOG(ViveOXRAnchor, Log, TEXT("ExportPersistedAnchor %s"), *persistedAnchorName);
	XrResult ret = XR_ERROR_VALIDATION_FAILURE;
	uint32_t count = 0;
	do {
		if (collectionObj == nullptr) break;
		auto mod = FViveOpenXRAnchor::Instance();
		if (!mod) break;
		auto collection = (XrPersistedAnchorCollectionHTC)collectionObj->GetCollection();
		if (collection == XR_NULL_HANDLE) break;

		auto xrName = FViveOpenXRAnchor::FromFString(persistedAnchorName);
		//UE_LOG(ViveOXRAnchor, Log, TEXT("ExportPersistedAnchor1 %s +++"), *persistedAnchorName);
		ret = mod->ExportPersistedAnchor(collection, &xrName, 0, &count, nullptr);
		if (XR_FAILED(ret) || count == 0) {
			UE_LOG(ViveOXRAnchor, Error, TEXT("ExportPersistedAnchor1 %s failed"), *persistedAnchorName);
			break;
		}

		data.SetNum(count);
		ret = mod->ExportPersistedAnchor(collection, &xrName, count, &count, (char*)data.GetData());
	} while (false);

	// Not to expose openxr result type to function library header.
	if (XR_FAILED(ret) || count == 0) {
		data.SetNum(0);
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to export persisted anchor: %s ret=%d, count=%d"), *persistedAnchorName, ret, count);
	}
	else
	{
		// Create file name
		FString fileName = persistedAnchorName + TEXT(".bin");

		// Construct the full file path
		FString filePath = folderPath + fileName;

		// Save the data to the file
		if (FFileHelper::SaveArrayToFile(data, *filePath))
		{
			UE_LOG(ViveOXRAnchor, Log, TEXT("File saved successfully at: %s"), *filePath);
		}
		else
		{
			UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to save file at: %s"), *filePath);
		}
	}

	// Check weak again because the task take too long to process.
	result = (int32)ret;
	UE_LOG(ViveOXRAnchor, Log, TEXT("Export result: %s ret=%d, count=%d"), *persistedAnchorName, ret, count);
}

void UExportTask::GetResult(TArray<uint8>& dataOut, bool& isSuccess, int& xrResult) const
{
	dataOut = data;
	isSuccess = result == XR_SUCCESS;
	xrResult = result;
}

FString UExportTask::GetPersistedAnchorName() const
{
	return persistedAnchorName;
}

UImportTask::UImportTask()
	: collectionObj(nullptr), result((int)XR_ERROR_VALIDATION_FAILURE)
{
}

UImportTask* UImportTask::StartImportTask(UPersistedAnchorCollection* collection, const TArray<uint8>& data, UObject* outer, const FString& filePath)
{
	if (!outer) outer = GetTransientPackage();
	UImportTask* task = NewObject<UImportTask>(outer);
	task->collectionObj = collection;
	task->data = data;
	task->filePath = filePath;
	AddTaskToQueue(task);
	return task;
}

void UImportTask::SetTaskName(const FString& name)
{
	UImportTask::taskName = name;
}

FString UImportTask::GetTaskName() const
{
	return taskName;
}

void UImportTask::ExecuteTask()
{
	UE_LOG(ViveOXRAnchor, Log, TEXT("ImportPersistedAnchor: taskName=%s"), *taskName);

	// Check if the file exists
	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*filePath))
	{
		UE_LOG(ViveOXRAnchor, Warning, TEXT("File does not exist: %s"), *filePath);
	}

	XrResult ret = XR_ERROR_VALIDATION_FAILURE;
	do {
		auto mod = FViveOpenXRAnchor::Instance();
		if (!mod) break;
		if (collectionObj == nullptr) break;
		auto collection = (XrPersistedAnchorCollectionHTC)collectionObj->GetCollection();
		if (collection == XR_NULL_HANDLE) break;
		
		ret = mod->ImportPersistedAnchor(collection, data.Num(), (char*)data.GetData());
	} while (false);

	if (XR_FAILED(ret)) {
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to import persisted anchor: taskName=%s ret=%d"), *taskName, ret);
	}

	result = (int)ret;
	UE_LOG(ViveOXRAnchor, Log, TEXT("Import result: taskName=%s ret=%d"), *taskName, ret);
}

void UImportTask::GetResult(bool& isSuccess, int& xrResult) const
{
	isSuccess = result == XR_SUCCESS;
	xrResult = result;
}

UPersistTask::UPersistTask() : anchor(0), result((int)XR_ERROR_VALIDATION_FAILURE)
{
}

UPersistTask* UPersistTask::StartPersistTask(XrFutureEXT future, int64 anchor, const FString& name, UObject* outer)
{
	if (!outer) outer = GetTransientPackage();
	UPersistTask* task = NewObject<UPersistTask>(outer);
	task->future = future;
	task->name = name;
	task->anchor = anchor;
	AddTaskToQueue(task);
	return task;
}

void UPersistTask::GetResult(bool& isSuccess, int& xrResult) const
{
	isSuccess = result == XR_SUCCESS;
	xrResult = result;
}

FString UPersistTask::GetPersistedAnchorName() const
{
	return name;
}

int64 UPersistTask::GetAnchor() const
{
	return anchor;
}

int UPersistTask::OnFutureComplete()
{
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return XR_ERROR_VALIDATION_FAILURE;

	XrFutureCompletionEXT completion = {
		.type = XR_TYPE_FUTURE_COMPLETION_EXT,
		.next = nullptr
	};

	XrResult ret = mod->PersistSpatialAnchorComplete(future, &completion);
	if (XR_FAILED(ret))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to persist spatial anchor: %d"), ret);
		return ret;
	}
	result = completion.futureResult;
	return ret;  // No error on execution
}

UFromPersistedTask::UFromPersistedTask() : anchor(0), result((int)XR_ERROR_VALIDATION_FAILURE)
{
}

UFromPersistedTask* UFromPersistedTask::StartFromPersistedTask(XrFutureEXT future, const FString& paName, const FString& aName, UObject* outer)
{
	if (!outer) outer = GetTransientPackage();
	UFromPersistedTask* task = NewObject<UFromPersistedTask>(outer);
	task->future = future;
	task->paName = paName;
	task->aName = aName;
	AddTaskToQueue(task);
	return task;
}

void UFromPersistedTask::GetResult(int64& anchorOut, bool& isSuccess, int& xrResult) const
{
	anchorOut = anchor;
	isSuccess = result == XR_SUCCESS;
	xrResult = result;
}

FString UFromPersistedTask::GetPersistedAnchorName() const
{
	return paName;
}

FString UFromPersistedTask::GetAnchorName() const
{
	return aName;
}

int64 UFromPersistedTask::GetAnchor() const
{
	return anchor;
}

int UFromPersistedTask::OnFutureComplete()
{
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return XR_ERROR_VALIDATION_FAILURE;

	XrSpatialAnchorFromPersistedAnchorCreateCompletionHTC completion = {
		.type = XR_TYPE_SPATIAL_ANCHOR_FROM_PERSISTED_ANCHOR_CREATE_COMPLETION_HTC,
		.next = nullptr,
		.futureResult = XR_SUCCESS,
		.anchor = 0
	};

	XrResult cret = mod->CreateSpatialAnchorFromPersistedAnchorComplete(future, &completion);
	if (XR_FAILED(cret) || XR_FAILED(completion.futureResult))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to create spatial anchor from persisted anchor: completeResult=%d futureResult=%d"), cret, completion.futureResult);
		return cret;
	}
	anchor = (int64)completion.anchor;
	result = completion.futureResult;
	return cret;  // No error on execution
}

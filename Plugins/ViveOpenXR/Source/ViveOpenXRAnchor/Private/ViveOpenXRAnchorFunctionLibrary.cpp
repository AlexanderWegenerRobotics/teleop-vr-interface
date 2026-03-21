// ViveOpenXRAnchorFunctionLibrary.cpp
// Copyright (c) 2024 HTC Corporation. All Rights Reserved.

#include "ViveOpenXRAnchorFunctionLibrary.h"
#include "ViveOpenXRAnchorPersistence.h"
#include "ViveOpenXRAnchorAsyncTask.h"
#include "ViveOpenXRAnchorModule.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include <string>
#include <inttypes.h>

bool UViveOpenXRAnchorFunctionLibrary::IsAnchorSupported()
{
	if (!FViveOpenXRAnchor::Instance()) return false;
	return FViveOpenXRAnchor::Instance()->IsSupported();
}

bool UViveOpenXRAnchorFunctionLibrary::CreateSpatialAnchor(FVector position, FRotator rotation, FString name, int64& anchor)
{
	auto mod = FViveOpenXRAnchor::Instance();
	auto hmd = FViveOpenXRAnchor::HMD();
	if (!mod || !hmd) return false;

	float w2m = hmd->GetWorldToMetersScale();
	FQuat rot = FQuat(rotation);
	XrSpatialAnchorCreateInfoHTC createInfo = mod->MakeCreateInfo(position, rot, hmd->GetTrackingSpace(), name, w2m);
	anchor = 0;
	XrSpace space;
	if (!mod->CreateSpatialAnchor(&createInfo, &space))
		return false;
	anchor = (int64)space;
	return true;
}

bool UViveOpenXRAnchorFunctionLibrary::CreateSpatialAnchorT(FTransform transform, FString name, int64& anchor)
{
	return CreateSpatialAnchor(transform.GetLocation(), transform.GetRotation().Rotator(), name, anchor);
}

void UViveOpenXRAnchorFunctionLibrary::DestroySpatialAnchor(int64 anchor)
{
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return;
	mod->DestroySpace((XrSpace)anchor);
}

bool UViveOpenXRAnchorFunctionLibrary::LocateAnchor(int64 anchor, FTransform& transform)
{
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return false;

	FVector translation;
	FRotator rotation;
	if (!mod->LocateAnchor((XrSpace)anchor, rotation, translation))
		return false;
	transform = FTransform(rotation, translation, FVector(1, 1, 1));
	return true;
}

bool UViveOpenXRAnchorFunctionLibrary::GetSpatialAnchorName(int64 anchor, FString& name)
{
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return false;
	XrSpatialAnchorNameHTC xrName{};
	xrName.name[0] = 0;
	if (!mod->GetSpatialAnchorName((XrSpace)anchor, &xrName)) return false;
	// Make sure the name is null-terminated
	xrName.name[XR_MAX_SPATIAL_ANCHOR_NAME_SIZE_HTC - 1] = 0;
	name = FString(xrName.name);
	return true;
}

bool UViveOpenXRAnchorFunctionLibrary::IsPersistedAnchorSupported()
{
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return false;
	return mod->IsPersistenceSupported();
}

bool UViveOpenXRAnchorFunctionLibrary::AcquirePersistedAnchorCollection(UPersistedAnchorCollection*& collectionObj, UObject* outer)
{
	collectionObj = nullptr;
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return false;

	XrFutureEXT future = XR_NULL_HANDLE;
	XrResult result = mod->AcquirePersistedAnchorCollectionAsync(&future);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to acquire persisted anchor collection async: ret=%d"), result);
		return false;
	}

	if (!outer) outer = GetTransientPackage();
	collectionObj = UPersistedAnchorCollection::StartAcquirePACTask(future, outer);
	return true;
}

void UViveOpenXRAnchorFunctionLibrary::ReleasePersistedAnchorCollection(UPersistedAnchorCollection* collectionObj)
{
	auto mod = FViveOpenXRAnchor::Instance();
	if (!mod) return;
	if (collectionObj == nullptr) return;

	XrPersistedAnchorCollectionHTC collection = (XrPersistedAnchorCollectionHTC)collectionObj->GetCollection();
	if (collection == XR_NULL_HANDLE)
	{
		XrFutureEXT future = (XrFutureEXT)collectionObj->GetFuture();
		if (future != XR_NULL_HANDLE)
			collectionObj->CancelFuture();
	}

	XrResult result = mod->ReleasePersistedAnchorCollection(collection);
	if (XR_FAILED(result))
	{
		UE_LOG(ViveOXRAnchor, Error, TEXT("Failed to release persisted anchor collection: %p ret=%d"), collection, result);
	}

	collectionObj->SetReleased();
}

FString UViveOpenXRAnchorFunctionLibrary::GetExternalStorageDir()
{
	FString path = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectDir());
	//UE_LOG(ViveOXRAnchor, Log, TEXT("ProjectDir=%s"), *path);

#if PLATFORM_ANDROID
	if (true)
#else
	if (false)
#endif
	{
		FString internalPath = FPaths::Combine(TEXT("Android/data"), FApp::GetProjectName());
		//UE_LOG(ViveOXRAnchor, Log, TEXT("internalPath=%s"), *internalPath);
		if (path.Contains(internalPath)) {
			path = path.Replace(*internalPath, TEXT(""));
			path = FPaths::Combine(path, FApp::GetProjectName());
			//UE_LOG(ViveOXRAnchor, Log, TEXT("ExternalStorageDir=%s"), *path);
		}
	}
	return path;
}

bool UViveOpenXRAnchorFunctionLibrary::GetFilesInFolder(const FString& dirPath, const FString& extName, TArray<FString>& filenames) {
	IFileManager& FileManager = IFileManager::Get();
	FileManager.FindFiles(filenames, *dirPath, *extName);
	//for (int i = 0; i < filenames.Num(); i++)
	//	UE_LOG(ViveOXRAnchor, Log, TEXT("GetFilesInFolder[%d] %s"), i, *filenames[i]);
	return true;
}

bool UViveOpenXRAnchorFunctionLibrary::DeleteAnchorDirectory(const FString& dirPath)
{
	IFileManager& FileManager = IFileManager::Get();
	return FileManager.DeleteDirectory(*dirPath);
}

bool UViveOpenXRAnchorFunctionLibrary::SaveData(const TArray<unsigned char>& data, const FString& filePathname) {
	if (FFileHelper::SaveArrayToFile(data, *filePathname)) {
		UE_LOG(ViveOXRAnchor, Log, TEXT("Data is saved to %s"), *filePathname);
		return true;
	}
	else {
		UE_LOG(ViveOXRAnchor, Log, TEXT("Unable to save data to %s"), *filePathname);
		return false;
	}
}

bool UViveOpenXRAnchorFunctionLibrary::LoadData(const FString& filePathname, TArray<unsigned char>& data) {
	if (FFileHelper::LoadFileToArray(data, *filePathname)) {
		UE_LOG(ViveOXRAnchor, Log, TEXT("Load data[%d] from %s "), data.Num(), *filePathname);
		return true;
	}
	else {
		UE_LOG(ViveOXRAnchor, Log, TEXT("Unable to load data from %s"), *filePathname);
		return false;
	}
}

FString UViveOpenXRAnchorFunctionLibrary::ToString_Anchor(int64 anchor) {
	return FString::Printf(TEXT("%016" PRIX64), (uint64_t)anchor);
}

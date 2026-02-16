// ViveOpenXRAnchorFunctionLibrary.h
// Copyright (c) 2024 HTC Corporation. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ViveOpenXRFuture.h"
#include "ViveOpenXRAnchorAsyncTask.h"
#include "ViveOpenXRAnchorPersistence.generated.h"

/**
 * Blueprint function library for ViveOpenXR HTC Anchor Persistence extension.
 */
UCLASS(BlueprintType)
class VIVEOPENXRANCHOR_API UPersistedAnchorCollection : public UFutureTask
{
	GENERATED_BODY()

public:
	UPersistedAnchorCollection();

	static UPersistedAnchorCollection* StartAcquirePACTask(XrFutureEXT future, UObject* outer);

public:

	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence",
		meta = (ToolTip = "Check if the creation of PersistedAnchorCollection is done."))
		bool IsInitialized() const;

	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence",
		meta = (ToolTip = "If IsInitialized, return if PAC is available.  If not IsInitialized, always return false."))
		bool IsAvailable() const;

	/**
	  * Persist a spatial anchor with a unique name.
	  * If PersistedAnchorCollection is not initialized block until it is initialized.
	  * @param anchor The spatial anchor you want to persist.
	  * @param name The new name for the persisted anchor. Every persisted anchor should have a unique name.
	  */
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
		UPersistTask* PersistSpatialAnchor(int64 anchor, const FString& name);

	/**
	  * Unpersist a spatial anchor with the persisted anchor's name.
	  * If PersistedAnchorCollection is not initialized block until it is initialized.
	  * @param name The name of the persisted anchor you want to unpersist.
	  */
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
		bool UnpersistSpatialAnchor(const FString& name);

	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence",
		meta = (ToolTip = "Enumerate the names of persisted anchors.  Need PersistedAnchorCollection be available."))
		bool EnumeratePersistedAnchorNames(TArray<FString>& anchorNames);

	/**
	 * Create a spatial anchor from a persisted anchor.
	 * If PersistedAnchorCollection is not initialized block until it is initialized.
	 * @param pAnchorName The name of the persisted anchor you want to create from.
	 * @param anchorName The name of the new spatial anchor.
	 */
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
		UFromPersistedTask* CreateSpatialAnchorFromPersistedAnchor(const FString& pAnchorName, const FString& anchorName);

	/**
	 * Clear all persisted anchors.
	 */
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
		bool ClearPersistedAnchors();

	/**
	 * Export the persisted anchor's data.  This data could be imported by another device. This is an asynchronized function.
	 * Use the returned task to get result.
	 * If PersistedAnchorCollection is not initialized block until it is initialized.
	 * @param name The name of the persisted anchor you want to export.
	 */
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
		UExportTask* ExportPersistedAnchor(const FString& name, const FString& folder);

	/**
	 * Import the persisted anchor's data.  The data could be previous saved or sent by another device.
	 * This is an asynchronized function.  Use the returend task to get result.
	 * If PersistedAnchorCollection is not initialized block until it is initialized.
	 * @param taskName taskName are optional.  You can use it to named this task for your own management.
	 */
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
		UImportTask* ImportPersistedAnchor(const FString& taskName, const TArray<uint8>& Data, const FString& filePath);

	/**
	 * The exported data include a persisted anchor name.  After Import, the name will be in the name array
	 * of EnumeratePersistedAnchorNames.
	 * If PersistedAnchorCollection is not initialized block until it is initialized.
	 * @param Data The exported persisted anchor data in byte array.
	 * @param name the persisted anchor's name included in data.
	 */
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
		bool GetPersistedAnchorNameFromBuffer(const TArray<uint8>& Data, FString& name);

	/**
	 * Get the limit system can store.  Use Export to save more if necessary.
	 * If PersistedAnchorCollection is not initialized block until it is initialized.
	 * @param maxCount the storage limit of persisted anchors.
	 */
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
		void GetMaxPersistedAnchorCount(int& maxCount);

private:
	virtual int OnFutureComplete() override;

private:
	// Wait until the PAC is initialized.
	bool CheckPAC();

public:
	inline void* GetCollection() { return collection; }
	inline void SetReleased() { isReleased = true; collection = XR_NULL_HANDLE; }

private:
	void* collection;
	bool isReleased;
};

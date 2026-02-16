// ViveOpenXRAnchorFunctionLibrary.h
// Copyright (c) 2024 HTC Corporation. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ViveOpenXRAnchorFunctionLibrary.generated.h"

/**
 * Blueprint function library for ViveOpenXR HTC Anchor extension.
 */
UCLASS(ClassGroup = OpenXR)
class VIVEOPENXRANCHOR_API UViveOpenXRAnchorFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor",
		meta = (ToolTip = "Check if Anchor is supported by requesting SystemProperties."))
		static bool IsAnchorSupported();

	/**
	* Creates a spatial anchor with pose in the tracking space. 
	* The anchor is represented by an XrSpace. In Unreal, anchor is in the form of int64. 
	* And its pose can be tracked via xrLocateSpace. Once the anchor is no longer needed, 
	* call DestroySpatialAnchor to erase the anchor.
	* @param position The position of the anchor in VR's trakcing space.
	* @param rotation The rotation of the anchor in VR's trakcing space.
	* @param name The name of the anchor.  This name is used to identify the anchor.  Should b a unique name.
	*/
	UFUNCTION(/*BlueprintCallable,*/ Category = "ViveOpenXR|SpatialAnchor")
		static bool CreateSpatialAnchor(FVector position, FRotator rotation, FString name, int64& anchor);

	UFUNCTION(/*BlueprintCallable,*/ Category = "ViveOpenXR|SpatialAnchor",
		meta = (ToolTip = "Similar to CreateSpatialAnchor, and take a transform for input. Transform's scale is ignored."))
		static bool CreateSpatialAnchorT(FTransform transform, FString name, int64& anchor);

	UFUNCTION(/*BlueprintCallable,*/ Category = "ViveOpenXR|SpatialAnchor",
		meta = (ToolTip = "Call xrDestroySpace to erase the anchor."))
		static void DestroySpatialAnchor(int64 anchor);

	UFUNCTION(/*BlueprintCallable,*/ Category = "ViveOpenXR|SpatialAnchor",
		meta = (ToolTip = "Call xrLocateSpace to get the anchor's current pose based on tracking origin setting."))
		static bool LocateAnchor(int64 anchor, FTransform& transform);

	UFUNCTION(/*BlueprintCallable,*/ Category = "ViveOpenXR|SpatialAnchor",
		meta = (ToolTip = "Gets the name given to the anchor when it was created."))
		static bool GetSpatialAnchorName(int64 anchor, FString& name);

	// Persistence Anchor Functions
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence",
		meta = (ToolTip = "Check if Persisted Anchor is supported."))
		static bool IsPersistedAnchorSupported();

	/**
	 * Acquires a persisted anchor collection.  Use this collection object to perform all persisted anchor operations.
	 * After acquire, you still need wait the initialization done.  Check IsInitialized() to know if it's ready.
	 * Please manually call ReleasePersistedAnchorCollection to release the collection object.
	 * @param collection The acquired collection object.
	 * @param outer The owner of the collection object.  If owner is destroyed, the collection object will be forced to release.  Can be null if you can manager the life cycle well.
	 * @return True if the collection object is valid.
	 */
	UFUNCTION(/*BlueprintCallable,*/ Category = "ViveOpenXR|SpatialAnchor|Persistence")
		static bool AcquirePersistedAnchorCollection(UPersistedAnchorCollection*& collection, UObject* outer);

	UFUNCTION(/*BlueprintCallable,*/ Category = "ViveOpenXR|SpatialAnchor|Persistence",
		meta = (ToolTip = "Release the persisted anchor collection."))
		static void ReleasePersistedAnchorCollection(UPersistedAnchorCollection* collection);


public:
	// Helper functions

	/**
	 * Helper function to get the external storage path.
	 */
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
		static FString GetExternalStorageDir();

	/**
	 * Helper function to list all filenames with the extension in the folder.
	 */
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
		static bool GetFilesInFolder(const FString& dirPath, const FString& extName, TArray<FString>& filenames);

	/**
	 * Helper function to force delete directory.
	 */
	UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
		static bool DeleteAnchorDirectory(const FString& dirPath);

	/**
	 * Helper function to save a persisted spatial anchor data to file.
	 */
	UFUNCTION(/*BlueprintCallable,*/ Category = "ViveOpenXR|SpatialAnchor|Persistence")
		static bool SaveData(const TArray<uint8>& data, const FString& filePathname);

	/**
	 * Helper function to load a persisted spatial anchor data from file.
	 */
	UFUNCTION(/*BlueprintCallable,*/ Category = "ViveOpenXR|SpatialAnchor|Persistence")
		static bool LoadData(const FString& filePathname, TArray<uint8>& data);

	/** Returns Hex string of anchor */
	UFUNCTION(/*BlueprintCallable, BlueprintPure,*/ Category = "ViveOpenXR|SpatialAnchor|Persistence",
		meta = (DisplayName = "ToString (anchor)"))
		static FString ToString_Anchor(int64 anchor);
};

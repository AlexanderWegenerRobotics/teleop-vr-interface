// Copyright HTC Corporation. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ARPin.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "ViveOpenXRAnchorAsyncTask.h"
#include "ViveOpenXRAnchorAsyncAction.generated.h"

//DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FExportCompleteOutputPin, const FString&, filePath);

/**
 * @param folderPath The full path of the result exported folder.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FExportAllCompleteOutputPin, const FString&, folderPath);

//DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FImportCompleteOutputPin, UARPin*, AR_pin);

/**
 * @param AR_pins All AR Pins imported from the folder.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FImportAllCompleteOutputPin, const TArray<UARPin*>&, AR_pins);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FExportImportFailedOutputPin);

/**
 * 
 */
//UCLASS()
//class VIVEOPENXRANCHOR_API UViveOpenXRAnchorExportAsyncAction : public UBlueprintAsyncActionBase, public FTickableGameObject
//{
//	GENERATED_BODY()
//
//public:
//	UPROPERTY(BlueprintAssignable)
//	FExportCompleteOutputPin Complete;
//
//	UPROPERTY(BlueprintAssignable)
//	FExportImportFailedOutputPin Failure;
//
//private:
//	UObject* WorldContextObject;
//	UARPin* mARPin = nullptr;
//	FString mFolder = "";
//	FString mFileName = "";
//	UExportTask* task = nullptr;
//
//public:
//	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Export Saved ARPin"), Category = "ViveOpenXR|SpatialAnchor|Persistence")
//	static UViveOpenXRAnchorExportAsyncAction* ExportPersistedARPin(UObject* WorldContextObject, UARPin* AR_Pin, const FString folderName = "ARPins");
//
//	// UBlueprintAsyncActionBase interface
//	virtual void Activate() override;
//	virtual void Tick(float DeltaTime) override {};
//
//	TStatId GetStatId() const override { return TStatId(); }
//private:
//
//};

/**
 * Expory All Saved AR Pins to external storage folder.
 */
UCLASS()
class VIVEOPENXRANCHOR_API UViveOpenXRAnchorExportAllAsyncAction : public UBlueprintAsyncActionBase, public FTickableGameObject
{
	GENERATED_BODY()

public:
	
	UPROPERTY(BlueprintAssignable)
	FExportAllCompleteOutputPin Complete;

	UPROPERTY(BlueprintAssignable)
	FExportImportFailedOutputPin Failure;

private:
	UObject* WorldContextObject;
	FString mFolder = "";
	TArray<UExportTask*> tasks;

public:
	/**
	 * Export All Saved AR Pins to external storage folder.
	 * @param folderName The folder name to store exported AR Pins.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Export All Saved ARPins"), Category = "ViveOpenXR|SpatialAnchor|Persistence")
	static UViveOpenXRAnchorExportAllAsyncAction* ExportAllPersistedARPins(UObject* WorldContextObject, const FString folderName = "ARPins");

	// UBlueprintAsyncActionBase interface
	virtual void Activate() override;
	virtual void Tick(float DeltaTime) override {};

	TStatId GetStatId() const override { return TStatId(); }
private:

};

/**
 *
 */
//UCLASS()
//class VIVEOPENXRANCHOR_API UViveOpenXRAnchorImportAsyncAction : public UBlueprintAsyncActionBase, public FTickableGameObject
//{
//	GENERATED_BODY()
//
//public:
//	UPROPERTY(BlueprintAssignable)
//	FImportCompleteOutputPin Complete;
//
//	UPROPERTY(BlueprintAssignable)
//	FExportImportFailedOutputPin Failure;
//
//private:
//	UObject* WorldContextObject;
//	FString mFilepath = "";
//	TArray<uint8> mData;
//	UImportTask* task = nullptr;
//	FString anchorName;
//
//public:
//	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Import ARPin from File"), Category = "ViveOpenXR|SpatialAnchor|Persistence")
//	static UViveOpenXRAnchorImportAsyncAction* ImportPersistedARPinFromFile(UObject* WorldContextObject, const FString filePath);
//
//	//UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Import ARPin from Data"), Category = "ViveOpenXR|SpatialAnchor|Persistence")
//	//static UViveOpenXRAnchorImportAsyncAction* ImportPersistedARPinFromData(UObject* WorldContextObject, const TArray<uint8>& data);
//
//	// UBlueprintAsyncActionBase interface
//	virtual void Activate() override;
//	virtual void Tick(float DeltaTime) override {};
//
//	//virtual void Tick(float DeltaTime) override;
//	TStatId GetStatId() const override { return TStatId(); }
//private:
//
//};

/**
 *
 */
UCLASS()
class VIVEOPENXRANCHOR_API UViveOpenXRAnchorImportAllAsyncAction : public UBlueprintAsyncActionBase, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FImportAllCompleteOutputPin Complete;

	UPROPERTY(BlueprintAssignable)
	FExportImportFailedOutputPin Failure;

private:
	UObject* WorldContextObject;
	FString mFolderpath = "";
	TArray<UImportTask*> tasks;
	TArray<UARPin*> pins;

public:
	/**
	 * Import All Saved AR Pins from external storage folder.
	 * @param folderPath The full path or relative path to external storage where contains the Persisted Anchor files.
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Import ARPins from Folder"), Category = "ViveOpenXR|SpatialAnchor|Persistence")
	static UViveOpenXRAnchorImportAllAsyncAction* ImportAllPersistedARPinsFromFolder(UObject* WorldContextObject, const FString folderPath);


	// UBlueprintAsyncActionBase interface
	virtual void Activate() override;
	virtual void Tick(float DeltaTime) override {};

	TStatId GetStatId() const override { return TStatId(); }
private:

};
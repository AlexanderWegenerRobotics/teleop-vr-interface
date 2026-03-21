// ViveOpenXRAnchorFunctionLibrary.h
// Copyright (c) 2024 HTC Corporation. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Delegates/DelegateCombinations.h"
#include "HAL/CriticalSection.h"
#include "ViveOpenXRAsyncTask.h"
#include "ViveOpenXRFuture.h"
#include "ViveOpenXRFutureAsyncTask.h"
#include "ViveOpenXRAnchorAsyncTask.generated.h"

class UPersistedAnchorCollection;

UCLASS(BlueprintType)
class VIVEOPENXRANCHOR_API UPersistTask : public UFutureTask
{
    GENERATED_BODY()

public:
    UPersistTask();
    virtual ~UPersistTask() {}

    static UPersistTask* StartPersistTask(XrFutureEXT future, int64 anchor, const FString& name, UObject* outer);

    /**
     * When persist a spatial anchor, it might take a while.
     * Use the task to get the result if the anchor is really persisted.
     * The result is not reliable if task is not completed or has error.
     * @param isSuccess True if the persisted anchor is persisted successfully.
     * @param xrResult The openxr's result code.
     */
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
    void GetResult(bool& isSuccess, int& xrResult) const;

    // The name of anchor persisted.  You can use this anchor as this task's identity.
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
    FString GetPersistedAnchorName() const;

    // The anchor persisted.  You can use this anchor as this task's identity.
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
    int64 GetAnchor() const;

protected:
    virtual int OnFutureComplete() override;

private:
    int64 anchor;
    FString name;
    int32 result;
};

UCLASS(BlueprintType)
class VIVEOPENXRANCHOR_API UFromPersistedTask : public UFutureTask
{
    GENERATED_BODY()

public:
    UFromPersistedTask();
    virtual ~UFromPersistedTask() {}

    static UFromPersistedTask* StartFromPersistedTask(XrFutureEXT future, const FString& paName, const FString& aName, UObject* outer);

    /**
     * When create a spatial anchor from persisted anchor, it might take a while.
     * Use the task to check if the persisted anchor can be really found and be located.
     * The result is not reliable if task is not completed or has error.
     * @param anchor the created spatial anchor
     * @param isSuccess True if the persisted anchor is found and located.
     * @param xrResult The openxr's result code.
     */
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
    void GetResult(int64& anchor, bool& isSuccess, int& xrResult) const;

    // The persisted anchor's name used to create spatial anchor.
    // You can use the name as this task's identity.
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
    FString GetPersistedAnchorName() const;

    // The name used to create spatial anchor.
    // You can use the name as this task's identity.
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
    FString GetAnchorName() const;

    // The created anchor.  it is the reasult of this task.
    // The result is not reliable if task is not completed or has error.
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
    int64 GetAnchor() const;

protected:
    virtual int OnFutureComplete() override;

private:
    int64 anchor;
    FString paName;
    FString aName;
    int32 result;
};

UCLASS(BlueprintType)
class VIVEOPENXRANCHOR_API UExportTask : public UBaseTask
{
    GENERATED_BODY()

public:
    UExportTask();

    static UExportTask* StartExportTask(UPersistedAnchorCollection* collection, const FString& name, UObject* outer, const FString& folderName);

    /**
     * Exported persisted anchor's data. Should only get result iftask is completed. 
     * Because the result will update by another thread, use impure function let you known the result may change over time.
     * @param data The exported persisted anchor data.
     * @param isSuccess True if the persisted anchor is exported successfully.
     * @param xrResult The openxr's result code.
     */
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
    void GetResult(TArray<uint8>& data, bool& isSuccess, int& xrResult) const;

    // The exported persisted anchor name
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
    FString GetPersistedAnchorName() const;

protected:
    virtual void ExecuteTask() override;

private:
    UPersistedAnchorCollection* collectionObj;
    FString folderPath;
    FString persistedAnchorName;
    int32 result;
    TArray<uint8> data;
};

UCLASS(BlueprintType)
class VIVEOPENXRANCHOR_API UImportTask : public UBaseTask
{
    GENERATED_BODY()

public:
    UImportTask();

    static UImportTask* StartImportTask(UPersistedAnchorCollection* collection, const TArray<uint8>& data, UObject* outer, const FString& filePath);

    /**
     * Task name is optinal.  Use it to manager yuour tasks.
     */
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
    void SetTaskName(const FString& name);

    /**
     * Task name is optinal.  Use it to manager yuour tasks.
     */
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
    FString GetTaskName() const;

    /**
     * Check if import is successfuly.  Should only get result if task is completed.
     * @param isSuccess True if the persisted anchor is exported successfully.
     * @param xrResult The openxr's result code.
     */
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|SpatialAnchor|Persistence")
    void GetResult(bool& isSuccess, int& xrResult) const;

protected:
    virtual void ExecuteTask() override;

    UPersistedAnchorCollection* collectionObj;
    FString filePath;
    int32 result;
    FString taskName;
    TArray<uint8> data;
};
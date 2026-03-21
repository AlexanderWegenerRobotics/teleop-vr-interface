// Copyright HTC Corporation. All Rights Reserved.

#include "ViveOpenXRAnchorAsyncAction.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "ARBlueprintLibrary.h"
#include "ViveOpenXRAnchorModule.h"
#include "ViveOpenXRAnchorFunctionLibrary.h"
#include "ViveOpenXRAnchorPersistence.h"
#include "Kismet/BlueprintPathsLibrary.h"

/*
UViveOpenXRAnchorExportAsyncAction* UViveOpenXRAnchorExportAsyncAction::ExportPersistedARPin(UObject* WorldContextObject, UARPin* AR_Pin, const FString folderName)
{
    UViveOpenXRAnchorExportAsyncAction* BlueprintNode = NewObject<UViveOpenXRAnchorExportAsyncAction>(WorldContextObject);
    BlueprintNode->WorldContextObject = WorldContextObject;
    BlueprintNode->mARPin = AR_Pin;
    BlueprintNode->mFolder = folderName;
    // Register with the game instance to avoid being garbage collected
    BlueprintNode->RegisterWithGameInstance(WorldContextObject);
    return BlueprintNode;
}

void UViveOpenXRAnchorExportAsyncAction::Activate()
{
    UE_LOG(ViveOXRAnchor, Log, TEXT("ExportPersistedARPin start"));
    if (mFileName == "") mFileName = mARPin->GetDebugName().ToString();

    // check ar pin saved(persisted) or not
    // if not, persisted first. Call UARBlueprintLibrary::SaveARPinToLocalStore(FName InSaveName, UARPin* InPin)
    UARBlueprintLibrary::SaveARPinToLocalStore(mARPin->GetDebugName(), mARPin);

    // export persisted anchor data
    auto mod = FViveOpenXRAnchor::Instance();
    if (!mod) 
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorExportAsyncAction] Failed to get FViveOpenXRAnchor instance."));
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    XrFutureEXT future = XR_NULL_HANDLE;
    XrResult result = mod->AcquirePersistedAnchorCollectionAsync(&future);
    if (XR_FAILED(result))
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorExportAsyncAction] Failed to acquire persisted anchor collection async: ret=%d"), result);
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    UPersistedAnchorCollection* PACollection = UPersistedAnchorCollection::StartAcquirePACTask(future, GetTransientPackage());
    if (PACollection == nullptr)
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorExportAsyncAction] Failed to StartAcquirePACTask."), result);
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    PACollection->WaitFuture();

    //task = PACollection->ExportPersistedAnchor(mFileName);

    if (task == nullptr)
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorExportAsyncAction] Failed to ExportPersistedAnchor."), result);
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    task->WaitTask();

    // Save to external storage
    TArray<uint8> exported_data;
    bool isSuccess;
    XrResult xrResult;
    task->GetResult(exported_data, isSuccess, (int&)xrResult);
    if (!isSuccess)
    {
        task->MarkAsGarbage();

        // ToDo print failure XrResult;
        UE_LOG(ViveOXRAnchor, Log, TEXT("ExportPersistedARPin XrResult failed "));
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    FString anchorName = task->GetPersistedAnchorName();
    FString anchorFileName = UBlueprintPathsLibrary::MakeValidFileName(anchorName, "_");
    anchorFileName = UBlueprintPathsLibrary::SetExtension(anchorFileName, "pa");
    FString dir = UViveOpenXRAnchorFunctionLibrary::GetExternalStorageDir();
    FString saved_filepath = dir + mFolder + "/" + anchorFileName;

    task->MarkAsGarbage();

    UE_LOG(ViveOXRAnchor, Log, TEXT("ExportPersistedARPin end"));

    if (FFileHelper::SaveArrayToFile(exported_data, *saved_filepath))
    {
        // call complete when done
        UE_LOG(ViveOXRAnchor, Log, TEXT("ExportPersistedARPin success"));
        Complete.Broadcast(saved_filepath);
        SetReadyToDestroy(); // garbage collected now that we're done
    }
    else
    {
        UE_LOG(ViveOXRAnchor, Log, TEXT("ExportPersistedARPin failed"));
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }
}
*/

UViveOpenXRAnchorExportAllAsyncAction* UViveOpenXRAnchorExportAllAsyncAction::ExportAllPersistedARPins(UObject* WorldContextObject, const FString folderName)
{
    UViveOpenXRAnchorExportAllAsyncAction* BlueprintNode = NewObject<UViveOpenXRAnchorExportAllAsyncAction>(WorldContextObject);
    BlueprintNode->WorldContextObject = WorldContextObject;
    BlueprintNode->mFolder = folderName;

    // Register with the game instance to avoid being garbage collected
    BlueprintNode->RegisterWithGameInstance(WorldContextObject);
    return BlueprintNode;
}

void UViveOpenXRAnchorExportAllAsyncAction::Activate()
{
    tasks.Empty();

    FString dir = UViveOpenXRAnchorFunctionLibrary::GetExternalStorageDir();
    FString saved_folder = dir + mFolder;

    // Ensure the folder path is correctly formatted with a trailing slash
    FString FixedFolderPath = saved_folder;
    if (!FixedFolderPath.EndsWith(TEXT("/")))
    {
        FixedFolderPath += TEXT("/");
    }

    // Check if the directory exists
    if (!IFileManager::Get().DirectoryExists(*FixedFolderPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Directory does not exist: %s"), *FixedFolderPath);

        // Attempt to create the directory
        bool bDirectoryCreated = IFileManager::Get().MakeDirectory(*FixedFolderPath, true);

        if (bDirectoryCreated)
        {
            UE_LOG(LogTemp, Log, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Directory created successfully: %s"), *FixedFolderPath);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Failed to create directory: %s"), *FixedFolderPath);
            Failure.Broadcast();
            SetReadyToDestroy(); // garbage collected now that we're done
        }
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Directory already exists: %s"), *FixedFolderPath);
    }

    if (saved_folder.IsEmpty())
    {
        UE_LOG(LogTemp, Log, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Folder path is empty."));
    }
    else
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Failed Export PersistedARPins because directory is not empty."));
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done

        // Find all files in the specified folder
        /*
        TArray<FString> FoundFiles;
        IFileManager& FileManager = IFileManager::Get();
        FileManager.FindFiles(FoundFiles, *FixedFolderPath, TEXT("*"));  // Use "*" to find all files
        UE_LOG(LogTemp, Log, TEXT("Find all files under: %s"), *FixedFolderPath);
        UE_LOG(LogTemp, Log, TEXT("FoundFiles: %d"), FoundFiles.Num());
        // Delete each file found in the folder
        for (const FString& FileName : FoundFiles)
        {
            FString FullFilePath = FixedFolderPath + FileName;
            bool bDeleted = FileManager.Delete(*FullFilePath);

            if (bDeleted)
            {
                UE_LOG(LogTemp, Log, TEXT("Deleted file: %s"), *FullFilePath);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Failed to delete file: %s"), *FullFilePath);
            }
        }

        UE_LOG(LogTemp, Log, TEXT("Successfully deleted all files in folder: %s"), *FixedFolderPath);
        */
    }

    XrResult result;
    auto mod = FViveOpenXRAnchor::Instance();

    if (!mod)
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Failed to get FViveOpenXRAnchor instance."));
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    XrFutureEXT future = XR_NULL_HANDLE;
    result = mod->AcquirePersistedAnchorCollectionAsync(&future);
    if (XR_FAILED(result))
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Failed to acquire persisted anchor collection async: ret = %d"), result);
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    UPersistedAnchorCollection* PACollection = UPersistedAnchorCollection::StartAcquirePACTask(future, GetTransientPackage());
    if (PACollection == nullptr)
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Failed to StartAcquirePACTask."), result);
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    auto pins = UARBlueprintLibrary::GetAllPins();
    UE_LOG(LogTemp, Log, TEXT("pins: %d"), pins.Num());

    for (auto pin : pins)
    {

        FString paName = pin->GetDebugName().ToString();
        if (!paName.EndsWith(TEXT("_PA")))
        {
            paName += TEXT("_PA");
        }
        UE_LOG(ViveOXRAnchor, Log, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Export AR Pin Debug Name: [%s], Persisted Anchor Name: [%s]"), *pin->GetDebugName().ToString(), *paName);

        // export persisted anchor data
        UExportTask* task = PACollection->ExportPersistedAnchor(paName, FixedFolderPath);
        if (task)
        {
            tasks.Add(task);
        }
        else
        {
            UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Fail to export AR Pin: %s"), *pin->GetDebugName().ToString());
        }
    }

    // wait for all task 
    for (auto task : tasks)
    {
        task->WaitTask();
    }

    for (auto task : tasks)
    {
        // Todo check success or  failure
        TArray<uint8> exported_data;
        bool isSuccess;
        XrResult xrResult;
        task->GetResult(exported_data, isSuccess, (int&)xrResult);
        if (!isSuccess)
        {
            UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Fail to export AR Pin: %s"), *task->GetPersistedAnchorName());
        }
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Exported AR Pin: %s"), *task->GetPersistedAnchorName());
        task->MarkAsGarbage();
    }

    UE_LOG(ViveOXRAnchor, Log, TEXT("[UViveOpenXRAnchorExportAllAsyncAction] Exported all AR Pins to folder: %s"), *FixedFolderPath);
    Complete.Broadcast(FixedFolderPath);

    SetReadyToDestroy(); // garbage collected now that we're done
}

/*
UViveOpenXRAnchorImportAsyncAction* UViveOpenXRAnchorImportAsyncAction::ImportPersistedARPinFromFile(UObject* WorldContextObject, const FString filePath)
{
    UViveOpenXRAnchorImportAsyncAction* BlueprintNode = NewObject<UViveOpenXRAnchorImportAsyncAction>(WorldContextObject);
    BlueprintNode->WorldContextObject = WorldContextObject;
    BlueprintNode->mFilepath = filePath;
    // Register with the game instance to avoid being garbage collected
    BlueprintNode->RegisterWithGameInstance(WorldContextObject);
    return BlueprintNode;
}

void UViveOpenXRAnchorImportAsyncAction::Activate()
{
    if (mFilepath == "")
    {
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
        return;
    }

    // load file
    TArray<uint8> data;
    if (!FFileHelper::LoadFileToArray(data, *mFilepath))
    {
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
        return;
    }

    auto mod = FViveOpenXRAnchor::Instance();
    if (!mod)
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorImportAsyncAction] Failed to get FViveOpenXRAnchor instance."));
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    XrFutureEXT future = XR_NULL_HANDLE;
    XrResult result = mod->AcquirePersistedAnchorCollectionAsync(&future);
    if (XR_FAILED(result))
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorImportAsyncAction] Failed to acquire persisted anchor collection async: ret=%d"), result);
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    UPersistedAnchorCollection* PACollection = UPersistedAnchorCollection::StartAcquirePACTask(future, GetTransientPackage());
    if (PACollection == nullptr)
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorImportAsyncAction] Failed to StartAcquirePACTask."), result);
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    // import persisted anchor data
    anchorName = UBlueprintPathsLibrary::GetBaseFilename(mFilepath);
    // task = PACollection->ImportPersistedAnchor(anchorName, data);

    if (task)
    {
        task->WaitTask();

        bool isSuccess;
        XrResult xrResult;
        task->GetResult(isSuccess, (int&)xrResult);


        if (!isSuccess)
        {
            task->MarkAsGarbage();
            Failure.Broadcast();
            SetReadyToDestroy(); // garbage collected now that we're done
            return;
        }

        // Create anchor from loaded persisted anchor

        // call complete when done 
        // Complete.Broadcast(ARPin);
        // SetReadyToDestroy(); // garbage collected now that we're done
    }

}
*/

UViveOpenXRAnchorImportAllAsyncAction* UViveOpenXRAnchorImportAllAsyncAction::ImportAllPersistedARPinsFromFolder(UObject* WorldContextObject, const FString folderPath)
{
    UViveOpenXRAnchorImportAllAsyncAction* BlueprintNode = NewObject<UViveOpenXRAnchorImportAllAsyncAction>(WorldContextObject);
    BlueprintNode->WorldContextObject = WorldContextObject;
    BlueprintNode->mFolderpath = folderPath;
    // Register with the game instance to avoid being garbage collected
    BlueprintNode->RegisterWithGameInstance(WorldContextObject);
    return BlueprintNode;
}

void UViveOpenXRAnchorImportAllAsyncAction::Activate()
{
    // Ensure the folder path is correctly formatted with a trailing slash
    if (!mFolderpath.EndsWith(TEXT("/")))
    {
        mFolderpath += TEXT("/");
    }

    // If path is not full path, combine with external storage path.
    FString saved_folder = mFolderpath;
    if (!IFileManager::Get().DirectoryExists(*mFolderpath))
    {
        FString externalDir = UViveOpenXRAnchorFunctionLibrary::GetExternalStorageDir();
        saved_folder = externalDir + mFolderpath;
    }

    FString FixedFolderPath = saved_folder;

    // Check if the directory exists
    if (!IFileManager::Get().DirectoryExists(*FixedFolderPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] Directory does not exist: %s"), *FixedFolderPath);
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    // Check if folder path is empty
    if (saved_folder.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] Folder path is empty."));
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    // Find all files in the folder
    TArray<FString> FoundFiles;
    IFileManager& FileManager = IFileManager::Get();
    FileManager.FindFiles(FoundFiles, *FixedFolderPath, TEXT("*.bin"));  // Use "*" to find all files

    UE_LOG(LogTemp, Log, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] Import Found: %d"), FoundFiles.Num());

    auto mod = FViveOpenXRAnchor::Instance();
    if (!mod)
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] Failed to get FViveOpenXRAnchor instance."));
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    XrFutureEXT future = XR_NULL_HANDLE;
    XrResult result = mod->AcquirePersistedAnchorCollectionAsync(&future);
    if (XR_FAILED(result))
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] Failed to acquire persisted anchor collection async: ret=%d"), result);
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    UPersistedAnchorCollection* PACollection = UPersistedAnchorCollection::StartAcquirePACTask(future, GetTransientPackage());
    if (PACollection == nullptr)
    {
        UE_LOG(ViveOXRAnchor, Error, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] Failed to StartAcquirePACTask."), result);
        Failure.Broadcast();
        SetReadyToDestroy(); // garbage collected now that we're done
    }

    TArray<FString> ExistAnchorNames = TArray<FString>();
    UE_LOG(ViveOXRAnchor, Log, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] Check anchor names"));
    PACollection->EnumeratePersistedAnchorNames(ExistAnchorNames);
    for (FString str : ExistAnchorNames)
    {
        UE_LOG(ViveOXRAnchor, Log, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] Current exists persisted anchor name: %s"), *str);
    }

    TArray<FString> filenames;
    FString ExtName = TEXT("*.bin");
    UViveOpenXRAnchorFunctionLibrary::GetFilesInFolder(FixedFolderPath, ExtName, filenames);
    UE_LOG(LogTemp, Log, TEXT("filenames: %d"), filenames.Num());

    UE_LOG(ViveOXRAnchor, Log, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] Import all AR Pins from folder: %s"), *FixedFolderPath);

    for (FString filename : filenames)
    {
        FString CheckImportAnchorName = filename;
        CheckImportAnchorName = CheckImportAnchorName.Replace(TEXT(".bin"), TEXT(""));  // Removes ".bin"
        UE_LOG(LogTemp, Log, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] CheckImportAnchorName: %s"), *CheckImportAnchorName);

        if (!ExistAnchorNames.Contains(CheckImportAnchorName))
        {
            TArray<uint8> data;
            FString filePath = FixedFolderPath + filename;
            if (FFileHelper::LoadFileToArray(data, *filePath))
            {
                UE_LOG(ViveOXRAnchor, Log, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] Import AR Pin from file: %s"), *filePath);
                auto task = PACollection->ImportPersistedAnchor(filename, data, filePath);
                if (task)
                {
                    tasks.Add(task);
                }
                else
                {
                    UE_LOG(ViveOXRAnchor, Log, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] Fail to import AR Pin from file: %s"), *filePath);
                }
            }
        }
        else
        {
            UE_LOG(ViveOXRAnchor, Log, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] ImportAnchor: %s already exists in collection"), *CheckImportAnchorName);
            continue;
        }
    }

    for (auto task : tasks)
    {
        task->WaitTask();
    }

    UE_LOG(ViveOXRAnchor, Log, TEXT("[UViveOpenXRAnchorImportAllAsyncAction] Load all AR Pins from local store."));
    auto ARPinsMap = UARBlueprintLibrary::LoadARPinsFromLocalStore();
    ARPinsMap.GenerateValueArray(pins);
    Complete.Broadcast(pins);
    SetReadyToDestroy(); // garbage collected now that we're done
}


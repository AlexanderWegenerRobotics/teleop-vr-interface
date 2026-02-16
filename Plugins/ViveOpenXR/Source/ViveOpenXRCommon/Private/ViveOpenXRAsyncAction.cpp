// Copyright HTC Corporation. All Rights Reserved.

#include "ViveOpenXRAsyncAction.h"

UViveOpenXRAsyncAction* UViveOpenXRAsyncAction::AwaitAsyncTask(UObject* WorldContextObject, UBaseTask* task)
{
    UViveOpenXRAsyncAction* BlueprintNode = NewObject<UViveOpenXRAsyncAction>(WorldContextObject);
    BlueprintNode->WorldContextObject = WorldContextObject;
    BlueprintNode->task = task;
    // Register with the game instance to avoid being garbage collected
    BlueprintNode->RegisterWithGameInstance(WorldContextObject);
    return BlueprintNode;
}

void UViveOpenXRAsyncAction::Activate()
{
    if (task != nullptr)
        task->WaitTask();
    Complete.Broadcast();
    SetReadyToDestroy(); // garbage collected now that we're done
}
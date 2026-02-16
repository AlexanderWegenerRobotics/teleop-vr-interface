// ViveOpenXRAnchorFunctionLibrary.cpp
// Copyright (c) 2024 HTC Corporation. All Rights Reserved.

#include "ViveOpenXRFutureAsyncTask.h"
#include "ViveOpenXRFuture.h"

UFutureTask::UFutureTask()
{
}

bool UFutureTask::HasError() const
{
	return FFutureObject::HasError();
}

int UFutureTask::GetPollResult() const
{
	return FFutureObject::GetPollResult();
}

int UFutureTask::GetCompleteResult() const
{
	return FFutureObject::GetCompleteResult();
}

void UFutureTask::BeginDestroy()
{
    // Check if the future is still pending and cancel it
    if (!IsDone())
    {
        UE_LOG(ViveOXRFuture, Log, TEXT("UFutureTask: Cancelling future before destruction."));
        CancelFuture();  // This cancels the future if it's still running
    }

    // Call the base class implementation to complete destruction
    Super::BeginDestroy();
}

// The UBaseTask will call this function to wait for the future to be completed.
// In WaitFuture, the FFutureObject will do the future poll and complete.
// After ExecuteTask is done, the FFutureTask's IsDone will be set to true, and UBaseTask's isComplete will be set to true.
void UFutureTask::ExecuteTask()
{
	// In WaitFuture, OnFutureComplete will be invoked.
	// All derived classes should implement OnFutureComplete.
	// All derived classes should not call WaitFuture and CheckComplition themselves because the UBaskTask will do it.
	WaitFuture();
}
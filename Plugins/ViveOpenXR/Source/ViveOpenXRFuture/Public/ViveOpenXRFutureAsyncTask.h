// Copyright HTC Corporation. All Rights Reserved.
#pragma once

#include "ViveOpenXRAsyncTask.h"
#include "ViveOpenXRFuture.h"
#include "ViveOpenXRFutureAsyncTask.generated.h"

/**
 * This FutureTask is used to run poll and complete of XrFutureEXT.
 * Features use future to do async task can inherit this class.
 * Features should implement their own create start task function.
 * Features should implement OnFutureComplete() to make their own complete function.
 * Features should not call WaitFuture and CheckComplition themselves because the UBaskTask will do it after start task.
 * In blueprint, the task should be kept to check result.
 * After complete, set null to future.  Only result will keep.
 * For example:
 * UPersistTask* UPersistTask::StartPersistTask(XrFutureEXT future, int64 anchor, const FString& name, UObject* outer)
 * {
 *   UPersistTask* task = NewObject<UPersistTask>(outer);
 *   task->future = future;  // Set the future to task
 *   task->name = name;  // The task's identity
 *   AddTaskToQueue(task);  // Run the task in Queue
 *   return task;
 * }
 */
UCLASS(BlueprintType)
class VIVEOPENXRFUTURE_API UFutureTask : public UBaseTask, public FFutureObject
{
    GENERATED_BODY()

protected:
    UFutureTask();

public:
    virtual ~UFutureTask() {}

    /**
      * Check if the task has error.  If task has error, beware not to use the result.
      * @return True if the task has error.
      */
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|Common|Async")
    bool HasError() const;

    /**
      * If has error, check the return value of last xrPollFutureEXT.  If no error, return XR_SUCCESS(0).
      * @return The error code when polling the future.
      */
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|Common|Async")
    int GetPollResult() const;

    /**
      * If has error, but not from xrPullFutureEXT, check the return value of Complete.
      * If no error, return XR_SUCCESS(0).
      * @return The error code when completing the future.
      */
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|Common|Async")
    int GetCompleteResult() const;

    virtual void BeginDestroy() override;

protected:
    // You can override this Task
    virtual void ExecuteTask() override;

    //virtual void WaitTask() override;

    // Implement it according to your feature
    //virtual int OnFutureComplete() override {}

private:
    FFutureObject* futureObj;
};
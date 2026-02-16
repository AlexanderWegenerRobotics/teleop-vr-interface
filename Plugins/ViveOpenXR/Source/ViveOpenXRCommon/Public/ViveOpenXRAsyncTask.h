// Copyright (c) 2024 HTC Corporation. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "UObject/NoExportTypes.h"
#include "Delegates/DelegateCombinations.h"
#include "HAL/CriticalSection.h"
#include "ViveOpenXRAsyncTask.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(ViveOXRAsyncTask, Log, All);

/**
 * This BaskTask is used to run any async task.  Implement your task by implement ExecuteTask().
 * In blueprint, the task should be kept to check result.
 * You should implement your own StartTask function.
 * After complete, method for result should be implemented.
 * For example:
 * UYourTask* UYourTask::StartYourTask(params..., UObject* Outer)
 * {
 *   UYourTask* task = NewObject<UYourTask>(Outer);
 *   task->params = parms
 *   AddTaskToQueue(task);  // Run the task in Queue
 *   return task;
 * }
 */
UCLASS(BlueprintType)
class VIVEOPENXRCOMMON_API UBaseTask : public UObject
{
    GENERATED_BODY()

public:
    UBaseTask();
    virtual ~UBaseTask() {}

    // Implement your own task here.  Run in a background thread.
    virtual void ExecuteTask() PURE_VIRTUAL(UBaseTask::ExecuteTask, );

    // Default WaitTask will use while loop to check isComplete, and use Sleep to wait.
    // Should not call WaitTask in ExecuteTask.
    virtual void WaitTask();

    /**
     * Check the async exporting task is finished. Before you get the result, check if task is completed.
     * Because the result will update by another thread, use impure function let you known the result may change over time.
     * @return True if the async task is finished.  It does not mean the result is successful.
     */
    UFUNCTION(BlueprintCallable, Category = "ViveOpenXR|Common|Async")
    bool IsCompleted() const { return isCompleted; }

    // Start to procee the task.
    static void AddTaskToQueue(TWeakObjectPtr<UBaseTask> Task);

protected:
    static void ProcessTasks();

    static TQueue<TWeakObjectPtr<UBaseTask>> TaskQueue;
    static bool bIsProcessingTasks;
    static FCriticalSection QueueCriticalSection;

private:
    bool isCompleted;
};

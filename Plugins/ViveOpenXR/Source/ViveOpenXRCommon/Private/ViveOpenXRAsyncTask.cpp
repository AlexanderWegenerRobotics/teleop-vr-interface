// Copyright (c) 2024 HTC Corporation. All Rights Reserved.
#include "ViveOpenXRAsyncTask.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "OpenXRCore.h"

DEFINE_LOG_CATEGORY(ViveOXRAsyncTask);

TQueue<TWeakObjectPtr<UBaseTask>> UBaseTask::TaskQueue;
bool UBaseTask::bIsProcessingTasks = false;
FCriticalSection UBaseTask::QueueCriticalSection;

UBaseTask::UBaseTask() : isCompleted(false)
{
}

// Will called from ViveOpenXRAsyncAction::Activate
void UBaseTask::WaitTask()
{
	//UE_LOG(ViveOXRAsyncTask, Log, TEXT("WaitTask+"));
	// Should implement your own method to wait complete.
	while (!isCompleted) {
		//UE_LOG(ViveOXRAsyncTask, Log, TEXT("WaitTask~"));
		FPlatformProcess::Sleep(0.005f);
	}
	//UE_LOG(ViveOXRAsyncTask, Log, TEXT("WaitTask-"));
}

void UBaseTask::AddTaskToQueue(TWeakObjectPtr<UBaseTask> Task)
{
	//UE_LOG(ViveOXRAsyncTask, Log, TEXT("AddTaskToQueue+"));
	FScopeLock Lock(&QueueCriticalSection);
	TaskQueue.Enqueue(Task);

	if (!bIsProcessingTasks)
	{
		bIsProcessingTasks = true;
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, ProcessTasks);
	}
	//UE_LOG(ViveOXRAsyncTask, Log, TEXT("AddTaskToQueue-"));
}

void UBaseTask::ProcessTasks()
{
	//UE_LOG(ViveOXRAsyncTask, Log, TEXT("ProcessTasks+"));
	TWeakObjectPtr<UBaseTask> Task;
	while (true)
	{
		{
			FScopeLock Lock(&QueueCriticalSection);
			if (!TaskQueue.Dequeue(Task))
			{
				//UE_LOG(ViveOXRAsyncTask, Log, TEXT("ProcessTasks Queue is empty"));
				bIsProcessingTasks = false;
				break;
			}
		}

		if (Task.IsValid())
		{
			//UE_LOG(ViveOXRAsyncTask, Log, TEXT("ProcessTasks Task execute+"));
			Task->ExecuteTask();
			Task->isCompleted = true;
			//UE_LOG(ViveOXRAsyncTask, Log, TEXT("ProcessTasks Task execute-"));
		}
	}
	//UE_LOG(ViveOXRAsyncTask, Log, TEXT("ProcessTasks-"));
}
// Copyright HTC Corporation. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "ViveOpenXRAsyncTask.h"
#include "ViveOpenXRAsyncAction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTaskCompleteOutputPin);

/**
 * 
 */
UCLASS()
class VIVEOPENXRCOMMON_API UViveOpenXRAsyncAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FTaskCompleteOutputPin Complete;

private:
	UObject* WorldContextObject;
	UBaseTask* task;

public:
	// Wait unitl BaseTask is completed without block main thread.
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "ViveOpenXR|Common|Async")
	static UViveOpenXRAsyncAction* AwaitAsyncTask(UObject* WorldContextObject, UBaseTask* task);

	// UBlueprintAsyncActionBase interface
	virtual void Activate() override;
};
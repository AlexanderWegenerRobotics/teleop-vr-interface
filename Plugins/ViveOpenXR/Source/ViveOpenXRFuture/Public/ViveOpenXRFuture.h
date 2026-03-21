// Copyright HTC Corporation. All Rights Reserved.
#pragma once

#include "ViveOpenXRWrapper.h"
#include "IOpenXRExtensionPlugin.h"
#include "OpenXRHMD.h"

DECLARE_LOG_CATEGORY_EXTERN(ViveOXRFuture, Log, All);

class VIVEOPENXRFUTURE_API FViveOpenXRFuture : public IModuleInterface, public IOpenXRExtensionPlugin
{
public:
	FViveOpenXRFuture();
	virtual ~FViveOpenXRFuture() {}

	XrResult PollFuture(const XrFuturePollInfoEXT* pollInfo, XrFuturePollResultEXT* pollResult);

	XrResult CancelFuture(const XrFutureCancelInfoEXT* cancelInfo);

	// Thess are helper functions to make things simple.
	XrResult PollFuture(XrFutureEXT future, bool& isReady);
	XrResult CancelFuture(XrFutureEXT future);

	static FOpenXRHMD* HMD();
	static FViveOpenXRFuture* Instance();

public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual FString GetDisplayName() override;
	virtual bool GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
	virtual const void* OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext) override;
	virtual void PostCreateSession(XrSession InSession) override;

private:
	static FOpenXRHMD* hmd;
	//FViveOpenXRFuture* instance;

	bool isExtEnabled = false;
	XrInstance xrInstance = nullptr;
	XrSession xrSession = XR_NULL_HANDLE;
	PFN_xrPollFutureEXT xrPollFuture = nullptr;
	PFN_xrCancelFutureEXT xrCancelFuture = nullptr;
};

class VIVEOPENXRFUTURE_API FFutureObject
{
public:
	FFutureObject();
	virtual ~FFutureObject();

	void SetFuture(XrFutureEXT future);

	inline XrFutureEXT GetFuture() const { return future; }
	// Before you get result, check if error exist.  If error exist, you should not get result.  The result of future may still have error but will not show here.
	inline bool HasError() const { return hasError; }
	// Get poll result.  If no error, return XR_SUCCESS(0).
	inline int GetPollResult() const { return pollResult; }
	// Get complete result.  If no error, return XR_SUCCESS(0).
	inline int GetCompleteResult() const { return completeResult; }
	// After PollFuture, you can check poll result here.
	inline bool IsReady() const { return isReady; }
	// If futrue has done poll and complete, or has error during poll or complete, return true.
	inline bool IsDone() const { return isDone; }

	// Return true if ready. If future is ready, you need call complete yourself.
	bool PollFuture();
	// Cancel the future.
	void CancelFuture();
	// Do PollFuture, and if ready, call OnFutureComplete.  Return true if completed, canceled, or if error occure.
	bool CheckCompletion();
	// Block until Future completed
	void WaitFuture();
	bool IsCanceled() const { return isCanceled; }

	// Call child's implement.  After complete, remove future.  Derived class should implement this. Return 0 means no error when execute Future's poll and complete.  Should return error code if error occur when calling complete function.  Should not return other result which is not from complete function.
	virtual int OnFutureComplete() { return 0; }

private:
	void SetCompleteError(int result);
	void SetPollError(int result);
	void SetError();

protected:
	bool hasError;
	bool isReady;
	bool isDone;
	bool isCanceled;
	XrFutureEXT future;
	int pollResult;
	int completeResult;
};

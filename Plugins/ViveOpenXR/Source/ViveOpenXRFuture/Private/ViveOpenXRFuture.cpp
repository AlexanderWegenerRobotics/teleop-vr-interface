#include "ViveOpenXRFuture.h"
#include "OpenXRCore.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(ViveOXRFuture);

FOpenXRHMD* FViveOpenXRFuture::hmd = nullptr;
//FViveOpenXRFuture* FViveOpenXRFuture::instance = nullptr;

FOpenXRHMD* FViveOpenXRFuture::HMD() {
	if (hmd != nullptr)
		return hmd;
	if (GEngine->XRSystem.IsValid())
	{
		hmd = static_cast<FOpenXRHMD*>(GEngine->XRSystem->GetHMDDevice());
	}
	return hmd;
}

FViveOpenXRFuture* FViveOpenXRFuture::Instance() {
	//if (instance != nullptr)
	//{
	//	return instance;
	//}
	//else
	//{
	//	if (GEngine->XRSystem.IsValid() && HMD() != nullptr)
	//	{
	//		for (IOpenXRExtensionPlugin* Module : HMD()->GetExtensionPlugins())
	//		{
	//			if (Module->GetDisplayName() == TEXT("ViveOpenXRFuture"))
	//			{
	//				instance = static_cast<FViveOpenXRFuture*>(Module);
	//				break;
	//			}
	//		}
	//	}
	//	return instance;
	//}
	return &FModuleManager::GetModuleChecked<FViveOpenXRFuture>("ViveOpenXRFuture");
}

FViveOpenXRFuture::FViveOpenXRFuture()
	: isExtEnabled(false), xrInstance(XR_NULL_HANDLE), xrSession(XR_NULL_HANDLE), xrPollFuture(nullptr), xrCancelFuture(nullptr)
{
}

void FViveOpenXRFuture::StartupModule()
{
	//instance = this;
	RegisterOpenXRExtensionModularFeature();
}

void FViveOpenXRFuture::ShutdownModule()
{
	//instance = nullptr;
	if (isExtEnabled)
		UnregisterOpenXRExtensionModularFeature();
}

FString FViveOpenXRFuture::GetDisplayName()
{
	return FString(TEXT("ViveOpenXRFuture"));
}

bool FViveOpenXRFuture::GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	OutExtensions.Add(XR_EXT_FUTURE_EXTENSION_NAME);
	return true;
}


const void* FViveOpenXRFuture::OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext)
{
	isExtEnabled = false;

	if (!HMD()->IsExtensionEnabled(XR_EXT_FUTURE_EXTENSION_NAME)) {
		UE_LOG(ViveOXRFuture, Error, TEXT("Future extension is not enabled."));
		return InNext;
	}

	XR_ENSURE(xrGetInstanceProcAddr(InInstance, "xrPollFutureEXT", (PFN_xrVoidFunction*)&xrPollFuture));
	XR_ENSURE(xrGetInstanceProcAddr(InInstance, "xrCancelFutureEXT", (PFN_xrVoidFunction*)&xrCancelFuture));

	isExtEnabled = true;
	xrInstance = InInstance;
	return InNext;
}

void FViveOpenXRFuture::PostCreateSession(XrSession InSession)
{
	xrSession = InSession;
}

XrResult FViveOpenXRFuture::PollFuture(const XrFuturePollInfoEXT* pollInfo, XrFuturePollResultEXT* pollResult)
{
	if (!isExtEnabled) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrPollFuture(xrInstance, pollInfo, pollResult);
}

XrResult FViveOpenXRFuture::PollFuture(XrFutureEXT future, bool& isReady)
{
	XrFuturePollInfoEXT pollInfo = {
		.type = XR_TYPE_FUTURE_POLL_INFO_EXT,
		.next = nullptr,
		.future = future
	};
	XrFuturePollResultEXT pollResult{
		.type = XR_TYPE_FUTURE_POLL_RESULT_EXT,
		.next = nullptr,
		.state = XR_FUTURE_STATE_PENDING_EXT
	};
	auto ret = PollFuture(&pollInfo, &pollResult);
	if (XR_SUCCEEDED(ret))
		isReady = pollResult.state == XR_FUTURE_STATE_READY_EXT;
	return ret;
}

XrResult FViveOpenXRFuture::CancelFuture(const XrFutureCancelInfoEXT* cancelInfo)
{
	if (!isExtEnabled) return XR_ERROR_FUNCTION_UNSUPPORTED;
	return xrCancelFuture(xrInstance, cancelInfo);
}

XrResult FViveOpenXRFuture::CancelFuture(XrFutureEXT future)
{
	XrFutureCancelInfoEXT cancelInfo = {
		.type = XR_TYPE_FUTURE_CANCEL_INFO_EXT,
		.next = nullptr,
		.future = future,
	};

	return CancelFuture(&cancelInfo);
}

FFutureObject::FFutureObject()
	: hasError(false)
	, isReady(false)
	, isDone(false)
	, isCanceled(false)
	, future(nullptr)
	, pollResult(XR_SUCCESS)
	, completeResult(XR_SUCCESS)
{
}

FFutureObject::~FFutureObject()
{
}

void FFutureObject::SetFuture(XrFutureEXT InFuture)
{
	future = InFuture;
	if (future != nullptr) {
		isReady = false;
		isDone = false;
		hasError = false;
		isCanceled = false;
	}
}

bool FFutureObject::CheckCompletion()
{
	if (isDone || isCanceled)
		return true;

	PollFuture();

	if (hasError || isCanceled)
		return true;

	if (!isReady)
		return false;
	
	// isReady
	auto ret = OnFutureComplete();
	if (XR_FAILED(ret))
		SetCompleteError(ret);

	isDone = true;
	future = nullptr;
	return true;
}

void FFutureObject::WaitFuture()
{
	while (!CheckCompletion() && !isCanceled)
	{
		FPlatformProcess::Sleep(0.005f);
	}
}

bool FFutureObject::PollFuture()
{
	if (isDone) return isReady;
	auto mod = FViveOpenXRFuture::Instance();
	if (mod == nullptr || future == nullptr) {
		UE_LOG(ViveOXRFuture, Error, TEXT("FutureObject: Future is not exist."));
		SetError();
		return isReady;
	}

	auto ret = mod->PollFuture(future, isReady);
	if (XR_FAILED(ret))
	{
		UE_LOG(ViveOXRFuture, Error, TEXT("FutureObject: Failed to PollFuture."));
		SetPollError(ret);
		return isReady;
	}

	return isReady;
}

void FFutureObject::CancelFuture()
{
	if (future == nullptr) return;
	if (isCanceled) return;
	isCanceled = true;

	auto mod = FViveOpenXRFuture::Instance();
	if (mod != nullptr)
		mod->CancelFuture(future);

	// Let user not to trust the result.
	SetError();
}

void FFutureObject::SetPollError(int result)
{
	pollResult = result;
	SetError();
}

void FFutureObject::SetCompleteError(int result)
{
	completeResult = result;
	SetError();
}

void FFutureObject::SetError()
{
	hasError = true;
	future = nullptr;
	isDone = true;
}

IMPLEMENT_MODULE(FViveOpenXRFuture, ViveOpenXRFuture)
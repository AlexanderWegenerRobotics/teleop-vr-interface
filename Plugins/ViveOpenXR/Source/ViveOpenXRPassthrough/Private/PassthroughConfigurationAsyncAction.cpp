// Copyright HTC Corporation. All Rights Reserved.

#include "PassthroughConfigurationAsyncAction.h"
#include "ViveOpenXRPassthroughFunctionLibrary.h"
#include "ViveOpenXRPassthrough.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "OpenXRHMD.h"

UPassthroughSetQualityAsyncAction* UPassthroughSetQualityAsyncAction::SetPassthroughQuality(float QualityScaleInput)
{
    UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Set Passthrough Quality"));

    UPassthroughSetQualityAsyncAction* SetQualityAction = NewObject<UPassthroughSetQualityAsyncAction>();
    SetQualityAction->QualityScaleInput = QualityScaleInput;
    return SetQualityAction;
}

void UPassthroughSetQualityAsyncAction::Activate()
{
    UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Set Passthrough Quality Activate %f"), QualityScaleInput);
    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->QualityChangeSuccessEvent.AddDynamic(this, &UPassthroughSetQualityAsyncAction::QualityChangeActionComplete);
    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->QualityChangeFailureEvent.AddDynamic(this, &UPassthroughSetQualityAsyncAction::QualityChangeActionFailure);
    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->SetPassthroughQuality(QualityScaleInput);
}

void UPassthroughSetQualityAsyncAction::QualityChangeActionComplete(float FromQualityScale, float ToQualityScale)
{
    UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("QualityChangedSuccess %f, %f"), FromQualityScale, ToQualityScale);
    QualityChangedSuccess.Broadcast(FromQualityScale, ToQualityScale);
    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->QualityChangeSuccessEvent.RemoveDynamic(this, &UPassthroughSetQualityAsyncAction::QualityChangeActionComplete);
    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->QualityChangeFailureEvent.RemoveDynamic(this, &UPassthroughSetQualityAsyncAction::QualityChangeActionFailure);
    SetReadyToDestroy();
}

void UPassthroughSetQualityAsyncAction::QualityChangeActionFailure()
{
    UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("QualityChangeActionFailure"));
    QualityChangedFailure.Broadcast();
    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->QualityChangeSuccessEvent.RemoveDynamic(this, &UPassthroughSetQualityAsyncAction::QualityChangeActionComplete);
    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->QualityChangeFailureEvent.RemoveDynamic(this, &UPassthroughSetQualityAsyncAction::QualityChangeActionFailure);
    SetReadyToDestroy();
}

UPassthroughSetRateAsyncAction* UPassthroughSetRateAsyncAction::SetPassthroughRate(ConfigurationRateType RateTypeInput)
{
    UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Set Passthrough Rate"));

    UPassthroughSetRateAsyncAction* SetRateAction = NewObject<UPassthroughSetRateAsyncAction>();
    SetRateAction->RateTypeInput = RateTypeInput;
    return SetRateAction;
}

void UPassthroughSetRateAsyncAction::Activate()
{
    UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("Set Passthrough Rate Activate %d"), RateTypeInput);

    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->RateChangeSuccessEvent.AddDynamic(this, &UPassthroughSetRateAsyncAction::RateChangeActionComplete);
    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->RateChangeFailureEvent.AddDynamic(this, &UPassthroughSetRateAsyncAction::RateChangeActionFailure);

    switch (RateTypeInput)
    {
    case ConfigurationRateType::Boost:
        UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->SetPassthroughRate(1);
        break;
    case ConfigurationRateType::Normal:
        UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->SetPassthroughRate(0);
        break;
    }
}

void UPassthroughSetRateAsyncAction::RateChangeActionComplete(ConfigurationRateType FromRateType, ConfigurationRateType ToRateType)
{
    UE_LOG(LogViveOpenXRPassthrough, Log, TEXT("RateChangedSuccess %d, %d"), FromRateType, ToRateType);
    RateChangedSuccess.Broadcast(FromRateType, ToRateType);
    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->RateChangeSuccessEvent.RemoveDynamic(this, &UPassthroughSetRateAsyncAction::RateChangeActionComplete);
    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->RateChangeFailureEvent.RemoveDynamic(this, &UPassthroughSetRateAsyncAction::RateChangeActionFailure);
    SetReadyToDestroy();
}

void UPassthroughSetRateAsyncAction::RateChangeActionFailure()
{
    RateChangedFailure.Broadcast();
    UE_LOG(LogViveOpenXRPassthrough, Warning, TEXT("RateChangedFailure"));
    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->RateChangeSuccessEvent.RemoveDynamic(this, &UPassthroughSetRateAsyncAction::RateChangeActionComplete);
    UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()->RateChangeFailureEvent.RemoveDynamic(this, &UPassthroughSetRateAsyncAction::RateChangeActionFailure);
    SetReadyToDestroy();
}
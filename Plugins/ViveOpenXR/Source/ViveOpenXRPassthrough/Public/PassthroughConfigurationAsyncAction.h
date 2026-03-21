// Copyright HTC Corporation. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "VivePassthroughEnums.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "PassthroughConfigurationAsyncAction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPassthroughConfigurationQualityChangeCompleted, float, FromQualityScale, float, ToQualityScale);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPassthroughConfigurationQualityChangeFailed);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPassthroughConfigurationRateChangeCompleted, ConfigurationRateType, FromRateType, ConfigurationRateType, ToRateType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPassthroughConfigurationRateChangeFailed);

/**
 * This class defines an asynchronous node that returns a float and has success/failure pins.
 */

UCLASS(ClassGroup = OpenXR)
class VIVEOPENXRPASSTHROUGH_API UPassthroughSetQualityAsyncAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    // Factory function to create and initialize the async task
    UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Passthrough Quality", Keywords = "ViveOpenXR Passthrough", BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "ViveOpenXR|Passthrough")
    static UPassthroughSetQualityAsyncAction* SetPassthroughQuality(float QualityScaleInput);

    UPROPERTY(BlueprintAssignable)
    FPassthroughConfigurationQualityChangeCompleted QualityChangedSuccess;

    UPROPERTY(BlueprintAssignable)
    FPassthroughConfigurationQualityChangeFailed QualityChangedFailure;

    UFUNCTION()
    void QualityChangeActionComplete(float FromQualityScale, float ToQualityScale);

    UFUNCTION()
    void QualityChangeActionFailure();

protected:

    // Overriding the Activate
    virtual void Activate() override;

private:

    float QualityScaleInput;
};

UCLASS(ClassGroup = OpenXR)
class VIVEOPENXRPASSTHROUGH_API UPassthroughSetRateAsyncAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    // Factory function to create and initialize the async task
    UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Passthrough Rate", Keywords = "ViveOpenXR Passthrough", BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "ViveOpenXR|Passthrough")
    static UPassthroughSetRateAsyncAction* SetPassthroughRate(ConfigurationRateType RateTypeInput);

    UPROPERTY(BlueprintAssignable)
    FPassthroughConfigurationRateChangeCompleted RateChangedSuccess;

    UPROPERTY(BlueprintAssignable)
    FPassthroughConfigurationRateChangeFailed RateChangedFailure;

    UFUNCTION()
    void RateChangeActionComplete(ConfigurationRateType FromRateType, ConfigurationRateType ToRateType);

    UFUNCTION()
    void RateChangeActionFailure();

protected:

    // Overriding the Activate
    virtual void Activate() override;

private:

    ConfigurationRateType RateTypeInput;
};
#pragma once

#include "CoreMinimal.h"
#include "TeleOpTypes.generated.h"

/**
 * Tracked pose data for a single device (head or controller).
 */
USTRUCT(BlueprintType)
struct FTrackedPose
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Position = FVector::ZeroVector;

    UPROPERTY()
    FRotator Orientation = FRotator::ZeroRotator;

    UPROPERTY()
    bool bIsValid = false;

    UPROPERTY()
    double Timestamp = 0.0;
};
#include "UI/GazeComponent.h"
#include "EyeTrackerTypes.h"
#include "EyeTrackerFunctionLibrary.h"
#include "HTCEyeTrackerTypes.h"
#include "ViveOpenXREyeTrackerFunctionLibrary.h"
#include "DrawDebugHelpers.h"

UGazeComponent::UGazeComponent() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGazeComponent::BeginPlay() {
    Super::BeginPlay();
    bTrackerReady_ = UEyeTrackerFunctionLibrary::IsEyeTrackerConnected();
    if (bTrackerReady_) {
        UE_LOG(LogTemp, Log, TEXT("GazeComponent: eye tracking device ready"));
    } else {
        UE_LOG(LogTemp, Warning, TEXT("GazeComponent: eye tracker not ready at BeginPlay - will retry every %.0fs"), RetryInterval_);
    }
}

void UGazeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bTrackerReady_) {
        RetryAccum_ += DeltaTime;
        if (RetryAccum_ >= RetryInterval_) {
            RetryAccum_ = 0.0f;
            bTrackerReady_ = UEyeTrackerFunctionLibrary::IsEyeTrackerConnected();
            if (bTrackerReady_) {
                UE_LOG(LogTemp, Log, TEXT("GazeComponent: eye tracker connected on retry"));
            } else {
                UE_LOG(LogTemp, Warning, TEXT("GazeComponent: eye tracker still not ready - retrying..."));
            }
        }
        return;
    }

    FEyeTrackerGazeData RawGaze;
    if (!UEyeTrackerFunctionLibrary::GetGazeData(RawGaze)) return;

    bIsDeviceConnected = UEyeTrackerFunctionLibrary::IsEyeTrackerConnected();

    if (RawGaze.bIsRightEyeBlink) {
        GazeData_.bIsRightBlinking = true;
        GazeData_.bIsLeftBlinking = RawGaze.bIsLeftEyeBlink;
        GazeData_.bIsClosed = RawGaze.bIsLeftEyeBlink && RawGaze.bIsRightEyeBlink;
        GazeData_.bIsConnected = bIsDeviceConnected;
        GazeData_.Timestamp = FPlatformTime::Seconds();
        return;
    }

    DirectionHistory_.Add(RawGaze.GazeDirection);
    if (DirectionHistory_.Num() > SmoothingFrames_)
        DirectionHistory_.RemoveAt(0);

    FVector SmoothedDirection = FVector::ZeroVector;
    for (const FVector& D : DirectionHistory_)
        SmoothedDirection += D;
    SmoothedDirection = (SmoothedDirection / DirectionHistory_.Num()).GetSafeNormal();

    GazeData_.Origin = RawGaze.GazeOrigin;
    GazeData_.Direction = SmoothedDirection;
    GazeData_.Confidence = RawGaze.ConfidenceValue;
    GazeData_.bIsLeftBlinking = RawGaze.bIsLeftEyeBlink;
    GazeData_.bIsRightBlinking = false;
    GazeData_.bIsClosed = false;
    GazeData_.bIsValid = GazeData_.Confidence >= ConfidenceThreshold;
    GazeData_.bIsConnected = bIsDeviceConnected;
    GazeData_.Timestamp = FPlatformTime::Seconds();

    if (bDrawDebugRay && GazeData_.bIsValid) {
        DrawDebugLine(
            GetWorld(),
            GazeData_.Origin,
            GazeData_.Origin + GazeData_.Direction * DebugRayLength,
            FColor::Cyan, false, -1.0f, 0, 0.5f);
    }

    if (FPlatformTime::Seconds() - LastLogTime_ > 0.5 && bPrintDebugInfo) {
        UE_LOG(LogTemp, Log, TEXT("Gaze: valid=%d leftBlink=%d rightBlink=%d closed=%d conf=%.2f origin=(%.1f,%.1f,%.1f) dir=(%.2f,%.2f,%.2f)"),
            GazeData_.bIsValid, GazeData_.bIsLeftBlinking, GazeData_.bIsRightBlinking, GazeData_.bIsClosed, GazeData_.Confidence,
            GazeData_.Origin.X, GazeData_.Origin.Y, GazeData_.Origin.Z,
            GazeData_.Direction.X, GazeData_.Direction.Y, GazeData_.Direction.Z);
        LastLogTime_ = FPlatformTime::Seconds();
    }
}
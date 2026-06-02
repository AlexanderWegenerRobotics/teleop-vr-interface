#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GazeComponent.generated.h"

USTRUCT(BlueprintType)
struct FGazeData {
    GENERATED_BODY()
    UPROPERTY() FVector Origin = FVector::ZeroVector;
    UPROPERTY() FVector Direction = FVector::ZeroVector;
    UPROPERTY() bool bIsConnected = false;
    UPROPERTY() bool bIsValid = false;
    UPROPERTY() bool bIsLeftBlinking = false;
    UPROPERTY() bool bIsRightBlinking = false;
    UPROPERTY() bool bIsClosed = false;
    UPROPERTY() float Confidence = 0.0f;
    UPROPERTY() float leftPupilDiameter = 0.0f;
    UPROPERTY() float rightPupilDiameter = 0.0f;
    UPROPERTY() double Timestamp = 0.0;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UGazeComponent : public UActorComponent {
    GENERATED_BODY()

public:
    UGazeComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    const FGazeData& GetGazeData() const { return GazeData_; }
    bool IsTrackerConnected() const { return bTrackerReady_ && bIsDeviceConnected; }

    UPROPERTY(EditAnywhere, Category = "Gaze")
    float ConfidenceThreshold = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Gaze|Debug")
    bool bDrawDebugRay = false;

    UPROPERTY(EditAnywhere, Category = "Gaze|Debug")
    float DebugRayLength = 2000.0f;

    UPROPERTY(EditAnywhere, Category = "Gaze|Debug")
    bool bPrintDebugInfo = false;

private:
    FGazeData GazeData_;
    bool bIsDeviceConnected = false;

    TArray<FVector> DirectionHistory_;
    int32  SmoothingFrames_   = 5;
    double LastLogTime_       = 0.0;
    bool   bTrackerReady_     = false;
    float  RetryAccum_        = 0.0f;
    static constexpr float RetryInterval_ = 2.0f;
};
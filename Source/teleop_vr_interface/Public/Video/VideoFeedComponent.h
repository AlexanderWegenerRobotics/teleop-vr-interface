#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/StereoLayerComponent.h"
#include "Video/IVideoSource.h"
#include "VideoFeedComponent.generated.h"

class UCameraComponent;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UVideoFeedComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UVideoFeedComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void RegisterSource(const FString& Name, TUniquePtr<IVideoSource> Source);
    bool SetActiveSource(const FString& Name);

    FString GetActiveSourceName() const;
    FVideoSourceStats GetStreamStats() const;
    bool IsReceiving() const;
    UTexture2D* GetVideoTexture() const { return VideoTexture; }
    int64 GetSenderTimeNs() const { return ActiveSource_ ? ActiveSource_->GetLastSenderTimeNs() : 0; }

    UPROPERTY(EditAnywhere, Category = "VideoFeed")
    float PlaneDistance = 700.0f;

    UPROPERTY(EditAnywhere, Category = "VideoFeed", meta = (ClampMin = "0.5", ClampMax = "1.0"))
    float FOVCoverage = 0.85f;

private:
    void CreateStereoLayer();
    void UpdateLayerSize(int32 Width, int32 Height);

    UPROPERTY()
    TObjectPtr<UStereoLayerComponent> StereoLayer;

    UPROPERTY()
    TObjectPtr<UTexture2D> VideoTexture;

    TMap<FString, TUniquePtr<IVideoSource>> Sources_;
    FString      ActiveSourceName_;
    IVideoSource* ActiveSource_ = nullptr;

    UPROPERTY()
    TObjectPtr<UCameraComponent> CameraRef;
};
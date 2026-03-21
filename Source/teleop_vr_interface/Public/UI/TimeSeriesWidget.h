#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TMetricHistory.h"
#include "TimeSeriesWidget.generated.h"

USTRUCT(BlueprintType)
struct TELEOP_VR_INTERFACE_API FTimeSeriesConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    FString Label = TEXT("METRIC");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    FString Unit = TEXT("");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    float RangeMin = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    float RangeMax = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    float ThresholdLo = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    float ThresholdHi = 80.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    FLinearColor ColorNormal = FLinearColor(0.376f, 0.647f, 0.980f, 1.f);   // blue
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    FLinearColor ColorWarning = FLinearColor(0.961f, 0.620f, 0.043f, 1.f);  // amber
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    FLinearColor ColorCritical = FLinearColor(0.973f, 0.267f, 0.267f, 1.f); // red
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    float WarnFraction = 0.75f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    bool bDrawEnvelope = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    float EnvelopeAlpha = 0.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    float LineThickness = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    float BackgroundAlpha = 0.55f;
};


UCLASS()
class TELEOP_VR_INTERFACE_API UTimeSeriesWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeSeries")
    FTimeSeriesConfig Config;
    void BindHistory(const float* SamplesPtr, const float* EnvelopePtr, int32 HistoryCapacity, const int32* HeadPtr, float RangeMin, float RangeMax);

protected:

    virtual int32 NativePaint(
        const FPaintArgs& Args,
        const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements,
        int32 LayerId,
        const FWidgetStyle& InWidgetStyle,
        bool bParentEnabled) const override;

private:
    const float* SamplesPtr_ = nullptr;
    const float* EnvelopePtr_ = nullptr;
    const int32* HeadPtr_ = nullptr;
    int32        Capacity_ = 0;
    float        RangeMin_ = 0.f;
    float        RangeMax_ = 100.f;

    float ValueToY(float Value, float Height) const;
    FLinearColor ResolveLineColor(float CurrentValue) const;
    void DrawBackground(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geom) const;
    void DrawThresholdBand(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geom) const;
    void DrawEnvelope(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geom, const float* Ordered, const float* OrderedEnv) const;
    void DrawLine(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geom, const float* Ordered, const FLinearColor& Color) const;
    void DrawCurrentValueDot(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geom, float CurrentValue, const FLinearColor& Color) const;
    void DrawLabel(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geom,float CurrentValue) const;
};
#include "UI/TimeSeriesWidget.h"
#include "Rendering/DrawElements.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"

void UTimeSeriesWidget::BindHistory(const float* SamplesPtr, const float* EnvelopePtr, int32 HistoryCapacity, 
                                    const int32* HeadPtr, float InRangeMin, float InRangeMax) {
    SamplesPtr_ = SamplesPtr;
    EnvelopePtr_ = EnvelopePtr;
    HeadPtr_ = HeadPtr;
    Capacity_ = HistoryCapacity;
    RangeMin_ = InRangeMin;
    RangeMax_ = InRangeMax;
}

int32 UTimeSeriesWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const {
    if (!SamplesPtr_ || !HeadPtr_ || Capacity_ < 2)
        return LayerId;

    TArray<float> Ordered;
    TArray<float> OrderedEnv;
    Ordered.SetNumUninitialized(Capacity_);
    OrderedEnv.SetNumUninitialized(Capacity_);

    const int32 Head = *HeadPtr_;
    for (int32 i = 0; i < Capacity_; ++i) {
        const int32 Idx = (Head + i) % Capacity_;
        Ordered[i] = SamplesPtr_[Idx];
        OrderedEnv[i] = EnvelopePtr_ ? EnvelopePtr_[Idx] : 0.f;
    }

    const float CurrentValue = Ordered[Capacity_ - 1];
    const FLinearColor LineColor = ResolveLineColor(CurrentValue);

    DrawBackground(OutDrawElements, LayerId, AllottedGeometry);
    DrawThresholdBand(OutDrawElements, LayerId + 1, AllottedGeometry);

    if (Config.bDrawEnvelope && EnvelopePtr_)
        DrawEnvelope(OutDrawElements, LayerId + 2, AllottedGeometry, Ordered.GetData(), OrderedEnv.GetData());

    DrawLine(OutDrawElements, LayerId + 3, AllottedGeometry, Ordered.GetData(), LineColor);
    DrawCurrentValueDot(OutDrawElements, LayerId + 4, AllottedGeometry, CurrentValue, LineColor);
    DrawLabel(OutDrawElements, LayerId + 5, AllottedGeometry, CurrentValue);
    return LayerId + 6;
}

float UTimeSeriesWidget::ValueToY(float Value, float Height) const {
    const float Range = RangeMax_ - RangeMin_;
    if (FMath::IsNearlyZero(Range)) return Height * 0.5f;
    const float T = FMath::Clamp((Value - RangeMin_) / Range, 0.f, 1.f);
    return Height * (1.f - T);
}

FLinearColor UTimeSeriesWidget::ResolveLineColor(float CurrentValue) const {
    if (CurrentValue > Config.ThresholdHi)
        return Config.ColorCritical;
    if (CurrentValue > Config.ThresholdHi * Config.WarnFraction)
        return Config.ColorWarning;
    return Config.ColorNormal;
}

void UTimeSeriesWidget::DrawBackground(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geom) const {
    if (FMath::IsNearlyZero(Config.BackgroundAlpha)) return;

    const FVector2D Size = Geom.GetLocalSize();
    const FSlateRect Rect(0.f, 0.f, Size.X, Size.Y);

    FSlateDrawElement::MakeBox(
        OutDrawElements,
        LayerId,
        Geom.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f::ZeroVector)),
        FCoreStyle::Get().GetBrush("WhiteBrush"),
        ESlateDrawEffect::None,
        FLinearColor(0.03f, 0.05f, 0.08f, Config.BackgroundAlpha));
}

void UTimeSeriesWidget::DrawThresholdBand(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geom) const {
    const FVector2D Size = Geom.GetLocalSize();
    const float YTop = ValueToY(Config.ThresholdHi, Size.Y);
    const float YBottom = ValueToY(Config.ThresholdLo, Size.Y);

    FSlateDrawElement::MakeBox(
        OutDrawElements,
        LayerId,
        Geom.ToPaintGeometry(FVector2f(Size.X, YBottom - YTop), FSlateLayoutTransform(FVector2f(0.f, YTop))),
        FCoreStyle::Get().GetBrush("WhiteBrush"),
        ESlateDrawEffect::None,
        FLinearColor(0.133f, 0.773f, 0.369f, 0.07f));

    TArray<FVector2D> ThreshLine;
    ThreshLine.Add(FVector2D(0.f, YTop));
    ThreshLine.Add(FVector2D(Size.X, YTop));

    FSlateDrawElement::MakeLines(
        OutDrawElements,
        LayerId,
        Geom.ToPaintGeometry(),
        ThreshLine,
        ESlateDrawEffect::None,
        FLinearColor(0.133f, 0.773f, 0.369f, 0.25f),
        true,
        0.5f);
}

void UTimeSeriesWidget::DrawEnvelope(FSlateWindowElementList& OutDrawElements, int32 LayerId, 
                                    const FGeometry& Geom, const float* Ordered, const float* OrderedEnv) const {
    const FVector2D Size = Geom.GetLocalSize();
    const float     Dx = Size.X / static_cast<float>(Capacity_ - 1);

    TArray<FVector2D> Upper;
    Upper.Reserve(Capacity_);
    for (int32 i = 0; i < Capacity_; ++i)
        Upper.Add(FVector2D(i * Dx, ValueToY(Ordered[i] + OrderedEnv[i], Size.Y)));

    TArray<FVector2D> Lower;
    Lower.Reserve(Capacity_);
    for (int32 i = Capacity_ - 1; i >= 0; --i)
        Lower.Add(FVector2D(i * Dx, ValueToY(Ordered[i] - OrderedEnv[i], Size.Y)));

    for (int32 i = 0; i < Capacity_ - 1; ++i){
        const float X0 = i * Dx;
        const float X1 = (i + 1) * Dx;
        const float YTop = FMath::Min(Upper[i].Y, Upper[i + 1].Y);
        const float YBot = FMath::Max(
            Lower[Capacity_ - 1 - i].Y,
            Lower[Capacity_ - 2 - i].Y);

        if (YBot <= YTop) continue;

        FSlateDrawElement::MakeBox(
            OutDrawElements,
            LayerId,
            Geom.ToPaintGeometry(FVector2f(X1 - X0, YBot - YTop), FSlateLayoutTransform(FVector2f(X0, YTop))),
            FCoreStyle::Get().GetBrush("WhiteBrush"),
            ESlateDrawEffect::None,
            FLinearColor(0.376f, 0.647f, 0.980f, Config.EnvelopeAlpha));
    }

    FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geom.ToPaintGeometry(), Upper, ESlateDrawEffect::None, FLinearColor(0.376f, 0.647f, 0.980f, 0.2f), true, 0.5f);

    TArray<FVector2D> LowerFwd;
    LowerFwd.Reserve(Capacity_);
    for (int32 i = Capacity_ - 1; i >= 0; --i)
        LowerFwd.Add(Lower[Capacity_ - 1 - i]);

    FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geom.ToPaintGeometry(), LowerFwd, ESlateDrawEffect::None, FLinearColor(0.376f, 0.647f, 0.980f, 0.2f), true, 0.5f);
}

void UTimeSeriesWidget::DrawLine(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geom, const float* Ordered, const FLinearColor& Color) const {
    const FVector2D Size = Geom.GetLocalSize();
    const float     Dx = Size.X / static_cast<float>(Capacity_ - 1);

    TArray<FVector2D> Points;
    Points.Reserve(Capacity_);
    for (int32 i = 0; i < Capacity_; ++i)
        Points.Add(FVector2D(i * Dx, ValueToY(Ordered[i], Size.Y)));

    FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geom.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Config.LineThickness);
}

void UTimeSeriesWidget::DrawCurrentValueDot(FSlateWindowElementList& OutDrawElements, int32 LayerId, 
                                            const FGeometry& Geom, float CurrentValue, const FLinearColor& Color) const {
    const FVector2D Size = Geom.GetLocalSize();
    constexpr float Radius = 3.f;
    const float     X = Size.X - Radius;
    const float     Y = ValueToY(CurrentValue, Size.Y);

    FSlateDrawElement::MakeBox(
        OutDrawElements,
        LayerId,
        Geom.ToPaintGeometry(FVector2f(Radius * 2.f, Radius * 2.f), FSlateLayoutTransform(FVector2f(X - Radius, Y - Radius))),
        FCoreStyle::Get().GetBrush("WhiteBrush"),
        ESlateDrawEffect::None,
        Color);
}

void UTimeSeriesWidget::DrawLabel(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geom, float CurrentValue) const {
    const FVector2D Size = Geom.GetLocalSize();

    FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
    FSlateFontInfo ValueFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);

    const FLinearColor LabelColor(1.f, 1.f, 1.f, 0.35f);
    const FLinearColor ValueColor = ResolveLineColor(CurrentValue);

    // Metric label  top left
    FSlateDrawElement::MakeText(
        OutDrawElements, LayerId,
        Geom.ToPaintGeometry(FVector2f(Size.X, 14.f), FSlateLayoutTransform(FVector2f(6.f, 4.f))),
        FText::FromString(Config.Label),
        SmallFont,
        ESlateDrawEffect::None,
        LabelColor);

    // Current value  top right
    const FString ValueStr = FString::Printf(TEXT("%.1f%s"),
        CurrentValue, *Config.Unit);

    FSlateDrawElement::MakeText(
        OutDrawElements, LayerId,
        Geom.ToPaintGeometry(FVector2f(Size.X * 0.42f, 14.f), FSlateLayoutTransform(FVector2f(Size.X * 0.55f, 4.f))),
        FText::FromString(ValueStr),
        ValueFont,
        ESlateDrawEffect::None,
        ValueColor);
}
#pragma once

#include "CoreMinimal.h"
#include "UI/WidgetBinder.h"

class UTexture2D;
class UTextureRenderTarget2D;

struct FHudPanels
{
	UWidgetBinder* Frame  = nullptr;
	UWidgetBinder* Alerts = nullptr;

	TMap<FName, FName> NearOf;
	TMap<FName, FName> BaseOf;

	UWidgetBinder* For(FName Name) const
	{
		if (Alerts && Alerts->HasWidget(Name)) return Alerts;
		return Frame;
	}

	void BuildMirrors()
	{
		NearOf.Reset();
		BaseOf.Reset();
		const FString Suffix = TEXT("_near");
		for (UWidgetBinder* B : { Frame, Alerts }) {
			if (!B) continue;
			TArray<FName> Names;
			B->GetWidgetNames(Names);
			for (const FName& N : Names) {
				const FString S = N.ToString();
				if (!S.EndsWith(Suffix)) continue;
				const FName Base(*S.LeftChop(Suffix.Len()));
				if (!HasWidget(Base)) continue;
				NearOf.Add(Base, N);
				BaseOf.Add(N, Base);
			}
		}
	}

	FName Canonical(FName Name) const { const FName* B = BaseOf.Find(Name); return B ? *B : Name; }
	FName NearCopy(FName Name) const  { const FName* M = NearOf.Find(Name); return M ? *M : FName(); }

	template <typename FnType>
	void Each(FName Name, FnType&& Fn) const
	{
		if (UWidgetBinder* B = For(Name)) Fn(B, Name);
		const FName M = NearCopy(Name);
		if (M != FName()) if (UWidgetBinder* B = For(M)) Fn(B, M);
	}

	void SetGazeInput(const FGazeData& GazeData) const
	{
		if (Frame)  Frame->SetGazeInput(GazeData);
		if (Alerts) Alerts->SetGazeInput(GazeData);
	}

	FName ConsumePress() const
	{
		const FName A = Alerts ? Alerts->ConsumePress() : FName();
		const FName F = Frame  ? Frame->ConsumePress()  : FName();
		return Canonical(A != FName() ? A : F);
	}

	FName ConsumeRejection() const
	{
		const FName A = Alerts ? Alerts->ConsumeRejection() : FName();
		const FName F = Frame  ? Frame->ConsumeRejection()  : FName();
		return Canonical(A != FName() ? A : F);
	}

	void PushMessage(const FString& Text, float Duration) const
	{
		UWidgetBinder* Sink = (Alerts && Alerts->HasMessageSink()) ? Alerts : Frame;
		if (Sink) Sink->PushMessage(Text, Duration);
	}

	bool HasWidget(FName Name) const
	{
		return (Frame && Frame->HasWidget(Name)) || (Alerts && Alerts->HasWidget(Name));
	}

	TArray<FName> DuplicateNames() const
	{
		TArray<FName> Out;
		if (!Frame || !Alerts) return Out;
		TArray<FName> Names;
		Alerts->GetWidgetNames(Names);
		for (const FName& N : Names)
			if (Frame->HasWidget(N)) Out.Add(N);
		return Out;
	}

	FString GetWidgetClassName(FName Name) const { const UWidgetBinder* B = For(Name); return B ? B->GetWidgetClassName(Name) : FString(); }
	FLinearColor GetDesignTint(FName Name) const { const UWidgetBinder* B = For(Name); return B ? B->GetDesignTint(Name) : FLinearColor::White; }
	UTextureRenderTarget2D* GetRenderTarget() const { return Frame ? Frame->GetRenderTarget() : nullptr; }

	void SetText(FName Name, const FString& Text) const                { Each(Name, [&](UWidgetBinder* B, FName N) { B->SetText(N, Text); }); }
	void SetTextColor(FName Name, const FLinearColor& Color) const     { Each(Name, [&](UWidgetBinder* B, FName N) { B->SetTextColor(N, Color); }); }
	void SetImageColor(FName Name, const FLinearColor& Color) const    { Each(Name, [&](UWidgetBinder* B, FName N) { B->SetImageColor(N, Color); }); }
	void SetBorderColor(FName Name, const FLinearColor& Color) const   { Each(Name, [&](UWidgetBinder* B, FName N) { B->SetBorderColor(N, Color); }); }
	void SetImageTexture(FName Name, UTexture2D* Texture) const        { Each(Name, [&](UWidgetBinder* B, FName N) { B->SetImageTexture(N, Texture); }); }
	void SetVisibility(FName Name, bool bVisible) const                { Each(Name, [&](UWidgetBinder* B, FName N) { B->SetVisibility(N, bVisible); }); }
	void SetOpacity(FName Name, float Opacity) const                   { Each(Name, [&](UWidgetBinder* B, FName N) { B->SetOpacity(N, Opacity); }); }
	void SetButtonLocked(FName Name, bool bLocked) const               { Each(Name, [&](UWidgetBinder* B, FName N) { B->SetButtonLocked(N, bLocked); }); }
	void SetButtonToggled(FName Name, bool bToggled) const             { Each(Name, [&](UWidgetBinder* B, FName N) { B->SetButtonToggled(N, bToggled); }); }
	void SetStateDriven(FName Name) const                              { Each(Name, [&](UWidgetBinder* B, FName N) { B->SetStateDriven(N); }); }
	void SetPlotThreshold(FName Name, float Hi, float Lo = 0.0f) const { if (UWidgetBinder* B = For(Name)) B->SetPlotThreshold(Name, Hi, Lo); }
	void SetWidgetRenderScale(FName Name, FVector2D Scale, FVector2D Pivot) const { if (UWidgetBinder* B = For(Name)) B->SetWidgetRenderScale(Name, Scale, Pivot); }

	void BindPlot(FName Name, const float* Samples, const float* Envelope, int32 Capacity, const int32* Head, float RangeMin, float RangeMax) const
	{
		if (UWidgetBinder* B = For(Name)) B->BindPlot(Name, Samples, Envelope, Capacity, Head, RangeMin, RangeMax);
	}

	FVector2D GetWidgetSlotPosition(FName Name) const { const UWidgetBinder* B = For(Name); return B ? B->GetWidgetSlotPosition(Name) : FVector2D::ZeroVector; }
	FVector2D GetWidgetSize(FName Name) const         { const UWidgetBinder* B = For(Name); return B ? B->GetWidgetSize(Name) : FVector2D::ZeroVector; }
	bool IsGazeOverWidget(FName Name, float MarginPx = 0.f) const { const UWidgetBinder* B = For(Name); return B && B->IsGazeOverWidget(Name, MarginPx); }

	void ShowCameraMenu(const TArray<FString>& StreamNames, const FString& ActiveStream) const { if (Frame) Frame->ShowCameraMenu(StreamNames, ActiveStream); }
	void ShowResetMenu() const { if (Frame) Frame->ShowResetMenu(); }
	void HideMenu() const      { if (Frame) Frame->HideMenu(); }
};

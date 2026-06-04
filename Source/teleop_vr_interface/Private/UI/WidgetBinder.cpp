#include "UI/WidgetBinder.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Camera/CameraComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Slate/WidgetRenderer.h"


#ifdef UpdateResource
    #undef UpdateResource
#endif

UWidgetBinder::UWidgetBinder() {
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UWidgetBinder::BeginPlay() {
	Super::BeginPlay();
}

void UWidgetBinder::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	bIsBound_ = false;

	// Remove from Slate first — stops any new paint/tick commands being queued.
	if (Widget_) {
		Widget_->RemoveFromParent();
		Widget_ = nullptr;
	}

	// Stereo layer off next — stops GPU sampling the render target texture.
	if (Layer_) {
		Layer_->DestroyComponent();
		Layer_ = nullptr;
	}

	// First flush: drain commands queued before we reset the renderer.
	FlushRenderingCommands();

	// Reset renderer — may itself queue cleanup commands on the render thread.
	if (WidgetRenderer_) {
		WidgetRenderer_.Reset();
	}

	// Second flush: drain those cleanup commands before releasing the texture.
	FlushRenderingCommands();

	if (RenderTarget_) {
		RenderTarget_->ReleaseResource();
		RenderTarget_ = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void UWidgetBinder::Initialize(TSubclassOf<UUserWidget> WidgetClass, UCameraComponent* Camera, FVector2D RenderSize, float Distance, int32 Priority,
	float CollapsedWidth, float ExpandedWidth, float VerticalOffset) {

	if (!WidgetClass || !Camera) {
		UE_LOG(LogTemp, Error, TEXT("WidgetBinder: null widget class or camera"));
		return;
	}

	Camera_ = Camera;
	LayerDistance_ = Distance;
	RenderSize_ = RenderSize;

	WidgetRenderer_ = MakeShared<FWidgetRenderer>(true);

	RenderTarget_ = NewObject<UTextureRenderTarget2D>(GetOwner());
	RenderTarget_->InitCustomFormat(static_cast<uint32>(RenderSize.X), static_cast<uint32>(RenderSize.Y), PF_B8G8R8A8, true);
	RenderTarget_->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	RenderTarget_->UpdateResource();

	Widget_ = CreateWidget<UUserWidget>(GetWorld(), WidgetClass);
	if (!Widget_) {
		UE_LOG(LogTemp, Error, TEXT("WidgetBinder: failed to create widget"));
		return;
	}
	Widget_->AddToViewport(0);
	Widget_->SetVisibility(ESlateVisibility::HitTestInvisible);

	FName LayerName = FName(*FString::Printf(TEXT("UILayer_%s"), *GetName()));
	Layer_ = NewObject<UStereoLayerComponent>(GetOwner(), LayerName);
	Layer_->SetTexture(RenderTarget_);
	Layer_->bLiveTexture = true;
	Layer_->bSupportsDepth = false;
	Layer_->SetPriority(Priority);
	Layer_->AttachToComponent(Camera, FAttachmentTransformRules::KeepRelativeTransform);
	Layer_->SetRelativeLocation(FVector(Distance, 0.0f, 0.0f));
	QuadSize_ = RenderSize;
	CollapsedWidth_ = CollapsedWidth > 0.0f ? CollapsedWidth : RenderSize.X;
	ExpandedWidth_ = ExpandedWidth > CollapsedWidth_ ? ExpandedWidth : CollapsedWidth_;
	LayerVerticalOffset_ = VerticalOffset;
	Layer_->SetQuadSize(QuadSize_);
	Layer_->RegisterComponent();

	DiscoverWidgets();

	// Pre-render so Slate runs a full layout pass; nested widget geometries
	// (HBox/VBox children) are zero until at least one DrawWidget call.
	WidgetRenderer_->DrawWidget(RenderTarget_, Widget_->TakeWidget(), RenderSize_, 0.0f, true);

	CacheWidgetRects();
	bIsBound_ = true;
}

void UWidgetBinder::DiscoverWidgets() {
	if (!Widget_ || !Widget_->WidgetTree) return;

	CachedWidgets_.Empty();
	CachedButtons_.Empty();
	CachedTextBlocks_.Empty();
	CachedPlots_.Empty();
	CachedImages_.Empty();
	OriginalStyles_.Empty();
	MessageLog_ = nullptr;

	Widget_->WidgetTree->ForEachWidget([this](UWidget* W) {
		CachedWidgets_.Add(W->GetFName(), W);
		if (UButton* Btn = Cast<UButton>(W)) {
			FName Name = W->GetFName();
			CachedButtons_.Add(Name, Btn);
			OriginalStyles_.Add(Name, Btn->GetStyle());
		}
		else if (UTextBlock* Txt = Cast<UTextBlock>(W)) {
			CachedTextBlocks_.Add(W->GetFName(), Txt);
		}
		else if (UVerticalBox* VBox = Cast<UVerticalBox>(W)) {
			if (W->GetFName() == FName("cameraMenuList")) CameraMenuList_ = VBox;
			else if (!MessageLog_) MessageLog_ = VBox;
		}
		else if (UTimeSeriesWidget* Plot = Cast<UTimeSeriesWidget>(W)) {
			CachedPlots_.Add(W->GetFName(), Plot);
		}
		else if (UImage* Img = Cast<UImage>(W)) {
			CachedImages_.Add(W->GetFName(), Img);
		}
	});
}

void UWidgetBinder::CacheWidgetRects() {
	WidgetRects_.Empty();
	ButtonRects_.Empty();

	if (!Widget_ || !Widget_->WidgetTree) return;

	Widget_->WidgetTree->ForEachWidget([this](UWidget* W) {
		FName Name = W->GetFName();

		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(W->Slot)) {
			// Direct canvas child — compute rect from slot properties.
			FWidgetRect Rect;
			FVector2D SlotPos   = Slot->GetPosition();
			FVector2D SlotSize  = Slot->GetSize();
			FVector2D Alignment = Slot->GetAlignment();
			FAnchors  Anchors   = Slot->GetAnchors();

			FVector2D AnchorPos(RenderSize_.X * Anchors.Minimum.X, RenderSize_.Y * Anchors.Minimum.Y);
			Rect.Position.X = AnchorPos.X + SlotPos.X - (SlotSize.X * Alignment.X);
			Rect.Position.Y = AnchorPos.Y + SlotPos.Y - (SlotSize.Y * Alignment.Y);
			Rect.Size = SlotSize;

			WidgetRects_.Add(Name, Rect);
			if (CachedButtons_.Contains(Name)) ButtonRects_.Add(Name, Rect);
		}
		else if (CachedButtons_.Contains(Name)) {
			// Nested button (inside HBox / VBox / etc.) — read Slate geometry.
			// After FWidgetRenderer::DrawWidget the cached geometry is in render-target
			// pixel space (virtual window origin = RT top-left, DPI scale = 1.0).
			const FGeometry Geom = W->GetCachedGeometry();
			const FVector2D AbsSize = Geom.GetAbsoluteSize();
			if (!AbsSize.IsNearlyZero()) {
				FWidgetRect Rect;
				Rect.Position = Geom.GetAbsolutePosition();
				Rect.Size     = AbsSize;
				WidgetRects_.Add(Name, Rect);
				ButtonRects_.Add(Name, Rect);
			}
		}
	});
}

void UWidgetBinder::RenderWidget() {
	if (!bIsBound_ || !Widget_ || !RenderTarget_ || !WidgetRenderer_) return;
	if (!RenderTarget_->IsValidLowLevel()) return;
	WidgetRenderer_->DrawWidget(RenderTarget_, Widget_->TakeWidget(), RenderSize_, 0.0f, true);
}

void UWidgetBinder::SetGazeInput(const FGazeData& GazeData) {
	if (!Camera_) return;

	FTransform CameraWorld = Camera_->GetComponentTransform();
	FTransform CameraInverse = CameraWorld.Inverse();

	GazeLocalOrigin_ = CameraInverse.TransformPosition(GazeData.Origin);
	GazeLocalDirection_ = CameraInverse.TransformVectorNoScale(GazeData.Direction).GetSafeNormal();

	LatestGaze_ = GazeData;
}

bool UWidgetBinder::IsWinkActive() const {
	return WinkGesture_.IsBlinking();
}

FName UWidgetBinder::ConsumePress() {
	FName Result = PressedButton_;
	PressedButton_ = FName();
	return Result;
}

FName UWidgetBinder::ConsumeRejection() {
	FName Result = RejectedButton_;
	RejectedButton_ = FName();
	return Result;
}

void UWidgetBinder::SetText(FName WidgetName, const FString& Text) {
	if (auto* Found = CachedTextBlocks_.Find(WidgetName)) {
		if (*Found) (*Found)->SetText(FText::FromString(Text));
	}
}

void UWidgetBinder::SetTextColor(FName WidgetName, const FLinearColor& Color) {
	if (auto* Found = CachedTextBlocks_.Find(WidgetName)) {
		if (*Found) (*Found)->SetColorAndOpacity(FSlateColor(Color));
	}
}

void UWidgetBinder::PushMessage(const FString& Text, float Duration) {
	FPendingMessage Msg;
	Msg.Text = Text;
	Msg.Remaining = Duration;
	MessageQueue_.Add(Msg);
	bMessagesDirty_ = true;
}

void UWidgetBinder::SetButtonLocked(FName ButtonName, bool bLocked) {
	if (bLocked) {
		LockedButtons_.Add(ButtonName);
		SetButtonToLocked(ButtonName);
	}
	else {
		LockedButtons_.Remove(ButtonName);
		SetButtonToNormal(ButtonName);
	}
}

bool UWidgetBinder::IsButtonLocked(FName ButtonName) const {
	return LockedButtons_.Contains(ButtonName);
}

void UWidgetBinder::ApplyButtonStyle(FName Name, const FSlateBrush& Brush) {
	// Promote the chosen brush into the Normal slot so Slate renders it regardless of interaction state.
	auto* FoundBtn   = CachedButtons_.Find(Name);
	auto* FoundStyle = OriginalStyles_.Find(Name);
	if (!FoundBtn || !*FoundBtn || !FoundStyle) return;
	FButtonStyle S = *FoundStyle;
	S.Normal       = Brush;
	(*FoundBtn)->SetStyle(S);
}

void UWidgetBinder::SetButtonToLocked(FName Name) {
	if (Name == FName()) return;
	auto* Style = OriginalStyles_.Find(Name);
	if (!Style) return;
	// Keep the Normal image visible; apply the Disabled tint so the button is dimmed but not blank.
	FSlateBrush Brush = Style->Normal;
	Brush.TintColor   = Style->Disabled.TintColor;
	ApplyButtonStyle(Name, Brush);
}

void UWidgetBinder::SetButtonToggled(FName ButtonName, bool bToggled) {
	if (bToggled) ToggledButtons_.Add(ButtonName);
	else          ToggledButtons_.Remove(ButtonName);
	if (LockedButtons_.Contains(ButtonName))  SetButtonToLocked(ButtonName);
	else if (ButtonName == HoveredButton_)    SetButtonToHovered(ButtonName);
	else                                      SetButtonToNormal(ButtonName);
}

bool UWidgetBinder::IsButtonToggled(FName ButtonName) const {
	return ToggledButtons_.Contains(ButtonName);
}

void UWidgetBinder::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bIsBound_) return;

	bool bPressed = WinkGesture_.Update(LatestGaze_.bIsRightBlinking, DeltaTime);

	FVector2D UV;
	bool bGazeOnPanel = ProjectGazeToUV(UV);
	FName NewHovered = bGazeOnPanel ? FindButtonAtUV(UV) : FName();

	if (NewHovered != HoveredButton_) {
		SetButtonToNormal(HoveredButton_);
		if (LockedButtons_.Contains(NewHovered)) {
			SetButtonToLocked(NewHovered);
		}
		else {
			SetButtonToHovered(NewHovered);
		}
		HoveredButton_ = NewHovered;
	}

	if (bPressed && HoveredButton_ != FName()) {
		if (LockedButtons_.Contains(HoveredButton_)) {
			RejectedButton_ = HoveredButton_;
		}
		else {
			PressedButton_ = HoveredButton_;
			// Toggle persistent pressed state; visual follows immediately.
			SetButtonToggled(HoveredButton_, !ToggledButtons_.Contains(HoveredButton_));
		}
	}
	UpdateMessages(DeltaTime);

	if (bMessagesDirty_) {
		RebuildMessageLog();
		bMessagesDirty_ = false;
	}

	RenderWidget();

	if (bMenuRectsDirty_) {
		bool bAllReady = true;
		for (const FName& Name : DynamicMenuItems_) {
			UWidget** W = CachedWidgets_.Find(Name);
			if (!W || !*W) { bAllReady = false; continue; }
			const FGeometry Geom = (*W)->GetCachedGeometry();
			const FVector2D AbsSize = Geom.GetAbsoluteSize();
			if (AbsSize.IsNearlyZero()) { bAllReady = false; continue; }
			FWidgetRect Rect;
			Rect.Position = Geom.GetAbsolutePosition();
			Rect.Size     = AbsSize;
			WidgetRects_.Add(Name, Rect);
			ButtonRects_.Add(Name, Rect);
		}
		if (bAllReady) bMenuRectsDirty_ = false;
	}

	if (bPrintDebugInfo && FPlatformTime::Seconds() - LastLogTime_ > 0.5) {
		FVector2D PixelPos = bGazeOnPanel ? UV * RenderSize_ : FVector2D::ZeroVector;
		UE_LOG(LogTemp, Log, TEXT("WidgetBinder: pixel=(%.1f,%.1f) hovered=%s blinking=%d"),
			PixelPos.X, PixelPos.Y,
			HoveredButton_ != FName() ? *HoveredButton_.ToString() : TEXT("none"),
			WinkGesture_.IsBlinking());
		LastLogTime_ = FPlatformTime::Seconds();
	}
}

bool UWidgetBinder::ProjectGazeToUV(FVector2D& OutUV) const {
	if (FMath::IsNearlyZero(GazeLocalDirection_.X)) return false;

	float T = (LayerDistance_ - GazeLocalOrigin_.X) / GazeLocalDirection_.X;
	if (T < 0.0f) return false;

	float HitY = GazeLocalOrigin_.Y + GazeLocalDirection_.Y * T;
	float HitZ = GazeLocalOrigin_.Z + GazeLocalDirection_.Z * T;

	OutUV.X = (HitY / ExpandedWidth_) + 0.5f;
	OutUV.Y = (-HitZ / RenderSize_.Y) + 0.5f;

	return OutUV.X >= 0.0f && OutUV.X <= 1.0f && OutUV.Y >= 0.0f && OutUV.Y <= 1.0f;
}

FName UWidgetBinder::FindButtonAtUV(const FVector2D& UV) const {
	FVector2D PixelPos = UV * RenderSize_;
	for (const auto& Pair : ButtonRects_) {
		const FWidgetRect& R = Pair.Value;
		if (PixelPos.X >= R.Position.X && PixelPos.X <= R.Position.X + R.Size.X &&
			PixelPos.Y >= R.Position.Y && PixelPos.Y <= R.Position.Y + R.Size.Y) {
			return Pair.Key;
		}
	}
	return FName();
}

void UWidgetBinder::SetButtonToNormal(FName Name) {
	if (Name == FName()) return;
	if (LockedButtons_.Contains(Name)) { SetButtonToLocked(Name); return; }
	auto* Style = OriginalStyles_.Find(Name);
	if (!Style) return;
	// Toggled buttons rest on the Pressed image; untoggled buttons on Normal.
	ApplyButtonStyle(Name, ToggledButtons_.Contains(Name) ? Style->Pressed : Style->Normal);
}

void UWidgetBinder::SetButtonToHovered(FName Name) {
	if (Name == FName()) return;
	auto* Style = OriginalStyles_.Find(Name);
	if (!Style) return;
	// Keep the current state's image; only change the tint colour for hover feedback.
	// Hovered.TintColor may be UseColor_Foreground ("Inherit") which has no visible
	// effect inside a render-target context — fall back to a mild brightness boost.
	FSlateBrush Brush = ToggledButtons_.Contains(Name) ? Style->Pressed : Style->Normal;
	if (Style->Hovered.TintColor.IsColorSpecified())
	{
		Brush.TintColor = Style->Hovered.TintColor;
	}
	else
	{
		FLinearColor Base = Brush.TintColor.GetSpecifiedColor();
		Brush.TintColor   = FSlateColor(FLinearColor(Base.R * 1.25f, Base.G * 1.25f, Base.B * 1.25f, Base.A));
	}
	ApplyButtonStyle(Name, Brush);
}


void UWidgetBinder::ShowCameraMenu(const TArray<FString>& StreamNames, const FString& ActiveStream) {
	if (!CameraMenuList_ || !Widget_) return;

	HideMenu();

	constexpr float R = 8.f;
	auto MakeRoundedBrush = [](const FLinearColor& Color) -> FSlateBrush {
		FSlateBrush B;
		B.DrawAs    = ESlateBrushDrawType::RoundedBox;
		B.TintColor = FSlateColor(Color);
		B.OutlineSettings.CornerRadii = FVector4(R, R, R, R);
		B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		return B;
	};

	FButtonStyle ItemStyle;
	ItemStyle.Normal  = MakeRoundedBrush(FLinearColor(0.32f, 0.32f, 0.32f, 0.95f));
	ItemStyle.Hovered = MakeRoundedBrush(FLinearColor(0.14f, 0.14f, 0.14f, 1.0f));
	ItemStyle.Pressed = MakeRoundedBrush(FLinearColor(0.08f, 0.08f, 0.08f, 1.0f));

	auto AddItem = [&](const FName& BtnName, const FString& Label, const FLinearColor& LabelColor)
	{
		UButton*    Btn = NewObject<UButton>(Widget_->WidgetTree);
		UTextBlock* Lbl = NewObject<UTextBlock>(Widget_->WidgetTree);

		FSlateFontInfo Font = Lbl->GetFont();
		Font.Size = 15;
		Lbl->SetFont(Font);
		Lbl->SetText(FText::FromString(Label));
		Lbl->SetColorAndOpacity(FSlateColor(LabelColor));
		Lbl->SetJustification(ETextJustify::Center);

		Btn->AddChild(Lbl);
		FButtonStyle Style = ItemStyle;
		Style.SetNormalPadding(FMargin(16.f, 5.f));
		Style.SetPressedPadding(FMargin(16.f, 4.f));
		Btn->SetStyle(Style);

		UVerticalBoxSlot* Slot = CameraMenuList_->AddChildToVerticalBox(Btn);
		if (Slot) {
			Slot->SetPadding(FMargin(0.f, 2.f));
			Slot->SetHorizontalAlignment(HAlign_Fill);
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}

		CachedWidgets_.Add(BtnName, Btn);
		CachedButtons_.Add(BtnName, Btn);
		OriginalStyles_.Add(BtnName, Style);
		DynamicMenuItems_.Add(BtnName);
	};

	for (int32 i = 0; i < StreamNames.Num(); ++i) {
		FName Name = FName(*FString::Printf(TEXT("__menu_%d"), i));
		bool  bActive = (StreamNames[i] == ActiveStream);
		FString Label = bActive ? FString::Printf(TEXT("• %s"), *StreamNames[i]) : StreamNames[i];
		AddItem(Name, Label, bActive ? FLinearColor(0.4f, 0.8f, 1.f) : FLinearColor::White);
	}

	if (!ActiveStream.IsEmpty())
		AddItem(FName("__menu_off"), TEXT("turn off"), FLinearColor(1.f, 0.35f, 0.35f));

	SetVisibility(FName("cameraMenu"), true);

	// Compute rects analytically — no Slate tick dependency.
	// ForceLayoutPrepass populates GetDesiredSize() on all widgets immediately.
	if (Widget_) Widget_->ForceLayoutPrepass();

	UWidget*          MenuWidget   = CachedWidgets_.FindRef(FName("cameraMenu"));
	UCanvasPanelSlot* MenuSlot     = MenuWidget ? Cast<UCanvasPanelSlot>(MenuWidget->Slot) : nullptr;

	if (MenuSlot) {
		const FVector2D DesiredSize = MenuWidget->GetDesiredSize();
		const FVector2D SlotPos     = MenuSlot->GetPosition();
		const FVector2D Alignment   = MenuSlot->GetAlignment();
		const FAnchors  Anchors     = MenuSlot->GetAnchors();

		const FVector2D AnchorPos(RenderSize_.X * Anchors.Minimum.X, RenderSize_.Y * Anchors.Minimum.Y);
		const FVector2D MenuTL(
			AnchorPos.X + SlotPos.X - DesiredSize.X * Alignment.X,
			AnchorPos.Y + SlotPos.Y - DesiredSize.Y * Alignment.Y);

		// Match the content padding set on cameraMenuBorder in the Blueprint.
		constexpr float PadH  = 12.f;
		constexpr float PadV  = 8.f;
		constexpr float SlotV = 2.f;
		float Y = MenuTL.Y + PadV;

		for (const FName& Name : DynamicMenuItems_) {
			UWidget* Item = CachedWidgets_.FindRef(Name);
			if (!Item) continue;
			Y += SlotV;
			FWidgetRect Rect;
			Rect.Position = FVector2D(MenuTL.X + PadH, Y);
			Rect.Size     = FVector2D(DesiredSize.X - PadH * 2.f, Item->GetDesiredSize().Y);
			WidgetRects_.Add(Name, Rect);
			ButtonRects_.Add(Name, Rect);
			Y += Rect.Size.Y + SlotV;
		}
		// Keep dirty: on the FIRST menu open of a session the freshly created buttons
		// have no valid desired/cached size yet, so the analytic rects above are only a
		// rough first estimate. Leaving the flag set lets the Tick path (which uses real
		// GetCachedGeometry and waits for bAllReady) finalize the hit-rects within a frame.
		// Clearing it here was the bug that made the first menu selection of a session miss.
		bMenuRectsDirty_ = true;
	} else {
		bMenuRectsDirty_ = true;
	}

}

void UWidgetBinder::HideMenu() {
	SetVisibility(FName("cameraMenu"), false);

	if (HoveredButton_ != FName() && DynamicMenuItems_.Contains(HoveredButton_)) {
		SetButtonToNormal(HoveredButton_);
		HoveredButton_ = FName();
	}

	for (const FName& Name : DynamicMenuItems_) {
		CachedButtons_.Remove(Name);
		CachedWidgets_.Remove(Name);
		ButtonRects_.Remove(Name);
		WidgetRects_.Remove(Name);
		OriginalStyles_.Remove(Name);
		ToggledButtons_.Remove(Name);
		LockedButtons_.Remove(Name);
	}
	DynamicMenuItems_.Empty();

	if (CameraMenuList_) CameraMenuList_->ClearChildren();
}

void UWidgetBinder::UpdateMessages(float DeltaTime) {
	if (!MessageLog_) return;

	for (int32 i = MessageQueue_.Num() - 1; i >= 0; --i) {
		MessageQueue_[i].Remaining -= DeltaTime;
		if (MessageQueue_[i].Remaining <= 0.0f) {
			MessageQueue_.RemoveAt(i);
			bMessagesDirty_ = true;
		}
	}
}

void UWidgetBinder::RebuildMessageLog() {
	if (!MessageLog_) return;

	MessageLog_->ClearChildren();
	for (const FPendingMessage& Msg : MessageQueue_) {
		UTextBlock* Entry = NewObject<UTextBlock>(Widget_->WidgetTree);
		Entry->SetText(FText::FromString(Msg.Text));
		Entry->SetColorAndOpacity(FLinearColor::White);
		MessageLog_->AddChildToVerticalBox(Entry);
	}
}

void UWidgetBinder::BindPlot(FName WidgetName, const float* Samples, const float* Envelope,
	int32 Capacity, const int32* Head, float RangeMin, float RangeMax) {
	if (auto* Found = CachedPlots_.Find(WidgetName)) {
		if (*Found) (*Found)->BindHistory(Samples, Envelope, Capacity, Head, RangeMin, RangeMax);
	}
}

void UWidgetBinder::SetPlotThreshold(FName WidgetName, float ThresholdHi, float ThresholdLo){
	if (auto* Found = CachedPlots_.Find(WidgetName)){
		if (*Found) {
			(*Found)->Config.ThresholdHi = ThresholdHi;
			(*Found)->Config.ThresholdLo = ThresholdLo;
		}
	}
}

void UWidgetBinder::SetExpansion(float Alpha) {
	if (!Layer_ || !bIsBound_) return;
	if (CollapsedWidth_ <= 0.0f || ExpandedWidth_ <= CollapsedWidth_) return;

	float CurrentWidth = FMath::Lerp(CollapsedWidth_, ExpandedWidth_, Alpha);
	if (FMath::IsNearlyEqual(CurrentWidth, QuadSize_.X, 0.5f)) return;

	QuadSize_.X = CurrentWidth;
	Layer_->SetQuadSize(QuadSize_);

	float RightEdgeY = (ExpandedWidth_ * 0.5f) + LayerVerticalOffset_;
	float CenterY = RightEdgeY - (CurrentWidth * 0.5f);
	FVector CurrentLocal = Layer_->GetRelativeLocation();
	Layer_->SetRelativeLocation(FVector(CurrentLocal.X, CenterY, CurrentLocal.Z));
}

void UWidgetBinder::SetLayerOpacity(float Opacity) {
	if (!Layer_ || !bIsBound_) return;
	//Layer_->SetLayerOpacity(FMath::Clamp(Opacity, 0.0f, 1.0f));
}

void UWidgetBinder::SetVisibility(FName WidgetName, bool bVisible)
{
	if (auto* Found = CachedWidgets_.Find(WidgetName))
	{
		if (*Found)
			(*Found)->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UWidgetBinder::SetImageColor(FName WidgetName, const FLinearColor& Color) {
	if (auto* Found = CachedImages_.Find(WidgetName)) {
		if (*Found) (*Found)->SetColorAndOpacity(Color);
	}
}

FVector2D UWidgetBinder::GetWidgetSlotPosition(FName WidgetName) const {
    // Reads the raw slot position from the widget's UCanvasPanelSlot (widget centre when alignment is 0.5,0.5).
    UWidget* const* Found = CachedWidgets_.Find(WidgetName);
    if (!Found || !*Found) return FVector2D::ZeroVector;
    UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>((*Found)->Slot);
    return Slot ? Slot->GetPosition() : FVector2D::ZeroVector;
}

FVector2D UWidgetBinder::GetWidgetSize(FName WidgetName) const {
    // Returns the pixel size of the named widget from the cached rect.
    const FWidgetRect* R = WidgetRects_.Find(WidgetName);
    return R ? R->Size : FVector2D::ZeroVector;
}

bool UWidgetBinder::IsGazeOverWidget(FName WidgetName, float MarginPx) const {
    // Projects current gaze to render-target pixel space and checks against the widget's cached rect.
    FVector2D UV;
    if (!ProjectGazeToUV(UV)) return false;
    const FWidgetRect* R = WidgetRects_.Find(WidgetName);
    if (!R) return false;
    FVector2D P = UV * RenderSize_;
    return P.X >= R->Position.X - MarginPx && P.X <= R->Position.X + R->Size.X + MarginPx
        && P.Y >= R->Position.Y - MarginPx && P.Y <= R->Position.Y + R->Size.Y + MarginPx;
}

void UWidgetBinder::SetWidgetBounds(FName WidgetName, FVector2D BaseSlotPosition, FVector2D NormalSize, FVector2D TargetSize, FVector2D ExpandDirection) {
    // Moves and resizes a canvas-slot widget; the corner opposite ExpandDirection stays anchored.
    UWidget* const* Found = CachedWidgets_.Find(WidgetName);
    if (!Found || !*Found) return;
    UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>((*Found)->Slot);
    if (!Slot) return;
    FVector2D NewPos = BaseSlotPosition + (TargetSize - NormalSize) * 0.5f * ExpandDirection;
    Slot->SetPosition(NewPos);
    Slot->SetSize(TargetSize);
    FVector2D AnchorPos(RenderSize_.X * Slot->GetAnchors().Minimum.X, RenderSize_.Y * Slot->GetAnchors().Minimum.Y);
    FWidgetRect& Rect = WidgetRects_.FindOrAdd(WidgetName);
    Rect.Position = AnchorPos + NewPos - TargetSize * Slot->GetAlignment();
    Rect.Size = TargetSize;
}

void UWidgetBinder::SetWidgetRenderScale(FName WidgetName, FVector2D Scale, FVector2D Pivot) {
    // Scales the widget and all its children via render transform; pivot controls which corner stays fixed.
    UWidget* const* Found = CachedWidgets_.Find(WidgetName);
    if (!Found || !*Found) return;
    (*Found)->SetRenderTransformPivot(Pivot);
    FWidgetTransform T = (*Found)->GetRenderTransform();
    T.Scale = Scale;
    (*Found)->SetRenderTransform(T);
}

void UWidgetBinder::SetImageTexture(FName WidgetName, UTexture2D* Texture) {
	if (!Texture) return;
	if (auto* Found = CachedImages_.Find(WidgetName)) {
		if (!*Found) return;
		const int32 W = Texture->GetSizeX();
		const int32 H = Texture->GetSizeY();
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(W, H);
		Brush.DrawAs   = ESlateBrushDrawType::Image;
		(*Found)->SetBrush(Brush);
	}
}
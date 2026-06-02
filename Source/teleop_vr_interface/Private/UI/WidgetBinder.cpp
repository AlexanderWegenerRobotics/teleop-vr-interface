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
			if (!MessageLog_) MessageLog_ = VBox;
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
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
#include "Slate/WidgetRenderer.h"

UWidgetBinder::UWidgetBinder() {
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UWidgetBinder::BeginPlay() {
	Super::BeginPlay();
}

void UWidgetBinder::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	WidgetRenderer_.Reset();
	Super::EndPlay(EndPlayReason);
}

void UWidgetBinder::Initialize(TSubclassOf<UUserWidget> WidgetClass, UCameraComponent* Camera, FVector2D RenderSize, float Distance, int32 Priority) {
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

	Layer_ = NewObject<UStereoLayerComponent>(GetOwner(), TEXT("UILayer"));
	Layer_->SetTexture(RenderTarget_);
	Layer_->bLiveTexture = true;
	Layer_->bSupportsDepth = false;
	Layer_->SetPriority(Priority);
	Layer_->AttachToComponent(Camera, FAttachmentTransformRules::KeepRelativeTransform);
	Layer_->SetRelativeLocation(FVector(Distance, 0.0f, 0.0f));
	QuadSize_ = RenderSize;
	Layer_->SetQuadSize(QuadSize_);
	Layer_->RegisterComponent();

	DiscoverWidgets();
	CacheWidgetRects();
	bIsBound_ = true;
}

void UWidgetBinder::DiscoverWidgets() {
	if (!Widget_ || !Widget_->WidgetTree) return;

	CachedButtons_.Empty();
	CachedTextBlocks_.Empty();
	CachedPlots_.Empty();
	MessageLog_ = nullptr;

	Widget_->WidgetTree->ForEachWidget([this](UWidget* W) {
		if (UButton* Btn = Cast<UButton>(W)) {
			CachedButtons_.Add(W->GetFName(), Btn);
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
		});
}

void UWidgetBinder::CacheWidgetRects() {
	WidgetRects_.Empty();
	ButtonRects_.Empty();

	if (!Widget_ || !Widget_->WidgetTree) return;

	Widget_->WidgetTree->ForEachWidget([this](UWidget* W) {
		UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(W->Slot);
		if (!Slot) return;

		FWidgetRect Rect;
		FVector2D SlotPos = Slot->GetPosition();
		FVector2D SlotSize = Slot->GetSize();
		FVector2D Alignment = Slot->GetAlignment();
		FAnchors Anchors = Slot->GetAnchors();

		FVector2D AnchorPos(RenderSize_.X * Anchors.Minimum.X, RenderSize_.Y * Anchors.Minimum.Y);
		Rect.Position.X = AnchorPos.X + SlotPos.X - (SlotSize.X * Alignment.X);
		Rect.Position.Y = AnchorPos.Y + SlotPos.Y - (SlotSize.Y * Alignment.Y);
		Rect.Size = SlotSize;

		FName Name = W->GetFName();
		WidgetRects_.Add(Name, Rect);

		if (CachedButtons_.Contains(Name)) {
			ButtonRects_.Add(Name, Rect);
		}
		});
}

void UWidgetBinder::RenderWidget() {
	if (!Widget_ || !RenderTarget_ || !WidgetRenderer_) return;
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

void UWidgetBinder::SetButtonToLocked(FName Name) {
	if (Name == FName()) return;
	if (auto* Found = CachedButtons_.Find(Name)) {
		if (*Found) {
			FLinearColor DisabledColor = (*Found)->WidgetStyle.Disabled.TintColor.GetSpecifiedColor();
			(*Found)->SetBackgroundColor(DisabledColor);
		}
	}
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
			if (auto* Found = CachedButtons_.Find(HoveredButton_)) {
				if (*Found) {
					FLinearColor PressFlashColor = (*Found)->WidgetStyle.Pressed.TintColor.GetSpecifiedColor();
					(*Found)->SetBackgroundColor(PressFlashColor);
					FlashingButton_ = HoveredButton_;
					FlashRemaining_ = PressFlashDuration_;
				}
			}
		}
	}

	UpdatePressFlash(DeltaTime);
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

	OutUV.X = (HitY / QuadSize_.X) + 0.5f;
	OutUV.Y = (-HitZ / QuadSize_.Y) + 0.5f;

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
	if (Name == FName() || Name == FlashingButton_) return;
	if (LockedButtons_.Contains(Name)) {
		SetButtonToLocked(Name);
		return;
	}
	if (auto* Found = CachedButtons_.Find(Name)) {
		if (*Found) {
			FLinearColor NormalColor = (*Found)->WidgetStyle.Normal.TintColor.GetSpecifiedColor();
			(*Found)->SetBackgroundColor(NormalColor);
		}
	}
}

void UWidgetBinder::SetButtonToHovered(FName Name) {
	if (Name == FName() || Name == FlashingButton_) return;
	if (auto* Found = CachedButtons_.Find(Name)) {
		if (*Found) {
			FLinearColor HoverColor = (*Found)->WidgetStyle.Hovered.TintColor.GetSpecifiedColor();
			(*Found)->SetBackgroundColor(HoverColor);
		}
	}
}

void UWidgetBinder::UpdatePressFlash(float DeltaTime) {
	if (FlashingButton_ == FName()) return;

	FlashRemaining_ -= DeltaTime;
	if (FlashRemaining_ > 0.0f) return;

	if (auto* Found = CachedButtons_.Find(FlashingButton_)) {
		if (*Found) {
			if (LockedButtons_.Contains(FlashingButton_)) {
				FLinearColor LockedColor = (*Found)->WidgetStyle.Disabled.TintColor.GetSpecifiedColor();
				(*Found)->SetBackgroundColor(LockedColor);
			}
			else if (FlashingButton_ == HoveredButton_) {
				FLinearColor HoverColor = (*Found)->WidgetStyle.Hovered.TintColor.GetSpecifiedColor();
				(*Found)->SetBackgroundColor(HoverColor);
			}
			else {
				FLinearColor NormalColor = (*Found)->WidgetStyle.Normal.TintColor.GetSpecifiedColor();
				(*Found)->SetBackgroundColor(NormalColor);
			}
		}
	}

	FlashingButton_ = FName();
	FlashRemaining_ = 0.0f;
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
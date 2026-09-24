#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/StereoLayerComponent.h"
#include "UI/TimeSeriesWidget.h"
#include "UI/GazeComponent.h"
#include "UI/WinkGesture.h"
#include "WidgetBinder.generated.h"

class UUserWidget;
class UCameraComponent;
class UTextBlock;
class UButton;
class UVerticalBox;
class UTextureRenderTarget2D;
class FWidgetRenderer;
class UImage;
class UBorder;

USTRUCT()
struct FPendingMessage {
	GENERATED_BODY()
	FString Text;
	float Remaining = 0.0f;
};

USTRUCT()
struct FWidgetRect {
	GENERATED_BODY()
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UWidgetBinder : public UActorComponent {
	GENERATED_BODY()

public:
	UWidgetBinder();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void Initialize(TSubclassOf<UUserWidget> WidgetClass, UCameraComponent* Camera, FVector2D RenderSize, float Distance, int32 Priority,
		float CollapsedWidth = 0.0f, float ExpandedWidth = 0.0f, float VerticalOffset = 0.0f);

	void SetGazeInput(const FGazeData& GazeData);
	void SetText(FName WidgetName, const FString& Text);
	void SetTextColor(FName WidgetName, const FLinearColor& Color);
	void PushMessage(const FString& Text, float Duration);
	void BindPlot(FName WidgetName, const float* Samples, const float* Envelope, int32 Capacity, const int32* Head, float RangeMin, float RangeMax);
	void SetPlotThreshold(FName WidgetName, float ThresholdHi, float ThresholdLo = 0.0f);

	void SetButtonLocked(FName ButtonName, bool bLocked);
	bool IsButtonLocked(FName ButtonName) const;

	// Programmatically set or clear the persistent toggled (pressed) visual state.
	void SetButtonToggled(FName ButtonName, bool bToggled);
	bool IsButtonToggled(FName ButtonName) const;

	void SetVisibility(FName WidgetName, bool bVisible);
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget_; }
	void SetImageColor(FName WidgetName, const FLinearColor& Color);
	void SetBorderColor(FName WidgetName, const FLinearColor& Color);
	void SetImageTexture(FName WidgetName, UTexture2D* Texture);

	// Returns the canvas slot position (widget center when alignment is 0.5,0.5).
	FVector2D GetWidgetSlotPosition(FName WidgetName) const;
	// Returns the cached render-target pixel size of a named widget.
	FVector2D GetWidgetSize(FName WidgetName) const;
	// True when the projected gaze pixel falls within the named widget's cached rect expanded by MarginPx.
	bool IsGazeOverWidget(FName WidgetName, float MarginPx = 0.f) const;
	// Resizes a canvas-panel widget and adjusts its slot position so the corner opposite ExpandDirection stays fixed.
	// ExpandDirection components: 1=expand right/down, -1=expand left/up, 0=expand from centre.
	void SetWidgetBounds(FName WidgetName, FVector2D BaseSlotPosition, FVector2D NormalSize, FVector2D TargetSize, FVector2D ExpandDirection);
	// Applies a render-transform scale to a widget (scales all children). Pivot=(0,0) anchors top-left, (1,1) anchors bottom-right.
	void SetWidgetRenderScale(FName WidgetName, FVector2D Scale, FVector2D Pivot);

	void ShowCameraMenu(const TArray<FString>& StreamNames, const FString& ActiveStream);
	void ShowResetMenu();
	void HideMenu();

	FName GetHoveredButton() const { return HoveredButton_; }
	FName ConsumePress();
	FName ConsumeRejection();
	bool IsWinkActive() const;
	void SetExpansion(float Alpha);
	void SetLayerOpacity(float Opacity);

	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bPrintDebugInfo = false;

	// How often the widget tree is rasterised into the stereo layer, in Hz.
	// Deliberately not the frame rate -- see the throttle in TickComponent.
	// Hover and click bypass it, so this paces only the numbers and plots.
	// <= 0 renders every tick, i.e. the previous behaviour.
	UPROPERTY(EditAnywhere, Category = "Performance", meta = (ClampMin = "0.0"))
	float RenderRateHz = 10.0f;

	// Invisible slack added around every button's hit rect, in render-target
	// pixels. Gaze is not a mouse: the ray carries tracker noise, a calibration
	// offset and the operator's own microsaccades, so a button that is exactly
	// as big as it looks is harder to hold than it appears -- the wink lands in
	// the two-pixel gap between the rect and where the eye actually was.
	//
	// This grows the target without growing the graphic, which is the whole
	// point: the HUD sits over the video and every pixel of panel is a pixel of
	// workspace the operator cannot see. Making a button easier to hit by
	// drawing it bigger costs sight; making its hit rect bigger costs nothing
	// until two rects start to overlap, which is what the exact-hit-first rule
	// in FindButtonAtUV handles.
	//
	// 0 restores the previous pixel-exact behaviour.
	UPROPERTY(EditAnywhere, Category = "Gaze", meta = (ClampMin = "0.0"))
	float GazeHitMarginPx = 12.0f;

private:
	void DiscoverWidgets();
	void CacheWidgetRects();
	void RenderWidget();
	bool ProjectGazeToUV(FVector2D& OutUV) const;
	FName FindButtonAtUV(const FVector2D& UV) const;
	void SetButtonToNormal(FName Name);
	void SetButtonToHovered(FName Name);
	void SetButtonToLocked(FName Name);
	void ApplyButtonStyle(FName Name, const FSlateBrush& Brush);
	void UpdateMessages(float DeltaTime);
	void RebuildMessageLog();

	UCameraComponent* Camera_ = nullptr;
	float LayerDistance_ = 0.0f;
	FVector2D RenderSize_ = FVector2D::ZeroVector;
	FVector2D QuadSize_ = FVector2D::ZeroVector;
	float CollapsedWidth_ = 0.0f;
	float ExpandedWidth_ = 0.0f;
	float LayerVerticalOffset_ = 0.0f;

	FVector GazeLocalOrigin_ = FVector::ZeroVector;
	FVector GazeLocalDirection_ = FVector::ForwardVector;

	FGazeData LatestGaze_;
	FWinkGesture WinkGesture_;

	UPROPERTY()
	UUserWidget* Widget_ = nullptr;

	UPROPERTY()
	UStereoLayerComponent* Layer_ = nullptr;

	UPROPERTY()
	UTextureRenderTarget2D* RenderTarget_ = nullptr;

	TSharedPtr<FWidgetRenderer> WidgetRenderer_;

	UPROPERTY() TMap<FName, UWidget*> CachedWidgets_;
	UPROPERTY() TMap<FName, UButton*> CachedButtons_;
	UPROPERTY() TMap<FName, UTextBlock*> CachedTextBlocks_;
	UPROPERTY() TMap<FName, UTimeSeriesWidget*> CachedPlots_;
	UPROPERTY() TMap<FName, FWidgetRect> WidgetRects_;
	UPROPERTY() TMap<FName, FWidgetRect> ButtonRects_;
	UPROPERTY() TMap<FName, UImage*> CachedImages_;
	UPROPERTY() TMap<FName, UBorder*> CachedBorders_;

	// Message sink. Bound by explicit widget name only -- an earlier version
	// claimed the first unnamed VerticalBox in tree order, which silently
	// captured whatever panel happened to come first (the START/ENGAGE stack)
	// and then destroyed its children on the first RebuildMessageLog.
	// MessageText_ is the fallback for widgets that carry a single TextBlock
	// instead of a list. Both null is legal: messages then expire unseen.
	UVerticalBox* MessageLog_      = nullptr;
	UTextBlock*   MessageText_     = nullptr;
	bool          bMessageSinkWarned_ = false;
	UVerticalBox* CameraMenuList_  = nullptr;
	UVerticalBox* ResetMenuList_   = nullptr;

	TArray<FName> DynamicMenuItems_;
	bool          bMenuRectsDirty_   = false;
	bool          bStaticRectsDirty_ = false;

	TSet<FName> LockedButtons_;
	TSet<FName> ToggledButtons_;

	// Original button styles cached at discovery time; used to build per-state overrides.
	TMap<FName, FButtonStyle> OriginalStyles_;

	FName HoveredButton_  = FName();
	FName PressedButton_  = FName();
	FName RejectedButton_ = FName();

	TArray<FPendingMessage> MessageQueue_;
	bool bMessagesDirty_ = false;

	bool bIsBound_ = false;
	double LastLogTime_ = 0.0;

	// HUD raster pacing (see TickComponent). RenderAccum_ carries time toward
	// the next slot; bRenderDirty_ forces a draw on the tick something the
	// operator interacted with changed, so input never waits for the clock.
	float RenderAccum_  = 0.0f;
	bool  bRenderDirty_ = true;   // the first tick after binding always draws
};
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
	void HideMenu();

	FName GetHoveredButton() const { return HoveredButton_; }
	FName ConsumePress();
	FName ConsumeRejection();
	bool IsWinkActive() const;
	void SetExpansion(float Alpha);
	void SetLayerOpacity(float Opacity);

	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bPrintDebugInfo = false;

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
	UVerticalBox* MessageLog_      = nullptr;
	UVerticalBox* CameraMenuList_  = nullptr;

	TArray<FName> DynamicMenuItems_;
	bool          bMenuRectsDirty_ = false;

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
};
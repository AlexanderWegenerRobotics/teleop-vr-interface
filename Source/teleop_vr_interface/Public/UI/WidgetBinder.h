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

	void SetButtonLocked(FName ButtonName, bool bLocked);
	bool IsButtonLocked(FName ButtonName) const;

	FName GetHoveredButton() const { return HoveredButton_; }
	FName ConsumePress();
	FName ConsumeRejection();
	bool IsWinkActive() const;
	void SetExpansion(float Alpha);
	void SetLayerOpacity(float Opacity);

	UPROPERTY(EditAnywhere, Category = "Gaze")
	float PressFlashDuration_ = 0.5f;

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
	void UpdatePressFlash(float DeltaTime);
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

	TMap<FName, UButton*> CachedButtons_;
	TMap<FName, UTextBlock*> CachedTextBlocks_;
	TMap<FName, UTimeSeriesWidget*> CachedPlots_;
	TMap<FName, FWidgetRect> WidgetRects_;
	TMap<FName, FWidgetRect> ButtonRects_;
	UVerticalBox* MessageLog_ = nullptr;

	TSet<FName> LockedButtons_;

	FName HoveredButton_ = FName();
	FName PressedButton_ = FName();
	FName RejectedButton_ = FName();

	FName FlashingButton_ = FName();
	float FlashRemaining_ = 0.0f;

	TArray<FPendingMessage> MessageQueue_;
	bool bMessagesDirty_ = false;

	bool bIsBound_ = false;
	double LastLogTime_ = 0.0;
};
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HUDPanelBase.h"
#include "HUDComponent.generated.h"

class UWidgetComponent;
class UWidgetInteractionComponent;
class UCameraComponent;
class UVideoFeedComponent;
class UComLink;

/**
 * Panel placement relative to the video feed
 */
UENUM(BlueprintType)
enum class EHUDPlacement : uint8
{
	Left,
	Right,
	Top,
	Bottom,
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight,
	Center,		// Overlays the video feed (for alerts)
};

/**
 * Panel registration info
 */
USTRUCT()
struct FHUDPanelEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FName PanelID;

	UPROPERTY()
	TSubclassOf<UHUDPanelBase> WidgetClass;

	UPROPERTY()
	TObjectPtr<UWidgetComponent> WidgetComponent;

	UPROPERTY()
	TObjectPtr<UHUDPanelBase> WidgetInstance;

	EHUDPlacement Placement = EHUDPlacement::Right;
	FVector2D Size = FVector2D(400.0f, 300.0f);	// Widget draw size
	bool bStartVisible = true;
};

/**
 * HUDComponent
 *
 * Manages the operator's HUD in VR:
 * - Registers and positions widget panels around the video feed
 * - Supports visibility presets (debug, operator, minimal)
 * - Sets up controller ray interaction with widgets
 * - Provides data context to panels for live system data
 *
 * Usage:
 *   - Add to OperatorPawn
 *   - Call RegisterPanel() to add widget panels
 *   - Panels are positioned automatically based on placement enum
 *   - Switch presets with ShowPreset()
 */
UCLASS(ClassGroup = (TeleOp), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UHUDComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHUDComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Panel management ---

	/** Register a panel by class. Returns the created widget instance. */
	UHUDPanelBase* RegisterPanel(FName PanelID, TSubclassOf<UHUDPanelBase> WidgetClass, EHUDPlacement Placement, 
		FVector2D DrawSize = FVector2D(400.0f, 300.0f), bool bStartVisible = true);

	/** Show a specific panel by ID */
	void ShowPanel(FName PanelID);

	/** Hide a specific panel by ID */
	void HidePanel(FName PanelID);

	/** Toggle a panel's visibility */
	void TogglePanel(FName PanelID);

	/** Show a group of panels by IDs */
	void ShowPreset(const TArray<FName>& PanelIDs);

	/** Hide all panels */
	void HideAll();

	/** Get a panel widget instance by ID */
	UHUDPanelBase* GetPanel(FName PanelID) const;

	// --- Configuration ---

	/** Distance from camera to HUD panels in cm (should be slightly less than video plane) */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float HUDDistance = 400.0f;

	/** Scale factor for widget components in world space */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float WorldScale = 0.5f;

	/** Horizontal spread for side panels in cm */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float SideOffset = 120.0f;

	/** Vertical spread for top/bottom panels in cm */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float VerticalOffset = 50.0f;

	void PressUI();
	void ReleaseUI();

private:

	void SetupInteraction();
	FVector GetPlacementOffset(EHUDPlacement Placement) const;
	FRotator GetPlacementRotation(EHUDPlacement Placement) const;

	UPROPERTY()
	TArray<FHUDPanelEntry> Panels;

	UPROPERTY()
	TObjectPtr<UCameraComponent> CameraRef;

	UPROPERTY()
	TObjectPtr<UVideoFeedComponent> VideoFeedRef;

	UPROPERTY()
	TObjectPtr<UComLink> ComLinkRef;

	UPROPERTY()
	TObjectPtr<UWidgetInteractionComponent> RightHandInteraction;
};
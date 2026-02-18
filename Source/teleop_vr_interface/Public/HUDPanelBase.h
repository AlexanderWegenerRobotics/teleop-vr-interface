#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HUDPanelBase.generated.h"


UCLASS(Abstract, Blueprintable)
class TELEOP_VR_INTERFACE_API UHUDPanelBase : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Panel identifier — set by HUDComponent on registration */
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FName PanelID;

	/** Whether this panel is currently meant to be shown */
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bPanelActive = false;

	// --- Visibility control ---

	/** Show the panel with optional fade */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void ShowPanel(float FadeDuration = 0.2f);

	/** Hide the panel with optional fade */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void HidePanel(float FadeDuration = 0.2f);

	/** Set panel opacity directly (0.0 = invisible, 1.0 = fully visible) */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetPanelOpacity(float Opacity);

	// --- Data access (Blueprint binds to these) ---

	/** Current system mode as display string */
	UFUNCTION(BlueprintCallable, Category = "HUD|Data")
	FText GetModeText() const;

	/** Stream FPS */
	UFUNCTION(BlueprintCallable, Category = "HUD|Data")
	int32 GetStreamFPS() const;

	/** Stream latency in ms */
	UFUNCTION(BlueprintCallable, Category = "HUD|Data")
	float GetStreamLatency() const;

	/** Stream jitter in ms */
	UFUNCTION(BlueprintCallable, Category = "HUD|Data")
	float GetStreamJitter() const;

	/** Packet loss percentage */
	UFUNCTION(BlueprintCallable, Category = "HUD|Data")
	float GetPacketLoss() const;

	/** Communication link latency in ms */
	UFUNCTION(BlueprintCallable, Category = "HUD|Data")
	float GetComLatency() const;

	/** Whether the communication link is connected */
	UFUNCTION(BlueprintCallable, Category = "HUD|Data")
	bool GetComConnected() const;

	/** Whether video is being received */
	UFUNCTION(BlueprintCallable, Category = "HUD|Data")
	bool GetStreamConnected() const;

	// --- Event that Blueprint can override ---

	/** Called when panel becomes active — use for animations */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnPanelShown();

	/** Called when panel becomes inactive */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnPanelHidden();

	/** Called when a button in this panel is pressed — override in subclass */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnButtonPressed(FName ButtonID);

	// --- Internal: set by HUDComponent ---

	/** Cache references so panels can pull live data without knowing about components directly */
	void SetDataContext(class UVideoFeedComponent* VideoFeed, class UComLink* ComLink);

	void SetStateContext(class UStateComponent* StateComp);

protected:

	UPROPERTY()
	TObjectPtr<class UVideoFeedComponent> VideoFeedRef;

	UPROPERTY()
	TObjectPtr<class UComLink> ComLinkRef;

	UPROPERTY()
	TObjectPtr<class UStateComponent> StateRef;

	float TargetOpacity = 1.0f;
};
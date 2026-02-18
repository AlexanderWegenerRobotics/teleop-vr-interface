#include "HUDPanelBase.h"
#include "VideoFeedComponent.h"
#include "ComLink.h"
#include "Components/PanelWidget.h"
#include "Animation/UMGSequencePlayer.h"
#include "StateComponent.h"

void UHUDPanelBase::SetDataContext(UVideoFeedComponent* VideoFeed, UComLink* ComLink)
{
	VideoFeedRef = VideoFeed;
	ComLinkRef = ComLink;
}

void UHUDPanelBase::ShowPanel(float FadeDuration)
{
	bPanelActive = true;
	SetVisibility(ESlateVisibility::Visible);
	SetPanelOpacity(1.0f);
	OnPanelShown();
}

void UHUDPanelBase::HidePanel(float FadeDuration)
{
	bPanelActive = false;
	SetPanelOpacity(0.0f);
	SetVisibility(ESlateVisibility::Hidden);
	OnPanelHidden();
}

void UHUDPanelBase::SetPanelOpacity(float Opacity)
{
	TargetOpacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	SetRenderOpacity(TargetOpacity);
}

// ============================================================================
// Data access — panels call these to get live system data
// ============================================================================

FText UHUDPanelBase::GetModeText() const {
	if (StateRef) {
		return StateRef->GetStateText();
	}
	return FText::FromString(TEXT("NO STATE"));
}

void UHUDPanelBase::SetStateContext(UStateComponent* StateComp) {
	StateRef = StateComp;
}

int32 UHUDPanelBase::GetStreamFPS() const
{
	if (VideoFeedRef)
	{
		return VideoFeedRef->GetActiveStats().CurrentFPS;
	}
	return 0;
}

float UHUDPanelBase::GetStreamLatency() const
{
	if (VideoFeedRef)
	{
		return VideoFeedRef->GetActiveStats().LatencyMs;
	}
	return 0.0f;
}

float UHUDPanelBase::GetStreamJitter() const
{
	if (VideoFeedRef)
	{
		return VideoFeedRef->GetActiveStats().JitterMs;
	}
	return 0.0f;
}

float UHUDPanelBase::GetPacketLoss() const
{
	if (VideoFeedRef)
	{
		return VideoFeedRef->GetActiveStats().PacketLossPercent;
	}
	return 0.0f;
}

float UHUDPanelBase::GetComLatency() const
{
	if (ComLinkRef)
	{
		return ComLinkRef->GetLatencyMs();
	}
	return 0.0f;
}

bool UHUDPanelBase::GetComConnected() const
{
	if (ComLinkRef)
	{
		return ComLinkRef->IsConnected();
	}
	return false;
}

bool UHUDPanelBase::GetStreamConnected() const
{
	if (VideoFeedRef)
	{
		return VideoFeedRef->IsReceiving();
	}
	return false;
}
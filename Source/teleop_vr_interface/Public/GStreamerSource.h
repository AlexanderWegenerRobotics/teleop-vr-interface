#pragma once

#include "IVideoSource.h"
#include "GStreamerVideoReceiver.h"

/**
 * GStreamerSource
 *
 * Wraps FGStreamerVideoReceiver behind the IVideoSource interface.
 * Configurable for port, decoder type, and transport protocol.
 */
class FGStreamerSource : public IVideoSource
{
public:

	struct FConfig
	{
		int32 Port = 5000;
		int32 SRTLatencyMs = 125;
		bool bUseHardwareDecoder = true;
	};

	FGStreamerSource(const FConfig& InConfig)
		: Config(InConfig)
	{
	}

	virtual ~FGStreamerSource() override
	{
		Stop();
	}

	virtual bool Initialize() override
	{
		Receiver = MakeUnique<FGStreamerVideoReceiver>();
		return Receiver->Initialize(Config.Port, Config.SRTLatencyMs, Config.bUseHardwareDecoder);
	}

	virtual bool Start() override
	{
		if (!Receiver) return false;
		return Receiver->Start();
	}

	virtual void Stop() override
	{
		if (Receiver)
		{
			Receiver->Stop();
		}
	}

	virtual bool UpdateTexture(UTexture2D* Texture) override
	{
		if (!Receiver) return false;
		return Receiver->UpdateTexture(Texture);
	}

	virtual bool GetDimensions(int32& OutWidth, int32& OutHeight) const override
	{
		if (!Receiver) return false;
		Receiver->GetDimensions(OutWidth, OutHeight);
		return (OutWidth > 0 && OutHeight > 0);
	}

	virtual FVideoSourceStats GetStats() const override
	{
		FVideoSourceStats Stats;
		if (!Receiver) return Stats;

		FGStreamerStats GStats = Receiver->GetStatistics();
		Stats.CurrentFPS = GStats.CurrentFPS;
		Stats.PacketLossPercent = GStats.PacketLossPercent;
		Stats.LatencyMs = static_cast<float>(GStats.PipelineLatencyMs);
		Stats.JitterMs = static_cast<float>(GStats.AverageJitterMs);
		Stats.RoundTripMs = static_cast<float>(GStats.SRTRoundTripMs);
		Stats.bIsReceiving = (GStats.CurrentFPS > 0);
		return Stats;
	}

	virtual FString GetSourceName() const override
	{
		return FString::Printf(TEXT("GStreamer (port %d, %s)"),
			Config.Port,
			Config.bUseHardwareDecoder ? TEXT("NVDEC") : TEXT("CPU"));
	}

private:
	FConfig Config;
	TUniquePtr<FGStreamerVideoReceiver> Receiver;
};

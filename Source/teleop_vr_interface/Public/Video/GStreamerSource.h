#pragma once

#include "IVideoSource.h"
#include "GStreamerVideoReceiver.h"

class FGStreamerSource : public IVideoSource {
public:
    explicit FGStreamerSource(const FReceiverConfig& InConfig)
        : Config(InConfig) {}

    virtual ~FGStreamerSource() override { Stop(); }

    virtual bool Initialize() override {
        Receiver = MakeUnique<FGStreamerVideoReceiver>();
        return Receiver->Initialize(Config);
    }

    virtual bool Start() override {
        if (!Receiver) return false;
        return Receiver->Start();
    }

    virtual void Stop() override {
        if (Receiver) Receiver->Stop();
    }

    virtual bool UpdateTexture(UTexture2D*& OutTexture) override {
        if (!Receiver) return false;
        return Receiver->UpdateTexture(OutTexture);
    }

    virtual bool GetDimensions(int32& OutWidth, int32& OutHeight) const override {
        if (!Receiver) return false;
        Receiver->GetDimensions(OutWidth, OutHeight);
        return (OutWidth > 0 && OutHeight > 0);
    }

    virtual int64 GetLastSenderTimeNs() const override
    {
        return Receiver ? Receiver->GetLastSenderTimeNs() : 0;
    }

    virtual uint64 GetLastFrameId() const override
    {
        return Receiver ? Receiver->GetLastFrameId() : 0;
    }

    virtual FVideoSourceStats GetStats() const override {
        FVideoSourceStats Out;
        if (!Receiver) return Out;

        FReceiverStats S = Receiver->GetStats();
        Out.CurrentFPS          = S.CurrentFPS;
        Out.PacketLossPercent   = S.PacketLossPercent;
        Out.PostFecLossPercent  = S.PostFecLossPercent;
        Out.OneWayLatencyMs     = S.OneWayLatencyMs;
        Out.JitterMs            = S.JitterMs;
        Out.FrameIntervalVarianceMs = S.FrameIntervalVarianceMs;
        Out.PacketsReceived     = static_cast<int64>(S.PacketsReceived);
        Out.PacketsLost         = static_cast<int64>(S.PacketsLost);
        Out.PacketsRecovered    = static_cast<int64>(S.PacketsRecovered);
        Out.PacketsLostPostFec  = static_cast<int64>(S.PacketsLostPostFec);
        Out.FramesDecoded       = static_cast<int64>(S.FramesDecoded);
        Out.bIsReceiving        = S.bIsReceiving;
        Out.StreamHealthState = S.StreamHealthState;
        return Out;
    }

    virtual FString GetSourceName() const override {
        return FString::Printf(TEXT("GStreamer UDP RTP (port %d)"), Config.Port);
    }

private:
    FReceiverConfig                    Config;
    TUniquePtr<FGStreamerVideoReceiver> Receiver;
};

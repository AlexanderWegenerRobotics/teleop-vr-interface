#pragma once

#include "IVideoSource.h"
#include "Networking/UdpSocket.h"

// Receive-only JPEG-over-UDP preview feed for a locally attached camera (no codec/RTP,
// same machine only). Registers as an ordinary PiP source next to the GStreamer feeds.
class FLocalPreviewSource : public IVideoSource {
public:
    explicit FLocalPreviewSource(int32 InPort) : Port(InPort) {}
    virtual ~FLocalPreviewSource() override { Stop(); }

    virtual bool Initialize() override;
    virtual bool Start() override;
    virtual void Stop() override;

    virtual bool UpdateTexture(UTexture2D*& OutTexture) override;
    virtual bool GetDimensions(int32& OutWidth, int32& OutHeight) const override;
    virtual FVideoSourceStats GetStats() const override;
    virtual FString GetSourceName() const override {
        return FString::Printf(TEXT("Local JPEG UDP (port %d)"), Port);
    }

private:
    void OnPacket(const uint8* Data, int32 Size);

    int32 Port = 0;
    TUniquePtr<UdpSocket> Socket_;

    mutable FCriticalSection FrameLock_;
    TArray<uint8> PendingPixels_;
    int32 PendingWidth_  = 0;
    int32 PendingHeight_ = 0;
    bool  bHasNewFrame_  = false;
    double LastFrameTime_ = 0.0;

    int32 VideoWidth_  = 0;
    int32 VideoHeight_ = 0;

    TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> bUpdateInFlight_;
    double UpdateInFlightStartTime_ = 0.0;
};

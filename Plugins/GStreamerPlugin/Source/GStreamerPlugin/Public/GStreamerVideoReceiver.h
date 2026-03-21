#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Containers/Queue.h"
#include "StreamStats.h"

// ---------------------------------------------------------------------------
// GStreamer C API
// ---------------------------------------------------------------------------

extern "C" void* GStreamerGetBus(void* pipeline);
extern "C" bool  GStreamerGetJitterBufferStats(void* pipeline,
    unsigned long long* num_pushed, unsigned long long* num_lost,
    double* avg_jitter, unsigned long long* rtx_count);
extern "C" bool  GStreamerGetLatency(void* pipeline,
    double* latency_ms, double* jitter_buffer_latency_ms);
extern "C" void  GStreamerUnrefBus(void* bus);
extern "C" void* GStreamerCreatePipeline(const char* description);
extern "C" bool  GStreamerStartPipeline(void* pipeline);
extern "C" void  GStreamerStopPipeline(void* pipeline);
extern "C" void  GStreamerDestroyPipeline(void* pipeline);
extern "C" void* GStreamerGetElementByName(void* pipeline, const char* name);
extern "C" void* GStreamerTryPullSample(void* appsink, double timeout_seconds);
extern "C" void* GStreamerGetSampleBuffer(void* sample);
extern "C" void* GStreamerGetSampleCaps(void* sample);
extern "C" bool  GStreamerGetVideoDimensions(void* caps, int* width, int* height);
extern "C" bool  GStreamerCopyBufferData(void* buffer, unsigned char* dest, int size);
extern "C" int   GStreamerGetBufferSize(void* buffer);
extern "C" void  GStreamerFreeSample(void* sample);
extern "C" void  GStreamerUnrefElement(void* element);

// Probe registration
extern "C" bool GStreamerAddRtpProbe(void* pipeline, const char* element_name,
    void (*callback)(uint16_t seq, uint32_t timestamp, void* userdata),
    void* userdata);

extern "C" bool GStreamerAddFecProbe(void* pipeline, const char* element_name,
    void (*callback)(uint16_t seq, void* userdata),
    void* userdata);

// ---------------------------------------------------------------------------
// Config / stats structs
// ---------------------------------------------------------------------------

struct FReceiverConfig
{
    int32   Port             = 5004;
    int32   FeedbackPort     = 5005;
    FString SenderIP         = TEXT("127.0.0.1");
    int32   ReportIntervalMs = 500;
};

struct FReceiverStats
{
    int32  CurrentFPS                = 0;
    float  PacketLossPercent         = 0.0f;
    float  PostFecLossPercent        = 0.0f;
    float  OneWayLatencyMs           = 0.0f;
    float  JitterMs                  = 0.0f;
    float  FrameIntervalVarianceMs   = 0.0f;
    uint32 PacketsReceived           = 0;
    uint32 PacketsLost               = 0;
    uint32 PacketsRecovered          = 0;
    uint32 PacketsLostPostFec        = 0;
    uint32 FramesDecoded             = 0;
    bool   bIsReceiving              = false;
};

// ---------------------------------------------------------------------------
// Decoded frame container
// ---------------------------------------------------------------------------

struct FVideoFrame
{
    TArray<uint8> Data;
    int32         Width              = 0;
    int32         Height             = 0;
    double        Timestamp          = 0.0;
    uint64        SenderTimestampNs  = 0;   // embedded wall-clock from sender
};

// ---------------------------------------------------------------------------
// Probe callback context
// ---------------------------------------------------------------------------

class FGStreamerVideoReceiver;

struct FProbeContext
{
    FStreamStats* Stats = nullptr;
};

// ---------------------------------------------------------------------------
// Frame pull thread
// ---------------------------------------------------------------------------

class FFramePullRunnable : public FRunnable
{
public:
    FFramePullRunnable(void* InAppSink, FStreamStats* InStats)
        : AppSink(InAppSink), Stats(InStats), bShouldStop(false) {}

    virtual ~FFramePullRunnable() {}
    virtual bool   Init() override { return true; }
    virtual uint32 Run()  override;
    virtual void   Stop() override { bShouldStop = true; }
    virtual void   Exit() override {}

    bool PopFrame(FVideoFrame& OutFrame);
    bool HasPendingFrame() const { return !FrameQueue.IsEmpty(); }

    float GetFrameIntervalVarianceMs() const { return static_cast<float>(FrameIntervalVariance); }

private:
    /// Number of bytes carrying the sender timestamp in the extra row.
    static constexpr int32 kTimestampBytes = 8;

    /// Extract sender timestamp from the last row of a decoded BGRA frame,
    /// crop the frame height by 1, and report latency to Stats.
    void ExtractTimestampRow(FVideoFrame& Frame);

    void*         AppSink;
    FStreamStats* Stats;
    TAtomic<bool> bShouldStop;
    TQueue<FVideoFrame, EQueueMode::Spsc> FrameQueue;

    // Welford online variance for inter-frame intervals
    double LastFrameTimeSec     = 0.0;
    bool   bFirstFrame          = true;
    int64  FrameIntervalCount   = 0;
    double FrameIntervalMean    = 0.0;
    double FrameIntervalM2      = 0.0;
    TAtomic<double> FrameIntervalVariance{ 0.0 };
};

// ---------------------------------------------------------------------------
// Main receiver
// ---------------------------------------------------------------------------

class GSTREAMERPLUGIN_API FGStreamerVideoReceiver
{
public:
    FGStreamerVideoReceiver();
    ~FGStreamerVideoReceiver();

    bool Initialize(const FReceiverConfig& Config);
    bool Start();
    void Stop();

    bool UpdateTexture(UTexture2D*& OutTexture);

    void           GetDimensions(int32& OutWidth, int32& OutHeight) const;
    FReceiverStats GetStats() const;

private:
    static void RtpProbeThunk(uint16_t Seq, uint32_t Ts, void* Userdata);
    static void FecProbeThunk(uint16_t Seq, void* Userdata);

    void* Pipeline  = nullptr;
    void* AppSink   = nullptr;
    void* Bus       = nullptr;

    bool  bIsInitialized = false;

    int32  VideoWidth        = 0;
    int32  VideoHeight       = 0;
    int32  FrameCount        = 0;
    double LastFPSUpdateTime = 0.0;
    int32  CurrentFPS        = 0;
    TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> bUpdateInFlight;

    TUniquePtr<FStreamStats>       Stats_;
    TUniquePtr<FProbeContext>      ProbeCtx_;

    TUniquePtr<FFramePullRunnable> FramePullRunnable_;
    TUniquePtr<FRunnableThread>    FramePullThread_;
};

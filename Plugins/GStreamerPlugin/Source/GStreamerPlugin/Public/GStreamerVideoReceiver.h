#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "RHIResources.h"
#include "Misc/ScopeLock.h"
#include "HAL/CriticalSection.h"
#include <atomic>
#include "StreamStats.h"

class FTextureResource;

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
extern "C" bool  GStreamerWaitForPlaying(void* pipeline, int timeout_ms);
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

extern "C" bool GStreamerNvdecAvailable();
extern "C" bool GStreamerElementAvailable(const char* factory_name);
extern "C" bool GStreamerElementHasProperty(const char* factory_name, const char* property);
extern "C" void* GStreamerMapSample(void* sample, const unsigned char** data, int* size);
extern "C" void  GStreamerUnmapSample(void* handle);
extern "C" bool GStreamerPopBusError(void* bus, char* OutMsg, int OutMsgLen, bool* bOutIsWarning);

// ---------------------------------------------------------------------------
// Config / stats structs
// ---------------------------------------------------------------------------

struct FReceiverConfig
{
    int32   Port = 5004;
    int32   FeedbackPort = 5005;
    int32   StatusPort = 5007;
    FString SenderIP = TEXT("127.0.0.1");
    int32   ReportIntervalMs = 500;
    // rtpjitterbuffer latency: how long it waits for a missing or reordered packet.
    // In-order packets pass straight through (glass-to-glass bench, 2026-10-15: 0 vs 50 ms
    // made no difference on localhost), so this only costs time when packets go missing.
    int32   JitterBufferMs = 50;
};

struct FReceiverStats
{
    int32  CurrentFPS = 0;
    float  PacketLossPercent = 0.0f;
    float  PostFecLossPercent = 0.0f;
    float  OneWayLatencyMs = 0.0f;
    float  JitterMs = 0.0f;
    float  FrameIntervalVarianceMs = 0.0f;
    uint32 PacketsReceived = 0;
    uint32 PacketsLost = 0;
    uint32 PacketsRecovered = 0;
    uint32 PacketsLostPostFec = 0;
    uint32 FramesDecoded = 0;
    bool   bIsReceiving = false;
    uint8  StreamHealthState = 0;
};

// ---------------------------------------------------------------------------
// Decoded frame: holds the mapped GstSample, so the pixels are never copied on
// the CPU. Released (unmapped) when the last reference goes away, which may be
// on the pull, render or RHI thread.
// ---------------------------------------------------------------------------

struct FVideoFrame
{
    void*        MapHandle          = nullptr;
    const uint8* Data               = nullptr;
    int32        Size               = 0;
    int32        Width              = 0;
    int32        Height             = 0;    // image rows, timestamp rows already excluded
    int32        Stride             = 0;
    double       Timestamp          = 0.0;
    uint64       SenderTimestampNs  = 0;    // embedded wall-clock from sender
    uint64       FrameId            = 0;    // monotonic counter from sender
    uint64       Seq                = 0;    // local publish counter, unique per receiver

    FVideoFrame() = default;
    FVideoFrame(const FVideoFrame&) = delete;
    FVideoFrame& operator=(const FVideoFrame&) = delete;
    ~FVideoFrame() { if (MapHandle) GStreamerUnmapSample(MapHandle); }
};

using FVideoFramePtr = TSharedPtr<FVideoFrame, ESPMode::ThreadSafe>;

// Single-slot "latest frame" handoff. The pull thread overwrites, any number of
// readers peek; nothing is dequeued, so there is no producer/consumer ordering
// to get wrong (the old SPSC TQueue was drained from two threads).
class FVideoFrameMailbox
{
public:
    void Publish(FVideoFramePtr Frame)
    {
        FScopeLock Lock(&Mutex);
        Latest = MoveTemp(Frame);
    }
    FVideoFramePtr Peek() const
    {
        FScopeLock Lock(&Mutex);
        return Latest;
    }
    void Reset()
    {
        FScopeLock Lock(&Mutex);
        Latest.Reset();
    }
private:
    mutable FCriticalSection Mutex;
    FVideoFramePtr           Latest;
};

// Shared between the receiver (game thread), the pull thread and the render
// thread. The render thread uploads the newest frame into every target texture
// that the game thread has "leased" within the last couple of frames.
struct FVideoUploadState
{
    static constexpr int32 kMaxTargets = 4;

    FVideoFrameMailbox   Mailbox;
    std::atomic<uint64>  PublishedCount{ 0 };
    std::atomic<bool>    bDead{ false };
    int32                Port = 0;

    // Render thread only: pull -> upload wait, logged every few seconds
    int32  WaitN   = 0;
    double WaitSum = 0.0;
    double WaitMax = 0.0;
    std::atomic<uint64>  LeaseFrame[kMaxTargets];

    // Render thread only
    FTextureRHIRef Target[kMaxTargets];
    uint64         UploadedSeq[kMaxTargets];

    FVideoUploadState()
    {
        for (int32 i = 0; i < kMaxTargets; ++i)
        {
            LeaseFrame[i].store(0);
            UploadedSeq[i] = 0;
        }
    }
};

using FVideoUploadStatePtr = TSharedPtr<FVideoUploadState, ESPMode::ThreadSafe>;

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
    FFramePullRunnable(void* InAppSink, FStreamStats* InStats, FVideoUploadStatePtr InUpload, int32 InPort, void* InBus = nullptr)
        : AppSink(InAppSink), Stats(InStats), Bus(InBus), Upload(MoveTemp(InUpload)), Port(InPort), bShouldStop(false) {}

    virtual ~FFramePullRunnable() {}
    virtual bool   Init() override { return true; }
    virtual uint32 Run()  override;
    virtual void   Stop() override { bShouldStop = true; }
    virtual void   Exit() override {}

    float  GetFrameIntervalVarianceMs() const { return static_cast<float>(FrameIntervalVariance); }
    int64  GetLastSenderTimeNs()  const { return LastSenderTimeNs.Load(); }
    uint64 GetLastFrameId()       const { return LastFrameId.Load(); }
    double GetLastFrameReceivedSec() const { return LastFrameReceivedSec.Load(); }
private:
    static constexpr int32 kTimestampBytes = 8;

    void ExtractTimestampRow(FVideoFrame& Frame);

    void*         AppSink;
    FStreamStats* Stats;
    void*         Bus;
    FVideoUploadStatePtr Upload;
    int32         Port = 0;
    TAtomic<bool> bShouldStop;
    uint64        NextSeq = 1;

    // sender push -> pull latency (same-machine wall clock), logged every kLatencyReportFrames
    static constexpr int32 kLatencyReportFrames = 300;
    int32  LatN   = 0;
    double LatSum = 0.0;
    double LatMin = 1e9;
    double LatMax = 0.0;

    // Welford online variance for inter-frame intervals
    double LastFrameTimeSec     = 0.0;
    bool   bFirstFrame          = true;
    int64  FrameIntervalCount   = 0;
    double FrameIntervalMean    = 0.0;
    double FrameIntervalM2      = 0.0;
    TAtomic<double>  FrameIntervalVariance{ 0.0 };
    TAtomic<int64>   LastSenderTimeNs{ 0 };
    TAtomic<uint64>  LastFrameId{ 0 };
    TAtomic<double>  LastFrameReceivedSec{ 0.0 };
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

    int64  GetLastSenderTimeNs()  const;
    uint64 GetLastFrameId()       const;
    uint64 GetPendingFrameId()    const;

private:
    static void RtpProbeThunk(uint16_t Seq, uint32_t Ts, void* Userdata);
    static void FecProbeThunk(uint16_t Seq, void* Userdata);

    // Builds + resolves the receive pipeline for the chosen decoder (GPU nvh264dec or CPU)
    // and colour conversion (CUDA on the GPU, or multithreaded videoconvert on the CPU).
    bool BuildPipeline(bool bUseGpuDecode, bool bUseGpuConvert);
    void DestroyPipeline();

    int32 AcquireTargetSlot(UTexture2D* Texture);
    void  RegisterUploadState();
    void  UnregisterUploadState();

    void* Pipeline  = nullptr;
    void* AppSink   = nullptr;
    void* Bus       = nullptr;

    FReceiverConfig Config_;
    bool            bUsingGpuDecode_  = false;
    bool            bUsingGpuConvert_ = false;

    bool  bIsInitialized = false;

    int32  VideoWidth  = 0;
    int32  VideoHeight = 0;
    double LastFPSUpdateTime  = 0.0;
    uint64 LastFPSCount       = 0;
    int32  CurrentFPS         = 0;
    uint64 LastReturnedSeq_   = 0;

    struct FTargetSlotGT
    {
        const UTexture2D*  Texture  = nullptr;
        FTextureResource*  Resource = nullptr;
        uint64             LastUse  = 0;
        uint64             LastSeq  = 0;
    };
    FTargetSlotGT        TargetSlots_[FVideoUploadState::kMaxTargets];
    FVideoUploadStatePtr UploadState_;
    bool                 bUploadRegistered_ = false;

    TUniquePtr<FStreamStats>       Stats_;
    TUniquePtr<FProbeContext>      ProbeCtx_;

    TUniquePtr<FFramePullRunnable> FramePullRunnable_;
    TUniquePtr<FRunnableThread>    FramePullThread_;
};

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "GStreamerStats.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Containers/Queue.h"

// Forward declarations for GStreamer C API
extern "C" void* GStreamerGetBus(void* pipeline);
extern "C" bool GStreamerGetJitterBufferStats(void* pipeline,
    unsigned long long* num_pushed, unsigned long long* num_lost,
    double* avg_jitter, unsigned long long* rtx_count);
extern "C" bool GStreamerGetLatency(void* pipeline, double* latency_ms, double* jitter_buffer_latency_ms);
extern "C" bool GStreamerGetSRTStats(void* pipeline, long long* packets_received, long long* packets_lost, double* rtt_ms);
extern "C" void GStreamerUnrefBus(void* bus);

extern "C" void* GStreamerCreatePipeline(const char* description);
extern "C" bool GStreamerStartPipeline(void* pipeline);
extern "C" void GStreamerStopPipeline(void* pipeline);
extern "C" void GStreamerDestroyPipeline(void* pipeline);
extern "C" void* GStreamerGetElementByName(void* pipeline, const char* name);
extern "C" void* GStreamerPullSample(void* appsink);
extern "C" void* GStreamerTryPullSample(void* appsink, double timeout_seconds);  // NEW: Non-blocking pull
extern "C" void* GStreamerGetSampleBuffer(void* sample);
extern "C" void* GStreamerGetSampleCaps(void* sample);
extern "C" bool GStreamerGetVideoDimensions(void* caps, int* width, int* height);
extern "C" bool GStreamerCopyBufferData(void* buffer, unsigned char* dest, int size);
extern "C" int GStreamerGetBufferSize(void* buffer);
extern "C" void GStreamerFreeSample(void* sample);
extern "C" void GStreamerUnrefElement(void* element);

/**
 * Represents a single decoded video frame
 */
struct FVideoFrame
{
    TArray<uint8> Data;
    int32 Width = 0;
    int32 Height = 0;
    double Timestamp = 0.0;
};

/**
 * Background thread runnable that pulls frames from GStreamer appsink
 * This prevents blocking the game thread when using hardware decoding
 */
class FFramePullRunnable : public FRunnable
{
public:
    FFramePullRunnable(void* InAppSink) 
        : AppSink(InAppSink)
        , bShouldStop(false) 
    {}
    
    virtual ~FFramePullRunnable() {}
    
    // FRunnable interface
    virtual bool Init() override { return true; }
    virtual uint32 Run() override;
    virtual void Stop() override { bShouldStop = true; }
    virtual void Exit() override {}
    
    /**
     * Try to get the latest frame (non-blocking)
     * @param OutFrame - Will be filled with frame data if available
     * @return true if a frame was available
     */
    bool PopFrame(FVideoFrame& OutFrame);
    
    /** Check if the thread has any frames waiting */
    bool HasPendingFrame() const { return !FrameQueue.IsEmpty(); }
    
private:
    void* AppSink;
    TAtomic<bool> bShouldStop;
    TQueue<FVideoFrame, EQueueMode::Spsc> FrameQueue;  // Single-producer single-consumer for best performance
};

/**
 * GStreamer video receiver that decodes H.264 RTP streams
 * Supports both CPU (avdec_h264) and GPU (nvh264dec) decoding
 */
class GSTREAMERPLUGIN_API FGStreamerVideoReceiver
{
public:
    FGStreamerVideoReceiver();
    ~FGStreamerVideoReceiver();
    
    bool Initialize(int32 Port = 5004, int SRTLatencyMs=200, bool bUseHardwareDecoder = false);
    bool Start();
    void Stop();
    bool UpdateTexture(UTexture2D* Texture);
    void GetDimensions(int32& OutWidth, int32& OutHeight) const;
    FGStreamerStats GetStatistics();
    bool IsUsingHardwareDecoder() const { return bUsingHardwareDecoder; }
    
private:
    // GStreamer handles
    void* Pipeline;
    void* AppSink;
    void* Bus;
    
    // Video state
    int32 VideoWidth;
    int32 VideoHeight;
    bool bIsInitialized;
    bool bUsingHardwareDecoder;

    // FPS tracking
    int32 FrameCount;
    double LastFPSUpdateTime;
    int32 CurrentFPS;
    
    // Background frame pulling (for hardware decode)
    TUniquePtr<FFramePullRunnable> FramePullRunnable;
    TUniquePtr<FRunnableThread> FramePullThread;
    bool bUseBackgroundThread;
    TAtomic<bool> bUpdateInFlight{ false };
};

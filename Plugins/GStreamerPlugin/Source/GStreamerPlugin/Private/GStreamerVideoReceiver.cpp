#include "GStreamerVideoReceiver.h"
#include "RenderingThread.h"
#include "RenderCommandFence.h"
#include "TextureResource.h"
#include "RHICommandList.h"
#include "RHI.h"
#include <string>

//=============================================================================
// FFramePullRunnable Implementation
//=============================================================================

uint32 FFramePullRunnable::Run()
{
    UE_LOG(LogTemp, Log, TEXT("Frame pull thread started"));
    
    while (!bShouldStop)
    {
        // Use non-blocking pull with short timeout to allow clean shutdown
        void* sample = GStreamerTryPullSample(AppSink, 0.016); // ~16ms timeout (1 frame at 60fps)
        
        if (!sample)
        {
            // No frame available, brief sleep to avoid spinning
            FPlatformProcess::Sleep(0.001f);
            continue;
        }
        
        void* buffer = GStreamerGetSampleBuffer(sample);
        void* caps = GStreamerGetSampleCaps(sample);
        
        if (buffer && caps)
        {
            int width, height;
            if (GStreamerGetVideoDimensions(caps, &width, &height))
            {
                FVideoFrame Frame;
                Frame.Width = width;
                Frame.Height = height;
                Frame.Timestamp = FPlatformTime::Seconds();
                
                int bufferSize = GStreamerGetBufferSize(buffer);
                if (bufferSize <= 0 || bufferSize > (width * height * 4 * 2))
                {
                    GStreamerFreeSample(sample);
                    continue;
                }
                Frame.Data.SetNumUninitialized(bufferSize);
                GStreamerCopyBufferData(buffer, Frame.Data.GetData(), bufferSize);
                
                // Drop old frames - only keep the latest
                // This ensures we always display the most recent frame for lowest latency
                FVideoFrame Temp;
                while (FrameQueue.Dequeue(Temp)) 
                {
                    // Drain any old frames
                }
                
                FrameQueue.Enqueue(MoveTemp(Frame));
            }
        }
        
        GStreamerFreeSample(sample);
    }
    
    UE_LOG(LogTemp, Log, TEXT("Frame pull thread stopped"));
    return 0;
}

bool FFramePullRunnable::PopFrame(FVideoFrame& OutFrame)
{
    return FrameQueue.Dequeue(OutFrame);
}

//=============================================================================
// FGStreamerVideoReceiver Implementation
//=============================================================================

FGStreamerVideoReceiver::FGStreamerVideoReceiver()
    : Pipeline(nullptr)
    , AppSink(nullptr)
    , Bus(nullptr)
    , VideoWidth(0)
    , VideoHeight(0)
    , bIsInitialized(false)
    , bUsingHardwareDecoder(false)
    , FrameCount(0)
    , LastFPSUpdateTime(0.0)
    , CurrentFPS(0)
    , bUseBackgroundThread(false)
{
}

FGStreamerVideoReceiver::~FGStreamerVideoReceiver()
{
    Stop();

    if (Bus)
    {
        GStreamerUnrefBus(Bus);
        Bus = nullptr;
    }
    
    if (AppSink)
    {
        GStreamerUnrefElement(AppSink);
        AppSink = nullptr;
    }
    
    if (Pipeline)
    {
        GStreamerDestroyPipeline(Pipeline);
        Pipeline = nullptr;
    }
}

bool FGStreamerVideoReceiver::Initialize(int32 Port, int SRTLatencyMs, bool bUseHardwareDecoder)
{
    if (bIsInitialized)
    {
        return true;
    }

    std::string pipelineStr;

    if (bUseHardwareDecoder)
    {
        UE_LOG(LogTemp, Log, TEXT("Using hardware-accelerated H.264 decoding (NVDEC)"));
        
        pipelineStr =
            "srtsrc uri=srt://0.0.0.0:" + std::to_string(Port) + "?mode=listener latency=" + std::to_string(SRTLatencyMs) + " ! "
            "tsdemux ! h264parse ! "
            "nvh264dec ! "
            "cudadownload ! "
            "videoconvert n-threads=4 ! "
            "video/x-raw,format=RGBA ! "
            "appsink name=sink emit-signals=false sync=false max-buffers=1 drop=true";
        // UDP + RTP approach
        /*
        std::string pipelineStr =
        "udpsrc port=" + std::to_string(Port) + 
        " caps=\"application/x-rtp\" ! "
        "rtpbin fec-decoders=\"fec,0=\\\"rtpulpfecdec\\\"\" latency=10 ! "
        "rtph264depay ! h264parse ! "
        "nvh264dec ! cudadownload ! "
        "videoconvert n-threads=4 ! video/x-raw,format=RGBA ! "
        "appsink name=sink emit-signals=false sync=false max-buffers=1 drop=true";
        */
        
        bUseBackgroundThread = true;
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("Using CPU H.264 decoding"));

        pipelineStr =
            "udpsrc port=" + std::to_string(Port) +
            " caps=\"application/x-rtp,media=video,encoding-name=H264,payload=96\" ! "
            "rtpjitterbuffer name=jitterbuffer latency=10 ! "
            "rtph264depay ! h264parse ! "
            "avdec_h264 ! "
            "videoconvert ! "
            "video/x-raw,format=BGRA ! "  // BGRA matches Unreal's PF_R8G8B8A8 on PC
            "appsink name=sink emit-signals=false sync=false max-buffers=2 drop=true";
        
        bUseBackgroundThread = true;
    }

    Pipeline = GStreamerCreatePipeline(pipelineStr.c_str());
    if (!Pipeline)
    {
        if (bUseHardwareDecoder)
        {
            UE_LOG(LogTemp, Warning, TEXT("Hardware decoder pipeline failed. Falling back to CPU."));
            return Initialize(Port, false);
        }

        UE_LOG(LogTemp, Error, TEXT("Failed to create GStreamer pipeline"));
        return false;
    }

    // Get the appsink element
    AppSink = GStreamerGetElementByName(Pipeline, "sink");
    if (!AppSink)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get appsink from pipeline"));
        GStreamerDestroyPipeline(Pipeline);
        Pipeline = nullptr;
        return false;
    }

    Bus = GStreamerGetBus(Pipeline);

    bIsInitialized = true;
    bUsingHardwareDecoder = bUseHardwareDecoder;

    UE_LOG(LogTemp, Log, TEXT("GStreamer initialized on port %d (%s, %s thread)"),
        Port, 
        bUseHardwareDecoder ? TEXT("NVDEC") : TEXT("CPU"),
        bUseBackgroundThread ? TEXT("background") : TEXT("game"));

    return true;
}

bool FGStreamerVideoReceiver::Start()
{
    if (!bIsInitialized || !Pipeline)
    {
        return false;
    }
    
    bool result = GStreamerStartPipeline(Pipeline);
    if (!result)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to start GStreamer pipeline"));
        return false;
    }
    
    UE_LOG(LogTemp, Log, TEXT("GStreamer pipeline started"));
    
    // Start background frame pulling thread if enabled
    if (bUseBackgroundThread && AppSink)
    {
        FramePullRunnable = MakeUnique<FFramePullRunnable>(AppSink);
        FramePullThread = TUniquePtr<FRunnableThread>(
            FRunnableThread::Create(
                FramePullRunnable.Get(),
                TEXT("GStreamerFramePull"),
                0,  // Stack size (0 = default)
                TPri_AboveNormal  // Higher priority for low latency
            )
        );
        
        if (!FramePullThread)
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to create frame pull thread, falling back to game thread pulling"));
            FramePullRunnable.Reset();
            bUseBackgroundThread = false;
        }
    }
    
    return true;
}

void FGStreamerVideoReceiver::Stop()
{
    // Stop background thread first
    if (FramePullRunnable)
    {
        FramePullRunnable->Stop();
    }
    
    if (FramePullThread)
    {
        FramePullThread->WaitForCompletion();
        FramePullThread.Reset();
    }
    
    FramePullRunnable.Reset();
    
    // Now stop pipeline
    if (Pipeline)
    {
        GStreamerStopPipeline(Pipeline);
        UE_LOG(LogTemp, Log, TEXT("GStreamer pipeline stopped"));
    }
}

bool FGStreamerVideoReceiver::UpdateTexture(UTexture2D* Texture)
{
    if (!Texture || !AppSink) { return false; }

    FVideoFrame Frame;
    bool bHasNewFrame = false;
    
    if (bUseBackgroundThread && FramePullRunnable) {
        bHasNewFrame = FramePullRunnable->PopFrame(Frame);
    }
    else {
        // Direct pull on game thread (blocking) - use timeout version
        void* sample = GStreamerTryPullSample(AppSink, 0.001); // 1ms timeout max
        if (sample)
        {
            void* buffer = GStreamerGetSampleBuffer(sample);
            void* caps = GStreamerGetSampleCaps(sample);
            
            if (buffer && caps)
            {
                int width, height;
                if (GStreamerGetVideoDimensions(caps, &width, &height))
                {
                    Frame.Width = width;
                    Frame.Height = height;
                    
                    int bufferSize = GStreamerGetBufferSize(buffer);
                    Frame.Data.SetNumUninitialized(bufferSize);
                    GStreamerCopyBufferData(buffer, Frame.Data.GetData(), bufferSize);
                    
                    bHasNewFrame = true;
                }
            }
            
            GStreamerFreeSample(sample);
        }
    }
    
    if (!bHasNewFrame)
    {
        return false; // No new frame available
    }
    
    // Update FPS counter
    FrameCount++;
    double CurrentTime = FPlatformTime::Seconds();
    if (CurrentTime - LastFPSUpdateTime >= 1.0)
    {
        CurrentFPS = FrameCount;
        FrameCount = 0;
        LastFPSUpdateTime = CurrentTime;
    }

    // Update stored dimensions
    VideoWidth = Frame.Width;
    VideoHeight = Frame.Height;

    if (!Texture->GetResource() || !Texture->GetResource()->TextureRHI) {
        return false;
    }

    if (bUpdateInFlight)
    {
        return false;  // Previous update still processing, skip this frame
    }

    bUpdateInFlight = true;

    // Guard: frame must match texture dimensions exactly
    if (Frame.Width != Texture->GetSizeX() || Frame.Height != Texture->GetSizeY()) {
        UE_LOG(LogTemp, Warning, TEXT("Frame/texture size mismatch: frame=%dx%d tex=%dx%d"), Frame.Width, Frame.Height, Texture->GetSizeX(), Texture->GetSizeY());
        bUpdateInFlight = false;
        return false;
    }

    // Guard: data size must be exactly width*height*4
    if (Frame.Data.Num() != Frame.Width * Frame.Height * 4) {
        UE_LOG(LogTemp, Warning, TEXT("Frame data size unexpected: %d vs expected %d"), Frame.Data.Num(), Frame.Width * Frame.Height * 4);
        bUpdateInFlight = false;
        return false;
    }

    FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Frame.Width, Frame.Height);
    TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> ImageDataPtr = MakeShared<TArray<uint8>, ESPMode::ThreadSafe>(MoveTemp(Frame.Data));

    TAtomic<bool>* FlagPtr = &bUpdateInFlight;
    Texture->UpdateTextureRegions(
        0,
        1,
        Region,
        Frame.Width * 4,
        4,
        ImageDataPtr->GetData(),
        [Region, ImageDataPtr, FlagPtr](auto* Data, const FUpdateTextureRegion2D* Regions)
        {
            delete Region;
            *FlagPtr = false;
        }
    );

    return true;
}

void FGStreamerVideoReceiver::GetDimensions(int32& OutWidth, int32& OutHeight) const
{
    OutWidth = VideoWidth;
    OutHeight = VideoHeight;
}

FGStreamerStats FGStreamerVideoReceiver::GetStatistics()
{
    FGStreamerStats Stats;

    if (!Pipeline)
    {
        return Stats;
    }

    // Try SRT stats first (for SRT pipeline)
    long long packetsReceived = 0, packetsLost = 0;
    double rttMs = 0.0;

    if (GStreamerGetSRTStats(Pipeline, &packetsReceived, &packetsLost, &rttMs))
    {
        Stats.SRTPacketsReceived = packetsReceived;
        Stats.SRTPacketsLost = packetsLost;
        Stats.SRTRoundTripMs = rttMs;

        if (packetsReceived > 0)
        {
            Stats.PacketLossPercent = (packetsLost * 100.0f) / (packetsReceived + packetsLost);
        }
    }
    else
    {
        // Fall back to RTP jitter buffer stats (for UDP/RTP pipeline)
        unsigned long long numPushed = 0, numLost = 0, rtxCount = 0;
        double avgJitter = 0.0;

        if (GStreamerGetJitterBufferStats(Pipeline, &numPushed, &numLost, &avgJitter, &rtxCount))
        {
            Stats.FramesPushed = numPushed;
            Stats.FramesLost = numLost;
            Stats.AverageJitterMs = avgJitter;
            Stats.RetransmissionCount = rtxCount;

            if (numPushed > 0)
            {
                Stats.PacketLossPercent = (numLost * 100.0f) / (numPushed + numLost);
            }
        }
    }

    // Get latency
    double pipelineLatency = 0.0;
    double jitterBufferLatency = 0.0;
    if (GStreamerGetLatency(Pipeline, &pipelineLatency, &jitterBufferLatency))
    {
        Stats.PipelineLatencyMs = pipelineLatency;
        Stats.JitterBufferLatencyMs = jitterBufferLatency;
    }

    Stats.CurrentFPS = CurrentFPS;

    return Stats;
}
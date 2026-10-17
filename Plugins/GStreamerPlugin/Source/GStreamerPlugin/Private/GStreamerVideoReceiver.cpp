#include "GStreamerVideoReceiver.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "RHICommandList.h"
#include "RHI.h"
#include "CoreGlobals.h"
#include "Misc/CoreDelegates.h"
#include "SceneViewExtension.h"
#include "RenderGraphBuilder.h"
#include <atomic>
#include <chrono>
#include <string>

// ---------------------------------------------------------------------------
// GPU decode verdict — process-wide, decided once
// ---------------------------------------------------------------------------
// Whether nvh264dec actually works is a property of the machine, not of any
// individual stream, so it is settled once and reused. Previously every stream
// re-ran its own 4 s probe; with four streams that was 16 s of startup spent
// re-answering the same question.
namespace {
    enum class EGpuVerdict : int32 { Unknown = 0, Good = 1, Bad = -1 };
    std::atomic<int32> GGpuVerdict{ static_cast<int32>(EGpuVerdict::Unknown) };

    EGpuVerdict GetGpuVerdict()          { return static_cast<EGpuVerdict>(GGpuVerdict.load()); }
    void        SetGpuVerdict(EGpuVerdict V) { GGpuVerdict.store(static_cast<int32>(V)); }

    // Watch the bus for a short grace period and report whether the pipeline
    // raised a hard ERROR. Warnings are logged but are not grounds for demotion.
    bool DrainBusForHardError(void* Bus, double GraceSec, FString& OutFirstError)
    {
        const double Deadline = FPlatformTime::Seconds() + GraceSec;
        bool bHard = false;
        char Buf[512];
        bool bWarn = false;

        do {
            while (GStreamerPopBusError(Bus, Buf, sizeof(Buf), &bWarn))
            {
                const FString Msg = ANSI_TO_TCHAR(Buf);
                if (bWarn)
                {
                    UE_LOG(LogTemp, Warning, TEXT("GStreamer: GPU decode probe warning: %s"), *Msg);
                }
                else
                {
                    if (!bHard) OutFirstError = Msg;
                    bHard = true;
                }
            }
            if (bHard) break;
            FPlatformProcess::Sleep(0.01f);
        } while (FPlatformTime::Seconds() < Deadline);

        return bHard;
    }

    // Long enough for a decoder that cannot initialise at all to say so on the
    // bus; short enough to be invisible at startup. Paid once per process.
    constexpr double kGpuProbeGraceSec = 0.30;

    // cudaconvert (NV12 -> BGRA on the GPU) is used when present. Like the decoder
    // verdict it is checked once per process; if it errors, later streams convert
    // on the CPU instead.
    std::atomic<int32> GCudaConvertVerdict{ static_cast<int32>(EGpuVerdict::Unknown) };

    std::string VideoConvertStep()
    {
        static const std::string Step = []()
        {
            if (!GStreamerElementHasProperty("videoconvert", "n-threads"))
                return std::string("videoconvert");
            const int32 Cores = FPlatformMisc::NumberOfCores();
            const int32 N = FMath::Clamp(Cores, 1, 4);
            return std::string("videoconvert n-threads=") + std::to_string(N);
        }();
        return Step;
    }
}

// ---------------------------------------------------------------------------
// Render-thread upload
// ---------------------------------------------------------------------------
// Frames are copied into their textures on the render thread right before the
// view family is rendered, instead of from the game-thread tick. The game thread
// runs a frame ahead of the render thread, so picking up the frame here should
// show it roughly one display frame earlier. High priority so it runs before
// other extensions (the HMD's stereo layer copy included, if that copy happens
// in its own PreRenderViewFamily).

class FVideoUploadExtension : public FSceneViewExtensionBase
{
public:
    FVideoUploadExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
    virtual int32 GetPriority() const override { return 1000; }

    virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override
    {
        UploadAll_RenderThread(GraphBuilder.RHICmdList);
    }

    void Add_RenderThread(const FVideoUploadStatePtr& State) { States.AddUnique(State); }
    void Clear_RenderThread() { States.Empty(); }

private:
    void UploadAll_RenderThread(FRHICommandListImmediate& RHICmdList)
    {
        States.RemoveAll([](const FVideoUploadStatePtr& State) { return State->bDead.load(); });

        const uint64 RenderFrame = GFrameCounterRenderThread;
        for (const FVideoUploadStatePtr& State : States)
        {
            FVideoFramePtr Frame = State->Mailbox.Peek();
            if (!Frame || !Frame->Data) continue;

            bool bUploaded = false;
            for (int32 i = 0; i < FVideoUploadState::kMaxTargets; ++i)
            {
                FRHITexture* Tex = State->Target[i].GetReference();
                if (!Tex) continue;

                const uint64 Lease = State->LeaseFrame[i].load();
                if (Lease == 0 || RenderFrame > Lease + 2) continue;
                if (State->UploadedSeq[i] == Frame->Seq) continue;

                const FIntPoint Extent = Tex->GetDesc().Extent;
                if (Extent.X != Frame->Width || Extent.Y != Frame->Height) continue;

                const bool bFirstUploadOfFrame = !bUploaded;
                const FUpdateTextureRegion2D Region(0, 0, 0, 0, Frame->Width, Frame->Height);
                RHICmdList.UpdateTexture2D(Tex, 0, Region, Frame->Stride, Frame->Data);
                State->UploadedSeq[i] = Frame->Seq;
                bUploaded = true;

                if (bFirstUploadOfFrame)
                {
                    const double WaitMs = (FPlatformTime::Seconds() - Frame->Timestamp) * 1000.0;
                    State->WaitSum += WaitMs;
                    State->WaitMax  = FMath::Max(State->WaitMax, WaitMs);
                    if (++State->WaitN >= 300)
                    {
                        UE_LOG(LogTemp, Log, TEXT("GStreamer: pull -> render upload mean %.2f ms  max %.2f (port %d)"),
                               State->WaitSum / State->WaitN, State->WaitMax, State->Port);
                        State->WaitN = 0; State->WaitSum = 0.0; State->WaitMax = 0.0;
                    }
                }
            }

            if (bUploaded)
                RHICmdList.EnqueueLambda([KeepAlive = Frame](auto&) {});
        }
    }

    TArray<FVideoUploadStatePtr> States;
};

namespace {
    TSharedPtr<FVideoUploadExtension, ESPMode::ThreadSafe> GVideoUploadExtension;

    FVideoUploadExtension* GetUploadExtension_GameThread()
    {
        check(IsInGameThread());
        if (!GVideoUploadExtension.IsValid())
        {
            GVideoUploadExtension = FSceneViewExtensions::NewExtension<FVideoUploadExtension>();

            // Drop the texture references while the RHI is still alive.
            FCoreDelegates::OnEnginePreExit.AddLambda([]()
            {
                if (FVideoUploadExtension* Ext = GVideoUploadExtension.Get())
                {
                    ENQUEUE_RENDER_COMMAND(GStreamerClearUploads)(
                        [Ext](FRHICommandListImmediate&) { Ext->Clear_RenderThread(); });
                    FlushRenderingCommands();
                }
            });
        }
        return GVideoUploadExtension.Get();
    }
}

// ---------------------------------------------------------------------------
// Probe thunks
// ---------------------------------------------------------------------------

void FGStreamerVideoReceiver::RtpProbeThunk(uint16_t Seq, uint32_t Ts, void* Userdata)
{
    FProbeContext* Ctx = static_cast<FProbeContext*>(Userdata);
    if (Ctx && Ctx->Stats)
        Ctx->Stats->OnRtpPacket(static_cast<uint16>(Seq), static_cast<uint32>(Ts));
}

void FGStreamerVideoReceiver::FecProbeThunk(uint16_t Seq, void* Userdata)
{
    FProbeContext* Ctx = static_cast<FProbeContext*>(Userdata);
    if (Ctx && Ctx->Stats)
        Ctx->Stats->OnFecPacket(static_cast<uint16>(Seq));
}

// ---------------------------------------------------------------------------
// FFramePullRunnable — timestamp extraction
// ---------------------------------------------------------------------------

void FFramePullRunnable::ExtractTimestampRow(FVideoFrame& Frame)
{
    // The sender encodes a 64-bit wall-clock timestamp into the first 64 pixels
    // of the second-to-last row and the frame id into the last row: each pixel
    // is black or white, which after decode arrives as BGRA. Threshold the blue
    // channel (index 0) at 128. The two rows are then excluded by reducing
    // Height; the pixels stay where they are, the texture just never sees them.

    if (Frame.Height < 3) return;

    const int32 TsRowOffset = (Frame.Height - 2) * Frame.Stride;
    if (Frame.Size < TsRowOffset + 64 * 4) return;

    const uint8* TsRow = Frame.Data + TsRowOffset;
    uint64 SenderNs = 0;
    for (int32 Bit = 0; Bit < 64; ++Bit)
    {
        if (TsRow[Bit * 4 + 0] > 128)
            SenderNs |= (static_cast<uint64>(1) << Bit);
    }
    Frame.SenderTimestampNs = SenderNs;

    const int32 IdRowOffset = (Frame.Height - 1) * Frame.Stride;
    if (Frame.Size >= IdRowOffset + 64 * 4)
    {
        const uint8* IdRow = Frame.Data + IdRowOffset;
        uint64 FrameId = 0;
        for (int32 Bit = 0; Bit < 64; ++Bit)
        {
            if (IdRow[Bit * 4 + 0] > 128)
                FrameId |= (static_cast<uint64>(1) << Bit);
        }
        Frame.FrameId = FrameId;
        LastFrameId.Store(FrameId);
    }

    Frame.Height -= 2;

    if (Stats && SenderNs > 0)
        Stats->OnFrameLatency(SenderNs);
}

int64 FGStreamerVideoReceiver::GetLastSenderTimeNs() const
{
    return FramePullRunnable_ ? FramePullRunnable_->GetLastSenderTimeNs() : 0;
}

uint64 FGStreamerVideoReceiver::GetLastFrameId() const
{
    return FramePullRunnable_ ? FramePullRunnable_->GetLastFrameId() : 0;
}

uint64 FGStreamerVideoReceiver::GetPendingFrameId() const
{
    if (!UploadState_) return 0;
    FVideoFramePtr Frame = UploadState_->Mailbox.Peek();
    return (Frame && Frame->Seq != LastReturnedSeq_) ? Frame->FrameId : 0;
}

// ---------------------------------------------------------------------------
// FFramePullRunnable::Run
// ---------------------------------------------------------------------------

uint32 FFramePullRunnable::Run()
{
    UE_LOG(LogTemp, Log, TEXT("GStreamer: frame pull thread started"));

    int32 IdleCount   = 0;
    int32 FrameCount  = 0;

    while (!bShouldStop)
    {
        void* Sample = GStreamerTryPullSample(AppSink, 0.016);
        if (!Sample)
        {
            ++IdleCount;

            char ErrBuf[512]; bool bWarn = false;
            while (GStreamerPopBusError(Bus, ErrBuf, sizeof(ErrBuf), &bWarn))
            {
                if (bWarn)
                {
                    UE_LOG(LogTemp, Warning, TEXT("GStreamer recv pipeline: %s"), ANSI_TO_TCHAR(ErrBuf));
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("GStreamer recv pipeline: %s"), ANSI_TO_TCHAR(ErrBuf));
                }
            }

            FPlatformProcess::Sleep(0.001f);
            continue;
        }
        ++FrameCount;

        void* Buffer = GStreamerGetSampleBuffer(Sample);
        void* Caps   = GStreamerGetSampleCaps(Sample);

        if (Buffer && Caps)
        {
            int W = 0, H = 0;
            if (GStreamerGetVideoDimensions(Caps, &W, &H) && W > 0 && H > 0)
            {
                int BufSize  = GStreamerGetBufferSize(Buffer);
                int Expected = W * H * 4;

                if (BufSize == Expected)
                {
                    FVideoFramePtr Frame = MakeShared<FVideoFrame, ESPMode::ThreadSafe>();
                    Frame->MapHandle = GStreamerMapSample(Sample, &Frame->Data, &Frame->Size);
                    if (Frame->MapHandle && Frame->Size >= Expected)
                    {
                        Frame->Width     = W;
                        Frame->Height    = H;
                        Frame->Stride    = W * 4;
                        Frame->Timestamp = FPlatformTime::Seconds();
                        Frame->Seq       = NextSeq++;

                        ExtractTimestampRow(*Frame);
                        LastSenderTimeNs.Store(Frame->SenderTimestampNs);

                        if (Frame->SenderTimestampNs > 0)
                        {
                            const int64 NowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
                            const double Ms = (NowNs - static_cast<int64>(Frame->SenderTimestampNs)) / 1e6;
                            if (Ms > -1000.0 && Ms < 1000.0)
                            {
                                LatSum += Ms;
                                LatMin  = FMath::Min(LatMin, Ms);
                                LatMax  = FMath::Max(LatMax, Ms);
                                if (++LatN >= kLatencyReportFrames)
                                {
                                    UE_LOG(LogTemp, Log,
                                           TEXT("GStreamer: sender push -> pull mean %.2f ms  min %.2f  max %.2f (port %d, same-machine clock only)"),
                                           LatSum / LatN, LatMin, LatMax, Port);
                                    LatN = 0; LatSum = 0.0; LatMin = 1e9; LatMax = 0.0;
                                }
                            }
                        }

                        if (Upload)
                        {
                            Upload->Mailbox.Publish(Frame);
                            Upload->PublishedCount.fetch_add(1);
                        }
                        LastFrameReceivedSec.Store(FPlatformTime::Seconds());

                        if (Stats) Stats->OnFrameDecoded();

                        double NowSec = FPlatformTime::Seconds();
                        if (!bFirstFrame)
                        {
                            double IntervalMs = (NowSec - LastFrameTimeSec) * 1000.0;
                            FrameIntervalCount++;
                            double Delta  = IntervalMs - FrameIntervalMean;
                            FrameIntervalMean += Delta / static_cast<double>(FrameIntervalCount);
                            double Delta2 = IntervalMs - FrameIntervalMean;
                            FrameIntervalM2  += Delta * Delta2;
                            if (FrameIntervalCount > 1)
                                FrameIntervalVariance.Store(FrameIntervalM2 / static_cast<double>(FrameIntervalCount - 1));
                        }
                        LastFrameTimeSec = NowSec;
                        bFirstFrame      = false;
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Warning,
                           TEXT("GStreamer: buf size %d != expected %d (%dx%d*4), drop"),
                           BufSize, Expected, W, H);
                }
            }
        }

        GStreamerFreeSample(Sample);
    }

    UE_LOG(LogTemp, Log, TEXT("GStreamer: frame pull thread stopped"));
    return 0;
}

FGStreamerVideoReceiver::FGStreamerVideoReceiver()
{
}

FGStreamerVideoReceiver::~FGStreamerVideoReceiver()
{
    Stop();
    UnregisterUploadState();
    DestroyPipeline();
}

void FGStreamerVideoReceiver::DestroyPipeline()
{
    if (Bus)      { GStreamerUnrefBus(Bus);             Bus      = nullptr; }
    if (AppSink)  { GStreamerUnrefElement(AppSink);     AppSink  = nullptr; }
    if (Pipeline) { GStreamerDestroyPipeline(Pipeline); Pipeline = nullptr; }
}

void FGStreamerVideoReceiver::RegisterUploadState()
{
    if (bUploadRegistered_ || !UploadState_) return;
    FVideoUploadExtension* Ext = GetUploadExtension_GameThread();
    FVideoUploadStatePtr State = UploadState_;
    ENQUEUE_RENDER_COMMAND(GStreamerRegisterUpload)(
        [Ext, State](FRHICommandListImmediate&) { Ext->Add_RenderThread(State); });
    bUploadRegistered_ = true;
}

// May run on a worker thread (VideoFeedComponent restarts sources off the game
// thread), so no render command here: the render thread drops dead states itself.
void FGStreamerVideoReceiver::UnregisterUploadState()
{
    if (!UploadState_) return;
    UploadState_->bDead.store(true);
    bUploadRegistered_ = false;
}

bool FGStreamerVideoReceiver::Initialize(const FReceiverConfig& Config)
{
    if (bIsInitialized) return true;

    Config_ = Config;

    // Prefer GPU decode (nvh264dec) when present; if its pipeline can't be built (missing
    // CUDA elements) fall back to CPU here, and if the decoder reports a hard error that
    // is caught and recovered in Start().
    //
    // Once any stream in this process has proven the GPU decoder broken, later streams
    // skip the attempt entirely rather than each rediscovering it.
    UploadState_ = MakeShared<FVideoUploadState, ESPMode::ThreadSafe>();

    bUsingGpuDecode_  = GStreamerNvdecAvailable() && GetGpuVerdict() != EGpuVerdict::Bad;
    bUsingGpuConvert_ = bUsingGpuDecode_
        && GCudaConvertVerdict.load() != static_cast<int32>(EGpuVerdict::Bad)
        && GStreamerElementAvailable("cudaconvert");
    bool bBuilt = bUsingGpuDecode_ && BuildPipeline(true, bUsingGpuConvert_);
    if (!bBuilt && bUsingGpuConvert_)
    {
        bUsingGpuConvert_ = false;
        bBuilt = BuildPipeline(true, false);
    }
    if (!bBuilt)
    {
        bUsingGpuDecode_  = false;
        bUsingGpuConvert_ = false;
        bBuilt = BuildPipeline(false, false);
    }
    if (!bBuilt)
    {
        UE_LOG(LogTemp, Error,
               TEXT("GStreamer: failed to create pipeline on port %d"), Config.Port);
        return false;
    }

    FStreamStatsConfig StatsConfig;
    StatsConfig.SenderIP         = Config.SenderIP;
    StatsConfig.FeedbackPort     = Config.FeedbackPort;
    StatsConfig.ReportIntervalMs = Config.ReportIntervalMs;
    StatsConfig.StatusPort = Config.StatusPort;
    Stats_ = MakeUnique<FStreamStats>(StatsConfig);

    ProbeCtx_ = MakeUnique<FProbeContext>();
    ProbeCtx_->Stats = Stats_.Get();

    bIsInitialized = true;
    UE_LOG(LogTemp, Log,
           TEXT("GStreamer: initialized, UDP port %d"), Config.Port);
    return true;
}

// Builds + resolves the receive pipeline. nvh264dec outputs CUDA memory: with
// cudaconvert the NV12 -> BGRA conversion stays on the GPU and only BGRA is
// downloaded; otherwise cudadownload brings NV12 back and a multithreaded
// videoconvert does it on the CPU. The CPU decode path uses avdec_h264.
bool FGStreamerVideoReceiver::BuildPipeline(bool bUseGpuDecode, bool bUseGpuConvert)
{
    std::string DecodeStep;
    if (bUseGpuDecode && bUseGpuConvert)
        DecodeStep = "nvh264dec ! cudaconvert ! video/x-raw(memory:CUDAMemory),format=BGRA ! cudadownload";
    else if (bUseGpuDecode)
        DecodeStep = "nvh264dec ! cudadownload ! " + VideoConvertStep();
    else
        DecodeStep = "avdec_h264 ! " + VideoConvertStep();

    const FString ConvertName = bUseGpuConvert ? FString(TEXT("cudaconvert (GPU)"))
                                               : FString(VideoConvertStep().c_str());
    UE_LOG(LogTemp, Log, TEXT("GStreamer: decoder: %s, convert: %s, jitterbuffer %d ms (port %d)"),
           bUseGpuDecode ? TEXT("nvh264dec (GPU)") : TEXT("avdec_h264 (CPU)"),
           *ConvertName, Config_.JitterBufferMs, Config_.Port);

    std::string PipelineStr =
        "udpsrc port=" + std::to_string(Config_.Port) +
        " caps=\"application/x-rtp,media=video,encoding-name=H264,payload=96\""
        " ! rtpjitterbuffer name=jitterbuffer latency=" + std::to_string(Config_.JitterBufferMs) +
        " drop-on-latency=true"
        " ! rtpulpfecdec name=fecdec"
        " ! rtph264depay name=depay"
        " ! h264parse"
        " ! " + DecodeStep +
        " ! video/x-raw,format=BGRA"
        " ! appsink name=sink emit-signals=false sync=false max-buffers=2 drop=true";

    Pipeline = GStreamerCreatePipeline(PipelineStr.c_str());
    if (!Pipeline) return false;

    AppSink = GStreamerGetElementByName(Pipeline, "sink");
    if (!AppSink)
    {
        UE_LOG(LogTemp, Error, TEXT("GStreamer: failed to find appsink"));
        GStreamerDestroyPipeline(Pipeline);
        Pipeline = nullptr;
        return false;
    }

    Bus = GStreamerGetBus(Pipeline);
    return true;
}

bool FGStreamerVideoReceiver::Start()
{
    if (!bIsInitialized || !Pipeline) return false;

    if (!GStreamerStartPipeline(Pipeline))
    {
        UE_LOG(LogTemp, Error, TEXT("GStreamer: failed to start pipeline"));
        return false;
    }

    // Decide GPU vs CPU on evidence, not on a timeout.
    //
    // This previously waited up to 4 s for the pipeline to reach PLAYING and demoted to
    // CPU if it didn't. That conflates three different questions -- can this machine
    // decode on the GPU, has the pipeline finished negotiating, and is the sender
    // actually sending -- and only measures the second. A live RTP pipeline cannot
    // finish negotiating until the sender's first keyframe arrives, so on a stream whose
    // sender is offline (e.g. a wrist cam that isn't running) "not PLAYING yet" simply
    // means "no data yet". Those streams were misdiagnosed as GPU failures, permanently
    // downgraded to software decode, and cost 4 s of blocking startup each.
    //
    // Now the only thing that demotes is an explicit ERROR on the bus, and the question
    // is asked once per process. If the GPU decoder is genuinely broken it will say so
    // here; if it breaks later, the frame-pull thread already drains and logs bus errors.
    if (bUsingGpuDecode_ && bUsingGpuConvert_
        && GCudaConvertVerdict.load() == static_cast<int32>(EGpuVerdict::Unknown))
    {
        FString FirstError;
        if (DrainBusForHardError(Bus, kGpuProbeGraceSec, FirstError))
        {
            GCudaConvertVerdict.store(static_cast<int32>(EGpuVerdict::Bad));
            UE_LOG(LogTemp, Warning,
                   TEXT("GStreamer: cudaconvert reported an error — converting on the CPU for this and ")
                   TEXT("all later streams. Reason: %s"), *FirstError);

            GStreamerStopPipeline(Pipeline);
            DestroyPipeline();
            bUsingGpuConvert_ = false;
            if (!BuildPipeline(true, false) || !GStreamerStartPipeline(Pipeline))
            {
                UE_LOG(LogTemp, Error, TEXT("GStreamer: GPU-decode / CPU-convert pipeline failed to start"));
                return false;
            }
        }
        else
        {
            GCudaConvertVerdict.store(static_cast<int32>(EGpuVerdict::Good));
            UE_LOG(LogTemp, Log, TEXT("GStreamer: cudaconvert accepted — NV12 to BGRA on the GPU"));
        }
    }

    if (bUsingGpuDecode_ && GetGpuVerdict() == EGpuVerdict::Unknown)
    {
        FString FirstError;
        if (DrainBusForHardError(Bus, kGpuProbeGraceSec, FirstError))
        {
            SetGpuVerdict(EGpuVerdict::Bad);
            UE_LOG(LogTemp, Warning,
                   TEXT("GStreamer: nvh264dec reported an error — falling back to avdec_h264 (CPU) ")
                   TEXT("for this and all later streams. Reason: %s"), *FirstError);

            GStreamerStopPipeline(Pipeline);
            DestroyPipeline();

            bUsingGpuDecode_  = false;
            bUsingGpuConvert_ = false;
            if (!BuildPipeline(false, false) || !GStreamerStartPipeline(Pipeline))
            {
                UE_LOG(LogTemp, Error, TEXT("GStreamer: CPU fallback pipeline failed to start"));
                return false;
            }
        }
        else
        {
            SetGpuVerdict(EGpuVerdict::Good);
            UE_LOG(LogTemp, Log,
                   TEXT("GStreamer: nvh264dec accepted — GPU decode confirmed for this process"));
        }
    }

    bool bRtpOk = GStreamerAddRtpProbe(
        Pipeline, "fecdec",
        &FGStreamerVideoReceiver::RtpProbeThunk,
        ProbeCtx_.Get());

    bool bFecOk = GStreamerAddFecProbe(
        Pipeline, "fecdec",
        &FGStreamerVideoReceiver::FecProbeThunk,
        ProbeCtx_.Get());

    if (!bRtpOk)
        UE_LOG(LogTemp, Warning,
               TEXT("GStreamer: RTP pre-FEC probe failed — loss/jitter will be zero"));
    if (!bFecOk)
        UE_LOG(LogTemp, Warning,
               TEXT("GStreamer: FEC post-FEC probe failed — recovery count will be zero"));

    if (Stats_) Stats_->Start();

    if (UploadState_)
    {
        UploadState_->Mailbox.Reset();
        UploadState_->Port = Config_.Port;
    }
    FramePullRunnable_ = MakeUnique<FFramePullRunnable>(AppSink, Stats_.Get(), UploadState_, Config_.Port, Bus);
    FramePullThread_   = TUniquePtr<FRunnableThread>(
        FRunnableThread::Create(
            FramePullRunnable_.Get(),
            TEXT("GStreamerFramePull"),
            0,
            TPri_AboveNormal));

    if (!FramePullThread_)
    {
        UE_LOG(LogTemp, Error, TEXT("GStreamer: failed to create frame pull thread"));
        FramePullRunnable_.Reset();
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("GStreamer: pipeline started, probes attached"));
    return true;
}

void FGStreamerVideoReceiver::Stop()
{
    if (FramePullRunnable_) FramePullRunnable_->Stop();
    if (FramePullThread_)
    {
        FramePullThread_->WaitForCompletion();
        FramePullThread_.Reset();
    }
    FramePullRunnable_.Reset();

    if (Stats_) Stats_->Stop();

    if (Pipeline) GStreamerStopPipeline(Pipeline);
}

// Game thread. Makes sure OutTexture exists at the stream's size and leases it
// for this frame; the pixels are written later on the render thread by
// FVideoUploadExtension. Returns true when a frame newer than the last call for
// this texture is available.
bool FGStreamerVideoReceiver::UpdateTexture(UTexture2D*& OutTexture)
{
    if (!AppSink || !FramePullRunnable_ || !UploadState_) return false;

    RegisterUploadState();

    const double Now = FPlatformTime::Seconds();
    if (Now - LastFPSUpdateTime >= 0.5)
    {
        const uint64 Count = UploadState_->PublishedCount.load();
        if (LastFPSUpdateTime > 0.0)
            CurrentFPS = FMath::RoundToInt(static_cast<double>(Count - LastFPSCount) / (Now - LastFPSUpdateTime));
        LastFPSCount      = Count;
        LastFPSUpdateTime = Now;
    }

    FVideoFramePtr Frame = UploadState_->Mailbox.Peek();
    if (!Frame) return false;

    VideoWidth  = Frame->Width;
    VideoHeight = Frame->Height;

    if (!OutTexture
        || OutTexture->GetSizeX() != Frame->Width
        || OutTexture->GetSizeY() != Frame->Height)
    {
        UE_LOG(LogTemp, Log,
               TEXT("GStreamer: texture resize %dx%d -> %dx%d"),
               OutTexture ? OutTexture->GetSizeX() : 0,
               OutTexture ? OutTexture->GetSizeY() : 0,
               Frame->Width, Frame->Height);

        FlushRenderingCommands();

        UTexture2D* NewTex = UTexture2D::CreateTransient(
            Frame->Width, Frame->Height, PF_B8G8R8A8);
        if (!NewTex) return false;
        NewTex->SRGB = false;
        NewTex->UpdateResource();
        OutTexture = NewTex;
    }

    const int32 Slot = AcquireTargetSlot(OutTexture);
    if (Slot < 0) return false;

    UploadState_->LeaseFrame[Slot].store(GFrameCounter);

    FTargetSlotGT& S = TargetSlots_[Slot];
    const bool bNew = (S.LastSeq != Frame->Seq);
    S.LastSeq        = Frame->Seq;
    LastReturnedSeq_ = Frame->Seq;
    return bNew;
}

// Finds (or assigns) the render-thread upload slot for this texture. A new or
// re-created texture gets its RHI reference handed to the render thread; slots
// whose texture stopped being leased are reused, oldest first.
int32 FGStreamerVideoReceiver::AcquireTargetSlot(UTexture2D* Texture)
{
    FTextureResource* Resource = Texture ? Texture->GetResource() : nullptr;
    if (!Resource) return -1;

    const uint64 Frame = GFrameCounter;
    int32 Oldest = 0;
    for (int32 i = 0; i < FVideoUploadState::kMaxTargets; ++i)
    {
        FTargetSlotGT& S = TargetSlots_[i];
        if (S.Texture == Texture && S.Resource == Resource)
        {
            S.LastUse = Frame;
            return i;
        }
        if (S.LastUse < TargetSlots_[Oldest].LastUse) Oldest = i;
    }

    FTargetSlotGT& S = TargetSlots_[Oldest];
    S.Texture  = Texture;
    S.Resource = Resource;
    S.LastUse  = Frame;
    S.LastSeq  = 0;

    FVideoUploadStatePtr State = UploadState_;
    const int32 Slot = Oldest;
    ENQUEUE_RENDER_COMMAND(GStreamerBindUploadTarget)(
        [State, Slot, Resource](FRHICommandListImmediate&)
        {
            State->Target[Slot]      = Resource->TextureRHI;
            State->UploadedSeq[Slot] = 0;
        });
    return Slot;
}

void FGStreamerVideoReceiver::GetDimensions(int32& OutWidth, int32& OutHeight) const
{
    OutWidth  = VideoWidth;
    OutHeight = VideoHeight;
}

FReceiverStats FGStreamerVideoReceiver::GetStats() const
{
    FReceiverStats Out;
    Out.CurrentFPS   = CurrentFPS;

    // bIsReceiving is driven by the pull thread's last-frame timestamp, not the
    // game-thread FPS counter — a UE5 hitch >500ms won't cause a false disconnect.
    if (FramePullRunnable_)
    {
        double SecsSinceLast = FPlatformTime::Seconds() - FramePullRunnable_->GetLastFrameReceivedSec();
        Out.bIsReceiving = (FramePullRunnable_->GetLastFrameReceivedSec() > 0.0 && SecsSinceLast < 1.0);
    }

    if (Stats_)
        Out.StreamHealthState = Stats_->GetStats().StreamHealthState;

    if (Stats_ && Out.bIsReceiving)
    {
        FRtpStats S = Stats_->GetStats();
        Out.PacketLossPercent  = S.LossRate * 100.0f;
        Out.PostFecLossPercent = S.PostFecLossRate * 100.0f;
        Out.JitterMs           = S.JitterMs;
        Out.OneWayLatencyMs    = S.OneWayLatencyMs;
        Out.PacketsReceived    = S.PacketsReceived;
        Out.PacketsLost        = S.PacketsLost;
        Out.PacketsRecovered   = S.PacketsRecovered;
        Out.PacketsLostPostFec = S.PacketsLostPostFec;
        Out.FramesDecoded      = S.FramesDecoded;
    }

    if (FramePullRunnable_)
    {
        double Variance = FramePullRunnable_->GetFrameIntervalVarianceMs();
        Out.FrameIntervalVarianceMs = static_cast<float>(FMath::Sqrt(Variance));
    }

    return Out;
}

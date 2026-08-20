#include "GStreamerVideoReceiver.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "RHICommandList.h"
#include "RHI.h"
#include <atomic>
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
    // of the second-to-last row: each pixel is black (0) or white (255) in RGB,
    // which after decode arrives as BGRA.  We threshold on the blue channel
    // (index 0 in BGRA) at 128 to recover each bit.

    if (Frame.Height < 3) return; // need at least 1 image row + 2 padding rows

    int32 RowBytes = Frame.Width * 4; // BGRA
    int32 TsRowOffset = (Frame.Height - 2) * RowBytes;
    int32 NeededBytes = TsRowOffset + 64 * 4; // 64 pixels * 4 bytes each

    if (Frame.Data.Num() < NeededBytes) return;

    const uint8* TsRow = Frame.Data.GetData() + TsRowOffset;

    uint64 SenderNs = 0;
    for (int32 Bit = 0; Bit < 64; ++Bit)
    {
        // BGRA layout: [B, G, R, A] — use blue channel (offset 0)
        uint8 Luma = TsRow[Bit * 4 + 0];
        if (Luma > 128)
        {
            SenderNs |= (static_cast<uint64>(1) << Bit);
        }
    }

    Frame.SenderTimestampNs = SenderNs;

    // Decode frame ID from the last padding row (Frame.Height - 1)
    int32 IdRowOffset  = (Frame.Height - 1) * RowBytes;
    int32 IdNeeded     = IdRowOffset + 64 * 4;
    if (Frame.Data.Num() >= IdNeeded)
    {
        const uint8* IdRow = Frame.Data.GetData() + IdRowOffset;
        uint64 FrameId = 0;
        for (int32 Bit = 0; Bit < 64; ++Bit)
        {
            if (IdRow[Bit * 4 + 0] > 128)
                FrameId |= (static_cast<uint64>(1) << Bit);
        }
        Frame.FrameId = FrameId;
        LastFrameId.Store(FrameId);
    }

    // Crop: remove the 2 padding rows
    Frame.Height -= 2;
    Frame.Data.SetNum(Frame.Height * RowBytes);

    // Report latency to stats (captures encode + network + decode)
    if (Stats && SenderNs > 0)
    {
        Stats->OnFrameLatency(SenderNs);
    }
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
    return FramePullRunnable_ ? FramePullRunnable_->GetPendingFrameId() : 0;
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
                    FVideoFrame Frame;
                    Frame.Width     = W;
                    Frame.Height    = H;
                    Frame.Timestamp = FPlatformTime::Seconds();
                    Frame.Data.SetNumUninitialized(BufSize);
                    GStreamerCopyBufferData(Buffer, Frame.Data.GetData(), BufSize);

                    // Extract embedded timestamp and crop the extra row
                    ExtractTimestampRow(Frame);
                    LastSenderTimeNs.Store(Frame.SenderTimestampNs);

                    // Drain stale — always keep the newest frame only
                    FVideoFrame Discard;
                    while (FrameQueue.Dequeue(Discard)) {}

                    uint64 EnqueuedId = Frame.FrameId;
                    FrameQueue.Enqueue(MoveTemp(Frame));
                    PendingFrameId.Store(EnqueuedId);
                    LastFrameReceivedSec.Store(FPlatformTime::Seconds());

                    if (Stats) Stats->OnFrameDecoded();

                    // Welford online variance for inter-frame intervals
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

bool FFramePullRunnable::PopFrame(FVideoFrame& OutFrame)
{
    bool bGot = FrameQueue.Dequeue(OutFrame);
    if (bGot)
        PendingFrameId.Store(0); // queue now empty
    return bGot;
}

// ---------------------------------------------------------------------------
// FGStreamerVideoReceiver
// ---------------------------------------------------------------------------

FGStreamerVideoReceiver::FGStreamerVideoReceiver()
{
    bUpdateInFlight = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
}

FGStreamerVideoReceiver::~FGStreamerVideoReceiver()
{
    Stop();

    if (Bus)      { GStreamerUnrefBus(Bus);             Bus      = nullptr; }
    if (AppSink)  { GStreamerUnrefElement(AppSink);     AppSink  = nullptr; }
    if (Pipeline) { GStreamerDestroyPipeline(Pipeline); Pipeline = nullptr; }
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
    bUsingGpuDecode_ = GStreamerNvdecAvailable() && GetGpuVerdict() != EGpuVerdict::Bad;
    bool bBuilt = bUsingGpuDecode_ && BuildPipeline(true);
    if (!bBuilt)
    {
        bUsingGpuDecode_ = false;
        bBuilt = BuildPipeline(false);
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

// Builds + resolves the receive pipeline. nvh264dec outputs CUDA memory, so cudadownload
// brings it back to system memory before the shared videoconvert→BGRA→appsink tail; the
// CPU path uses avdec_h264.
bool FGStreamerVideoReceiver::BuildPipeline(bool bUseGpuDecode)
{
    std::string DecodeStep = bUseGpuDecode ? "nvh264dec ! cudadownload" : "avdec_h264";
    UE_LOG(LogTemp, Log, TEXT("GStreamer: decoder: %s"),
           bUseGpuDecode ? TEXT("nvh264dec (GPU)") : TEXT("avdec_h264 (CPU)"));

    std::string PipelineStr =
        "udpsrc port=" + std::to_string(Config_.Port) +
        " caps=\"application/x-rtp,media=video,encoding-name=H264,payload=96\""
        " ! rtpjitterbuffer latency=50"
        " ! rtpulpfecdec name=fecdec"
        " ! rtph264depay name=depay"
        " ! h264parse"
        " ! " + DecodeStep +
        " ! videoconvert"
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
            if (AppSink) { GStreamerUnrefElement(AppSink); AppSink = nullptr; }
            if (Bus)     { GStreamerUnrefBus(Bus);         Bus     = nullptr; }
            GStreamerDestroyPipeline(Pipeline);
            Pipeline = nullptr;

            bUsingGpuDecode_ = false;
            if (!BuildPipeline(false) || !GStreamerStartPipeline(Pipeline))
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

    FramePullRunnable_ = MakeUnique<FFramePullRunnable>(AppSink, Stats_.Get(), Bus);
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

bool FGStreamerVideoReceiver::UpdateTexture(UTexture2D*& OutTexture)
{
    if (!AppSink || !FramePullRunnable_) return false;

    double Now = FPlatformTime::Seconds();
    double thr = 0.5;
    if (Now - LastFPSUpdateTime >= thr)
    {
        CurrentFPS = FrameCount * 1.0 / thr;
        FrameCount = 0;
        LastFPSUpdateTime = Now;
    }

    FVideoFrame Frame;
    if (!FramePullRunnable_->PopFrame(Frame)) return false;

    FrameCount++;
    VideoWidth = Frame.Width;
    VideoHeight = Frame.Height;

    if (!OutTexture
        || OutTexture->GetSizeX() != Frame.Width
        || OutTexture->GetSizeY() != Frame.Height)
    {
        UE_LOG(LogTemp, Log,
               TEXT("GStreamer: texture resize %dx%d -> %dx%d"),
               OutTexture ? OutTexture->GetSizeX() : 0,
               OutTexture ? OutTexture->GetSizeY() : 0,
               Frame.Width, Frame.Height);

        FlushRenderingCommands();

        UTexture2D* NewTex = UTexture2D::CreateTransient(
            Frame.Width, Frame.Height, PF_B8G8R8A8);
        if (!NewTex) return false;
        NewTex->SRGB = false;
        NewTex->UpdateResource();
        OutTexture = NewTex;
    }

    if (!OutTexture->GetResource() || !OutTexture->GetResource()->TextureRHI)
        return false;

    if (bUpdateInFlight->Load())
    {
        // Unstick if RHI callback never fired (e.g. render pipeline restart)
        if (FPlatformTime::Seconds() - UpdateInFlightStartTime > 0.1)
            bUpdateInFlight->Store(false);
        else
            return false;
    }
    bUpdateInFlight->Store(true);
    UpdateInFlightStartTime = FPlatformTime::Seconds();

    FUpdateTextureRegion2D* Region =
        new FUpdateTextureRegion2D(0, 0, 0, 0, Frame.Width, Frame.Height);
    TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> Pixels =
        MakeShared<TArray<uint8>, ESPMode::ThreadSafe>(MoveTemp(Frame.Data));

    TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> InFlightFlag = bUpdateInFlight;

    OutTexture->UpdateTextureRegions(
        0, 1, Region,
        static_cast<uint32>(Frame.Width * 4), 4,
        Pixels->GetData(),
        [Region, Pixels, InFlightFlag](uint8*, const FUpdateTextureRegion2D*)
        {
            delete Region;
            InFlightFlag->Store(false);
        });

    return true;
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

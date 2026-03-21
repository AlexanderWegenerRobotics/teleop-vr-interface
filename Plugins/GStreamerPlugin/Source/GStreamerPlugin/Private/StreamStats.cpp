#include "StreamStats.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static double WallTimeSec()
{
    return FDateTime::UtcNow().GetTicks() / (double)ETimespan::TicksPerSecond;
}

static uint64 WallTimeNs()
{
    static const int64 UnixEpochOffset = 621355968000000000LL;
    int64 Ticks = FDateTime::UtcNow().GetTicks();
    return static_cast<uint64>((Ticks - UnixEpochOffset) * 100LL);
}

// ---------------------------------------------------------------------------
// FReportRunnable
// ---------------------------------------------------------------------------

uint32 FReportRunnable::Run() {
    //UE_LOG(LogTemp, Log, TEXT("StreamStats: report thread started"));
    const double PeriodSec = Owner->Config.ReportIntervalMs / 1000.0;
    double NextSec = WallTimeSec() + PeriodSec;

    while (!bShouldStop)
    {
        double SleepSec = NextSec - WallTimeSec();
        if (SleepSec > 0.0)
            FPlatformProcess::Sleep(static_cast<float>(SleepSec));

        Owner->TickReport();
        NextSec += PeriodSec;
    }

    //UE_LOG(LogTemp, Log, TEXT("StreamStats: report thread stopped"));
    return 0;
}

// ---------------------------------------------------------------------------
// FStreamStats
// ---------------------------------------------------------------------------

FStreamStats::FStreamStats(const FStreamStatsConfig& InConfig)
    : Config(InConfig)
{
    ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SS) return;

    SendSocket = SS->CreateSocket(NAME_DGram, TEXT("StreamStatsFeedback"), false);
    if (SendSocket) SendSocket->SetNonBlocking(true);
}

FStreamStats::~FStreamStats()
{
    Stop();

    ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (SS)
    {
        if (SendSocket) { SS->DestroySocket(SendSocket); SendSocket = nullptr; }
    }
}

void FStreamStats::Start()
{
    ReportRunnable_ = MakeUnique<FReportRunnable>(this);
    ReportThread_   = TUniquePtr<FRunnableThread>(
        FRunnableThread::Create(ReportRunnable_.Get(),
            TEXT("StreamStatsReport"), 0, TPri_Normal));
}

void FStreamStats::Stop()
{
    if (ReportRunnable_) ReportRunnable_->Stop();
    if (ReportThread_)   { ReportThread_->WaitForCompletion(); ReportThread_.Reset(); }
    ReportRunnable_.Reset();
}

// ---------------------------------------------------------------------------
// Pre-FEC RTP probe callback path
// ---------------------------------------------------------------------------

void FStreamStats::OnRtpPacket(uint16 Seq, uint32 RtpTimestamp)
{
    FScopeLock Lock(&StatsMutex);

    // Use high-resolution monotonic clock for jitter (sub-μs on Windows)
    double NowSec = FPlatformTime::Seconds();

    if (bFirstPreFec)
    {
        LastPreFecSeq  = Seq;
        LastRtpTs      = RtpTimestamp;
        LastArrivalSec = NowSec;
        bFirstPreFec   = false;
        bFirstRtpTs    = false;
        TotalReceived++;
        IntervalReceived++;
        return;
    }

    int16 SeqDiff = static_cast<int16>(Seq - static_cast<uint16>(LastPreFecSeq));
    if (SeqDiff <= 0) return;

    if (SeqDiff > 1)
    {
        uint32 Lost    = static_cast<uint32>(SeqDiff - 1);
        TotalLost     += Lost;
        IntervalLost  += Lost;
    }

    TotalReceived++;
    IntervalReceived++;

    // RFC 3550 jitter estimate (using FPlatformTime for sub-μs resolution)
    if (!bFirstRtpTs)
    {
        double ArrivalDiffMs = (NowSec - LastArrivalSec) * 1000.0;
        double RtpDiffMs     = static_cast<double>(RtpTimestamp - LastRtpTs) / 90.0;
        double TransitDiff   = FMath::Abs(ArrivalDiffMs - RtpDiffMs);
        JitterEstimate      += (TransitDiff - JitterEstimate) / 16.0;
    }

    LastPreFecSeq  = Seq;
    LastRtpTs      = RtpTimestamp;
    LastArrivalSec = NowSec;
    bFirstRtpTs    = false;
}

// ---------------------------------------------------------------------------
// Post-FEC probe callback path
// ---------------------------------------------------------------------------

void FStreamStats::OnFecPacket(uint16 Seq)
{
    FScopeLock Lock(&StatsMutex);

    if (bFirstPostFec)
    {
        LastPostFecSeq = Seq;
        bFirstPostFec  = false;
        TotalPostFec++;
        IntervalPostFec++;
        return;
    }

    int16 SeqDiff = static_cast<int16>(Seq - static_cast<uint16>(LastPostFecSeq));
    if (SeqDiff <= 0) return;

    if (SeqDiff > 1)
    {
        uint32 Lost         = static_cast<uint32>(SeqDiff - 1);
        TotalLostPostFec   += Lost;
        IntervalLostPostFec += Lost;
    }

    TotalPostFec++;
    IntervalPostFec++;
    LastPostFecSeq = Seq;
}

void FStreamStats::OnFrameDecoded()
{
    FScopeLock Lock(&StatsMutex);
    TotalFrames++;
}

// ---------------------------------------------------------------------------
// Frame-embedded latency (called from frame pull thread)
// ---------------------------------------------------------------------------

void FStreamStats::OnFrameLatency(uint64 SenderTimestampNs)
{
    // Wall-clock comparison: NTP-disciplined system time on both ends
    uint64 NowNs    = WallTimeNs();
    int64  DeltaNs  = static_cast<int64>(NowNs) - static_cast<int64>(SenderTimestampNs);
    double LatencyMs = static_cast<double>(DeltaNs) / 1e6;

    // Sanity clamp — reject negative or absurdly large values (clock drift)
    if (LatencyMs < 0.0 || LatencyMs > 5000.0)
        return;

    // EWMA smoothing
    double Prev     = CurrentLatencyMs.Load();
    double Smoothed = (Prev == 0.0)
        ? LatencyMs  // first sample: initialize directly
        : LatencyAlpha * LatencyMs + (1.0 - LatencyAlpha) * Prev;
    CurrentLatencyMs.Store(Smoothed);
}

// ---------------------------------------------------------------------------
// Stats retrieval
// ---------------------------------------------------------------------------

FRtpStats FStreamStats::GetStats() const
{
    FScopeLock Lock(&StatsMutex);

    FRtpStats Out;
    Out.PacketsReceived    = TotalReceived;
    Out.PacketsLost        = TotalLost;
    Out.PacketsPostFec     = TotalPostFec;
    Out.PacketsLostPostFec = TotalLostPostFec;
    Out.PacketsRecovered   = (TotalLost > TotalLostPostFec)
                                 ? (TotalLost - TotalLostPostFec) : 0;
    Out.FramesDecoded      = TotalFrames;
    Out.LossRate           = CurrentLoss;
    Out.PostFecLossRate    = CurrentPostFecLoss;
    Out.JitterMs           = CurrentJitter;
    Out.OneWayLatencyMs    = static_cast<float>(CurrentLatencyMs.Load());
    return Out;
}

// ---------------------------------------------------------------------------
// Interval computation
// ---------------------------------------------------------------------------

void FStreamStats::ComputeIntervalStats()
{
    FScopeLock Lock(&StatsMutex);

    uint32 PreTotal = IntervalReceived + IntervalLost;
    CurrentLoss = (PreTotal > 0)
        ? static_cast<float>(IntervalLost) / static_cast<float>(PreTotal)
        : 0.0f;

    uint32 PostTotal = IntervalPostFec + IntervalLostPostFec;
    CurrentPostFecLoss = (PostTotal > 0)
        ? static_cast<float>(IntervalLostPostFec) / static_cast<float>(PostTotal)
        : 0.0f;

    CurrentJitter = static_cast<float>(JitterEstimate);

    IntervalReceived     = 0;
    IntervalLost         = 0;
    IntervalPostFec      = 0;
    IntervalLostPostFec  = 0;
}

// ---------------------------------------------------------------------------
// Feedback sender
// ---------------------------------------------------------------------------

void FStreamStats::SendFeedback()
{
    if (!SendSocket) return;

    FStreamFeedbackMsg Msg{};
    {
        FScopeLock Lock(&StatsMutex);
        Msg.TimestampNs     = WallTimeNs();
        Msg.LossRate        = CurrentLoss;
        Msg.JitterMs        = CurrentJitter;
        Msg.RttMs           = 0.0f;
        Msg.PacketsReceived = TotalReceived;
        Msg.PacketsLost     = TotalLost;
        Msg.FramesReceived  = TotalFrames;
    }

    ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SS) return;

    TSharedRef<FInternetAddr> Dest = SS->CreateInternetAddr();
    bool bValid = false;
    Dest->SetIp(*Config.SenderIP, bValid);
    Dest->SetPort(Config.FeedbackPort);

    if (!bValid)
    {
        UE_LOG(LogTemp, Warning,
               TEXT("StreamStats: invalid sender IP '%s'"), *Config.SenderIP);
        return;
    }

    int32 Sent = 0;
    SendSocket->SendTo(
        reinterpret_cast<const uint8*>(&Msg),
        sizeof(Msg), Sent, *Dest);
}

void FStreamStats::TickReport()
{
    ComputeIntervalStats();
    /*
    UE_LOG(LogTemp, Log, TEXT("[StreamStats] latency=%.1fms  jitter=%.2fms  preFEC_loss=%.1f%%  postFEC_loss=%.1f%%  rx=%u  lost=%u  recovered=%u"),
        CurrentLatencyMs.Load(),
        static_cast<double>(CurrentJitter),
        static_cast<double>(CurrentLoss) * 100.0,
        static_cast<double>(CurrentPostFecLoss) * 100.0,
        TotalReceived,
        TotalLost,
        (TotalLost > TotalLostPostFec ? TotalLost - TotalLostPostFec : 0)
    );
    */

    SendFeedback();
}

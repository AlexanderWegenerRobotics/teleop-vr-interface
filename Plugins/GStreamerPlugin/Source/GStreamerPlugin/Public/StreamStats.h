#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/CriticalSection.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

#pragma pack(push, 1)
struct FStreamFeedbackMsg
{
    uint64 TimestampNs = 0;
    float  LossRate = 0.0f;
    float  JitterMs = 0.0f;
    float  RttMs = 0.0f;
    uint32 PacketsReceived = 0;
    uint32 PacketsLost = 0;
    uint32 FramesReceived = 0;
};

struct FStreamStatusMsg
{
    uint32 Magic = 0;
    uint8  State = 0;
    uint64 TimestampNs = 0;
};
#pragma pack(pop)

static constexpr uint32 kStreamStatusMagic = 0x53545354;

struct FStreamStatsConfig
{
    FString SenderIP = TEXT("127.0.0.1");
    int32   FeedbackPort = 5005;
    int32   StatusPort = 5007;
    int32   ReportIntervalMs = 500;
};

struct FRtpStats
{
    uint32 PacketsReceived = 0;
    uint32 PacketsLost = 0;
    uint32 PacketsPostFec = 0;
    uint32 PacketsLostPostFec = 0;
    uint32 PacketsRecovered = 0;
    uint32 FramesDecoded = 0;

    float  LossRate = 0.0f;
    float  PostFecLossRate = 0.0f;
    float  JitterMs = 0.0f;
    float  OneWayLatencyMs = 0.0f;

    uint8  StreamHealthState = 0;
};

class FStreamStats;

class FReportRunnable : public FRunnable
{
public:
    explicit FReportRunnable(FStreamStats* InOwner) : Owner(InOwner) {}
    virtual bool   Init() override { return true; }
    virtual uint32 Run()  override;
    virtual void   Stop() override { bShouldStop = true; }
    virtual void   Exit() override {}
private:
    FStreamStats* Owner;
    TAtomic<bool> bShouldStop{ false };
};

class GSTREAMERPLUGIN_API FStreamStats
{
public:
    explicit FStreamStats(const FStreamStatsConfig& InConfig);
    ~FStreamStats();

    void Start();
    void Stop();

    void OnRtpPacket(uint16 Seq, uint32 RtpTimestamp);
    void OnFecPacket(uint16 Seq);
    void OnFrameDecoded();
    void OnFrameLatency(uint64 SenderTimestampNs);

    FRtpStats GetStats() const;

    void TickReport();

private:
    void ComputeIntervalStats();
    void SendFeedback();
    void PollStatusSocket();

    FStreamStatsConfig Config;

    mutable FCriticalSection StatsMutex;

    uint32 TotalReceived = 0;
    uint32 TotalLost = 0;
    uint32 IntervalReceived = 0;
    uint32 IntervalLost = 0;
    int64  LastPreFecSeq = -1;
    bool   bFirstPreFec = true;

    uint32 TotalPostFec = 0;
    uint32 TotalLostPostFec = 0;
    uint32 IntervalPostFec = 0;
    uint32 IntervalLostPostFec = 0;
    int64  LastPostFecSeq = -1;
    bool   bFirstPostFec = true;

    uint32 TotalFrames = 0;

    uint32 LastRtpTs = 0;
    double LastArrivalSec = 0.0;
    bool   bFirstRtpTs = true;
    double JitterEstimate = 0.0;

    float  CurrentLoss = 0.0f;
    float  CurrentPostFecLoss = 0.0f;
    float  CurrentJitter = 0.0f;

    TAtomic<double> CurrentLatencyMs{ 0.0 };
    static constexpr double LatencyAlpha = 0.1;

    TAtomic<uint8> LastStreamHealthState{ 0 };

    FSocket* SendSocket = nullptr;
    FSocket* StatusSocket = nullptr;

    TUniquePtr<FReportRunnable> ReportRunnable_;
    TUniquePtr<FRunnableThread> ReportThread_;

    friend class FReportRunnable;
};
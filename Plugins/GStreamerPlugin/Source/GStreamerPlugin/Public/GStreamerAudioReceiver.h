#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/CriticalSection.h"
#include <atomic>

class FSocket;
class FArchive;

// Receives the avatar_pipeline audio stream (see audio_streamer.hpp in teleop-simulator):
//   u32 magic 'TAUD' | u8 version=1 | u8 channels | u8 frame_ms | u8 reserved
//   u32 seq | u64 capture_ns (sender system_clock) | Opus payload
// Audio is scheduled to play at capture_ns + video latency, so it lines up with the video
// frame shown in the headset. Video latency comes from ReportVideoSenderTimeNs; the unknown
// clock offset between the two machines cancels because both streams carry sender time.
struct FAudioReceiverConfig
{
    int32   Port              = 5020;
    FString OutputDevice;
    bool    bSyncToVideo      = true;
    int32   ExtraDelayMs      = 0;
    float   GainDb            = 20.f;
    int32   SafetyMarginMs    = 5;
    int32   OutputBufferMs    = 20;
    int32   MaxHoldMs         = 400;
    int32   NoSignalTimeoutMs = 2000;
};

struct FAudioReceiverStats
{
    bool   bReceiving       = false;
    bool   bSyncedToVideo   = false;
    uint32 PacketsReceived  = 0;
    uint32 PacketsConcealed = 0;
    uint32 PacketsDropped   = 0;
    float  ArrivalDelayMs   = 0.f;
    float  PlayoutDelayMs   = 0.f;
};

class GSTREAMERPLUGIN_API FGStreamerAudioReceiver : public FRunnable
{
public:
    FGStreamerAudioReceiver() = default;
    virtual ~FGStreamerAudioReceiver();

    bool Start(const FAudioReceiverConfig& InConfig);
    void Shutdown();

    void ReportVideoSenderTimeNs(int64 SenderTimeNs);
    void StartLog(const FString& Dir);
    void StopLog();
    FAudioReceiverStats GetStats() const;

    virtual uint32 Run() override;
    virtual void   Stop() override { bStopRequested = true; }

private:
    struct FPacketMeta
    {
        uint32 Seq        = 0;
        uint64 CaptureNs  = 0;
        int64  RecvNs     = 0;
        int64  TargetNs   = 0;
        bool   bConcealed = false;
    };

    bool  BuildPipeline();
    void  DestroyPipeline();
    void  ResetTiming();
    void  HandlePacket(const uint8* Data, int32 Size, int64 RecvNs);
    void  Push(const uint8* Payload, int32 Size, uint32 Seq, const FPacketMeta& Meta);
    void  CheckBus();
    void  DrainLog();
    void  ApplyPendingLog();
    void  CloseLogFiles();
    void  WriteCsv(const FString& Line);

    FAudioReceiverConfig Config;
    FRunnableThread*     Thread = nullptr;
    FSocket*             Socket = nullptr;
    std::atomic<bool>    bStopRequested{false};

    void*  Pipeline          = nullptr;
    void*  AppSrc            = nullptr;
    void*  LogSink           = nullptr;
    void*  Bus               = nullptr;
    bool   bUseFakeSink      = false;
    int32  SinkFailures      = 0;
    double NextBuildTime     = 0.0;
    double PipelineStartTime = 0.0;
    bool   bLatencyQueried   = false;
    int64  PipelineLatencyNs = 20000000;

    int32  Channels   = 1;
    int64  FrameNs    = 10000000;
    bool   bHaveSeq   = false;
    uint32 BaseSeq    = 0;
    uint32 NextSeq    = 0;
    bool   bHaveAnchor = false;
    int64  AnchorPts   = 0;
    double AnchorEwma  = 0.0;
    double ArrivalEwma = 0.0;
    double ArrivalDev  = 0.0;
    bool   bHaveArrival = false;
    int64  LastPacketNs = 0;
    bool   bNoSignalLogged = false;
    double LastStatusLog   = 0.0;

    std::atomic<int64> VideoLatencyNs{0};
    std::atomic<int64> VideoLatencyUpdatedNs{0};

    mutable FCriticalSection StatsLock;
    FAudioReceiverStats      Stats;

    FCriticalSection LogLock;
    FString          PendingLogDir;
    bool             bLogChangePending = false;
    FArchive*        PcmFile = nullptr;
    FArchive*        CsvFile = nullptr;
    uint64           SamplesWritten = 0;
    TArray<FPacketMeta> MetaFifo;
    int32            MetaHead = 0;
};

#include "GStreamerAudioReceiver.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"
#include <chrono>

extern "C" void* GStreamerCreatePipeline(const char* description);
extern "C" bool  GStreamerStartPipeline(void* pipeline);
extern "C" void  GStreamerStopPipeline(void* pipeline);
extern "C" void  GStreamerDestroyPipeline(void* pipeline);
extern "C" void* GStreamerGetElementByName(void* pipeline, const char* name);
extern "C" void  GStreamerUnrefElement(void* element);
extern "C" void* GStreamerGetBus(void* pipeline);
extern "C" void  GStreamerUnrefBus(void* bus);
extern "C" bool  GStreamerPopBusError(void* bus, char* OutMsg, int OutMsgLen, bool* bOutIsWarning);
extern "C" void* GStreamerTryPullSample(void* appsink, double timeout_seconds);
extern "C" void* GStreamerMapSample(void* sample, const unsigned char** data, int* size);
extern "C" void  GStreamerUnmapSample(void* handle);
extern "C" void  GStreamerFreeSample(void* sample);
extern "C" bool  GStreamerGetLatency(void* pipeline, double* latency_ms, double* jitter_buffer_latency_ms);
extern "C" bool  GStreamerElementAvailable(const char* factory_name);
extern "C" bool  GStreamerFindAudioDeviceId(const char* device_class, const char* name_substring,
                                            const char* factory_name, char* out_id, int out_id_len,
                                            char* out_name, int out_name_len);
extern "C" bool  GStreamerAppSrcPush(void* appsrc, const unsigned char* data, int size,
                                     unsigned long long pts_ns, unsigned long long duration_ns);
extern "C" bool  GStreamerGetRunningTimeNs(void* pipeline, unsigned long long* out_ns);
extern "C" void  GStreamerUseSystemClock(void* pipeline);

namespace
{
    constexpr uint32 kMagic        = 0x44554154;
    constexpr uint8  kVersion      = 1;
    constexpr int32  kHeaderSize   = 20;
    constexpr int32  kMaxConceal   = 10;
    constexpr int32  kRestartGap   = 50;
    constexpr int64  kReanchorNs   = 15000000;
    constexpr int32  kMaxMetaFifo  = 2000;
    constexpr int64  kOutlierClampNs = 30000000;

    int64 NowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
}

FGStreamerAudioReceiver::~FGStreamerAudioReceiver()
{
    Shutdown();
}

bool FGStreamerAudioReceiver::Start(const FAudioReceiverConfig& InConfig)
{
    Config = InConfig;
    Socket = FUdpSocketBuilder(TEXT("TeleopAudioRx"))
        .AsNonBlocking()
        .AsReusable()
        .BoundToPort(Config.Port)
        .WithReceiveBufferSize(1 << 20)
        .Build();
    if (!Socket)
    {
        UE_LOG(LogTemp, Warning, TEXT("AudioRx: cannot bind UDP port %d, audio disabled"), Config.Port);
        return false;
    }
    bStopRequested = false;
    Thread = FRunnableThread::Create(this, TEXT("TeleopAudioRx"), 0, TPri_AboveNormal);
    UE_LOG(LogTemp, Log, TEXT("AudioRx: listening on UDP %d (sync to video: %s, extra delay %d ms, margin %d ms, output buffer %d ms)"),
           Config.Port, Config.bSyncToVideo ? TEXT("on") : TEXT("off"), Config.ExtraDelayMs,
           Config.SafetyMarginMs, Config.OutputBufferMs);
    return Thread != nullptr;
}

void FGStreamerAudioReceiver::Shutdown()
{
    bStopRequested = true;
    if (Thread)
    {
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }
    if (Socket)
    {
        Socket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
        Socket = nullptr;
    }
}

void FGStreamerAudioReceiver::ReportVideoSenderTimeNs(int64 SenderTimeNs)
{
    if (SenderTimeNs <= 0) return;
    const int64 Now = NowNs();
    const int64 Lat = Now - SenderTimeNs;
    if (Lat < -10000000000LL || Lat > 10000000000LL) return;
    const int64 Prev    = VideoLatencyNs.load();
    const int64 Updated = VideoLatencyUpdatedNs.load();
    const bool  bFresh  = Updated > 0 && Now - Updated < 1000000000LL;
    const int64 Step = FMath::Clamp(Lat - Prev, -kOutlierClampNs, kOutlierClampNs);
    VideoLatencyNs.store(bFresh ? Prev + Step / 20 : Lat);
    VideoLatencyUpdatedNs.store(Now);
}

void FGStreamerAudioReceiver::StartLog(const FString& Dir)
{
    FScopeLock Lock(&LogLock);
    PendingLogDir     = Dir;
    bLogChangePending = true;
}

void FGStreamerAudioReceiver::StopLog()
{
    FScopeLock Lock(&LogLock);
    PendingLogDir.Empty();
    bLogChangePending = true;
}

FAudioReceiverStats FGStreamerAudioReceiver::GetStats() const
{
    FScopeLock Lock(&StatsLock);
    return Stats;
}

bool FGStreamerAudioReceiver::BuildPipeline()
{
    FString SinkDesc;
    if (!bUseFakeSink && GStreamerElementAvailable("wasapi2sink"))
    {
        SinkDesc = FString::Printf(TEXT("wasapi2sink name=aout low-latency=true sync=true buffer-time=%d latency-time=%d"),
                                   Config.OutputBufferMs * 1000, FMath::Max(Config.OutputBufferMs / 4, 2) * 1000);
        if (Config.OutputDevice.IsEmpty())
        {
            UE_LOG(LogTemp, Log, TEXT("AudioRx: output device = Windows default (set \"output_device\" to pick one)"));
        }
        else
        {
            char Id[512] = {0};
            char Name[256] = {0};
            if (GStreamerFindAudioDeviceId("Audio/Sink", TCHAR_TO_UTF8(*Config.OutputDevice), "wasapi2sink",
                                           Id, sizeof(Id), Name, sizeof(Name)))
            {
                SinkDesc += FString::Printf(TEXT(" device=\"%s\""), UTF8_TO_TCHAR(Id));
                UE_LOG(LogTemp, Log, TEXT("AudioRx: output device '%s'"), UTF8_TO_TCHAR(Name));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("AudioRx: no output device matching '%s', using the Windows default"),
                       *Config.OutputDevice);
            }
        }
    }
    else
    {
        SinkDesc = TEXT("fakesink name=aout sync=true");
        UE_LOG(LogTemp, Warning, TEXT("AudioRx: no usable audio output, audio is logged but not played"));
    }

    const FString Desc = FString::Printf(
        TEXT("appsrc name=asrc is-live=true format=time do-timestamp=false block=false max-bytes=0 ")
        TEXT("caps=audio/x-opus,rate=48000,channels=%d,channel-mapping-family=0 ")
        TEXT("! opusdec plc=true ! audioconvert ! audioresample ")
        TEXT("! audio/x-raw,format=S16LE,rate=48000,channels=%d ! tee name=at ")
        TEXT("at. ! queue max-size-time=300000000 max-size-buffers=0 max-size-bytes=0 ")
        TEXT("! audioconvert ! volume volume=%.3f ! audiodynamic mode=compressor characteristics=soft-knee threshold=0.5 ratio=0.25 ")
        TEXT("! audioconvert ! %s ")
        TEXT("at. ! queue max-size-time=1000000000 max-size-buffers=0 max-size-bytes=0 leaky=downstream ")
        TEXT("! appsink name=alog sync=false emit-signals=false max-buffers=1000 drop=true"),
        Channels, Channels, FMath::Pow(10.f, Config.GainDb / 20.f), *SinkDesc);

    Pipeline = GStreamerCreatePipeline(TCHAR_TO_UTF8(*Desc));
    if (!Pipeline)
    {
        UE_LOG(LogTemp, Error, TEXT("AudioRx: failed to create pipeline: %s"), *Desc);
        return false;
    }
    GStreamerUseSystemClock(Pipeline);
    AppSrc  = GStreamerGetElementByName(Pipeline, "asrc");
    LogSink = GStreamerGetElementByName(Pipeline, "alog");
    Bus     = GStreamerGetBus(Pipeline);
    if (!AppSrc || !LogSink || !GStreamerStartPipeline(Pipeline))
    {
        UE_LOG(LogTemp, Error, TEXT("AudioRx: pipeline failed to start"));
        DestroyPipeline();
        return false;
    }
    PipelineStartTime = FPlatformTime::Seconds();
    bLatencyQueried   = false;
    ResetTiming();
    UE_LOG(LogTemp, Log, TEXT("AudioRx: playback pipeline running (%d ch, %lld ms frames, gain %+.0f dB)"),
           Channels, FrameNs / 1000000, Config.GainDb);
    return true;
}

void FGStreamerAudioReceiver::DestroyPipeline()
{
    if (AppSrc)  { GStreamerUnrefElement(AppSrc);  AppSrc  = nullptr; }
    if (LogSink) { GStreamerUnrefElement(LogSink); LogSink = nullptr; }
    if (Bus)     { GStreamerUnrefBus(Bus);         Bus     = nullptr; }
    if (Pipeline)
    {
        GStreamerStopPipeline(Pipeline);
        GStreamerDestroyPipeline(Pipeline);
        Pipeline = nullptr;
    }
    MetaFifo.Reset();
    MetaHead = 0;
    ResetTiming();
}

void FGStreamerAudioReceiver::ResetTiming()
{
    bHaveSeq     = false;
    bHaveAnchor  = false;
    bHaveArrival = false;
}

void FGStreamerAudioReceiver::HandlePacket(const uint8* Data, int32 Size, int64 RecvNs)
{
    if (Size <= kHeaderSize) return;
    uint32 Magic = 0;
    FMemory::Memcpy(&Magic, Data, 4);
    if (Magic != kMagic || Data[4] != kVersion) return;

    const int32 PktChannels = FMath::Max<int32>(1, Data[5]);
    const int64 PktFrameNs  = FMath::Max<int64>(1, Data[6]) * 1000000LL;
    uint32 Seq = 0;
    uint64 CaptureNs = 0;
    FMemory::Memcpy(&Seq, Data + 8, 4);
    FMemory::Memcpy(&CaptureNs, Data + 12, 8);

    if (Pipeline && (PktChannels != Channels || PktFrameNs != FrameNs))
    {
        UE_LOG(LogTemp, Log, TEXT("AudioRx: stream format changed, rebuilding"));
        DestroyPipeline();
    }
    if (!Pipeline)
    {
        if (FPlatformTime::Seconds() < NextBuildTime) return;
        Channels = PktChannels;
        FrameNs  = PktFrameNs;
        if (!BuildPipeline())
        {
            NextBuildTime = FPlatformTime::Seconds() + 5.0;
            return;
        }
    }

    LastPacketNs = RecvNs;
    if (bNoSignalLogged)
    {
        UE_LOG(LogTemp, Log, TEXT("AudioRx: audio resumed"));
        bNoSignalLogged = false;
    }

    double Arrival = static_cast<double>(RecvNs - static_cast<int64>(CaptureNs));
    if (bHaveArrival)
        Arrival = FMath::Clamp(Arrival, ArrivalEwma - kOutlierClampNs, ArrivalEwma + kOutlierClampNs);
    if (!bHaveArrival)
    {
        ArrivalEwma  = Arrival;
        ArrivalDev   = 0.0;
        bHaveArrival = true;
    }
    else
    {
        ArrivalDev  = 0.95 * ArrivalDev + 0.05 * FMath::Abs(Arrival - ArrivalEwma);
        ArrivalEwma = 0.95 * ArrivalEwma + 0.05 * Arrival;
    }

    if (!bHaveSeq)
    {
        BaseSeq  = Seq;
        NextSeq  = Seq;
        bHaveSeq = true;
    }
    int32 Diff = static_cast<int32>(Seq - NextSeq);
    if (Diff < 0)
    {
        if (Diff > -kRestartGap)
        {
            FScopeLock Lock(&StatsLock);
            ++Stats.PacketsDropped;
            return;
        }
        UE_LOG(LogTemp, Log, TEXT("AudioRx: sender restarted, resyncing"));
        BaseSeq     = Seq;
        NextSeq     = Seq;
        bHaveAnchor = false;
        Diff        = 0;
    }

    const int64 VideoUpdated = VideoLatencyUpdatedNs.load();
    const bool  bSynced = Config.bSyncToVideo && VideoUpdated > 0 && RecvNs - VideoUpdated < 1000000000LL;
    const int64 MinDelay = static_cast<int64>(ArrivalEwma + 3.0 * ArrivalDev) + PipelineLatencyNs
                         + static_cast<int64>(Config.SafetyMarginMs) * 1000000LL;
    const int64 Extra    = static_cast<int64>(Config.ExtraDelayMs) * 1000000LL;
    int64 Delay = bSynced ? FMath::Max(VideoLatencyNs.load() + Extra, MinDelay) : MinDelay + Extra;
    Delay = FMath::Min(Delay, static_cast<int64>(ArrivalEwma) + static_cast<int64>(Config.MaxHoldMs) * 1000000LL);

    unsigned long long RunningNow = 0;
    if (!GStreamerGetRunningTimeNs(Pipeline, &RunningNow)) return;
    const int64 TargetWall  = static_cast<int64>(CaptureNs) + Delay;
    const int64 TargetPts   = static_cast<int64>(RunningNow) + (TargetWall - NowNs()) - PipelineLatencyNs;
    const int64 Rel         = static_cast<int64>(static_cast<uint32>(Seq - BaseSeq));
    const double Desired    = static_cast<double>(TargetPts - Rel * FrameNs);
    if (!bHaveAnchor)
    {
        AnchorPts   = static_cast<int64>(Desired);
        AnchorEwma  = Desired;
        bHaveAnchor = true;
    }
    else
    {
        AnchorEwma = 0.95 * AnchorEwma + 0.05 * Desired;
        if (FMath::Abs(AnchorEwma - static_cast<double>(AnchorPts)) > static_cast<double>(kReanchorNs))
        {
            UE_LOG(LogTemp, Log, TEXT("AudioRx: playout re-anchored by %.1f ms"),
                   (AnchorEwma - static_cast<double>(AnchorPts)) / 1e6);
            AnchorPts = static_cast<int64>(AnchorEwma);
        }
    }

    if (Diff > 0 && Diff <= kMaxConceal)
    {
        for (int32 i = 0; i < Diff; ++i)
        {
            FPacketMeta Lost;
            Lost.Seq        = NextSeq + i;
            Lost.bConcealed = true;
            Push(nullptr, 0, Lost.Seq, Lost);
        }
        FScopeLock Lock(&StatsLock);
        Stats.PacketsConcealed += Diff;
    }

    FPacketMeta Meta;
    Meta.Seq       = Seq;
    Meta.CaptureNs = CaptureNs;
    Meta.RecvNs    = RecvNs;
    Meta.TargetNs  = TargetWall;
    Push(Data + kHeaderSize, Size - kHeaderSize, Seq, Meta);
    NextSeq = Seq + 1;

    FScopeLock Lock(&StatsLock);
    Stats.bReceiving      = true;
    Stats.bSyncedToVideo  = bSynced;
    ++Stats.PacketsReceived;
    Stats.ArrivalDelayMs  = static_cast<float>(ArrivalEwma / 1e6);
    Stats.PlayoutDelayMs  = static_cast<float>(Delay / 1e6);
}

void FGStreamerAudioReceiver::Push(const uint8* Payload, int32 Size, uint32 Seq, const FPacketMeta& Meta)
{
    const int64 Rel = static_cast<int64>(static_cast<uint32>(Seq - BaseSeq));
    const int64 Pts = AnchorPts + Rel * FrameNs;
    if (Pts < 0) return;
    if (MetaFifo.Num() - MetaHead < kMaxMetaFifo) MetaFifo.Add(Meta);
    GStreamerAppSrcPush(AppSrc, Payload, Size, static_cast<unsigned long long>(Pts),
                        static_cast<unsigned long long>(FrameNs));
}

void FGStreamerAudioReceiver::CheckBus()
{
    char Msg[1024];
    bool bWarning = false;
    while (Bus && GStreamerPopBusError(Bus, Msg, sizeof(Msg), &bWarning))
    {
        if (bWarning)
        {
            UE_LOG(LogTemp, Verbose, TEXT("AudioRx: %s"), UTF8_TO_TCHAR(Msg));
            continue;
        }
        UE_LOG(LogTemp, Warning, TEXT("AudioRx: playback error: %s"), UTF8_TO_TCHAR(Msg));
        if (!bUseFakeSink && ++SinkFailures >= 2)
        {
            bUseFakeSink = true;
            UE_LOG(LogTemp, Warning, TEXT("AudioRx: audio output keeps failing, continuing with logging only"));
        }
        DestroyPipeline();
        NextBuildTime = FPlatformTime::Seconds() + 2.0;
        return;
    }

    if (Pipeline && !bLatencyQueried && FPlatformTime::Seconds() - PipelineStartTime > 1.0)
    {
        double LatencyMs = 0.0, Unused = 0.0;
        if (GStreamerGetLatency(Pipeline, &LatencyMs, &Unused))
        {
            PipelineLatencyNs = static_cast<int64>(LatencyMs * 1e6);
            UE_LOG(LogTemp, Log, TEXT("AudioRx: playback pipeline latency %.1f ms"), LatencyMs);
        }
        bLatencyQueried = true;
    }
}

void FGStreamerAudioReceiver::WriteCsv(const FString& Line)
{
    if (!CsvFile) return;
    FTCHARToUTF8 Utf8(*Line);
    CsvFile->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
}

void FGStreamerAudioReceiver::DrainLog()
{
    if (!LogSink) return;
    while (void* Sample = GStreamerTryPullSample(LogSink, 0.0))
    {
        const unsigned char* Data = nullptr;
        int Size = 0;
        void* Handle = GStreamerMapSample(Sample, &Data, &Size);
        GStreamerFreeSample(Sample);

        FPacketMeta Meta;
        if (MetaHead < MetaFifo.Num()) Meta = MetaFifo[MetaHead++];
        if (MetaHead > 512)
        {
            MetaFifo.RemoveAt(0, MetaHead, EAllowShrinking::No);
            MetaHead = 0;
        }

        if (Handle && Data && Size > 0 && PcmFile)
        {
            const int32 NSamples = Size / (2 * Channels);
            PcmFile->Serialize(const_cast<unsigned char*>(Data), Size);
            WriteCsv(FString::Printf(TEXT("%llu,%d,%u,%llu,%lld,%lld,%d\n"),
                SamplesWritten, NSamples, Meta.Seq, Meta.CaptureNs, Meta.RecvNs, Meta.TargetNs,
                Meta.bConcealed ? 1 : 0));
            SamplesWritten += NSamples;
        }
        if (Handle) GStreamerUnmapSample(Handle);
    }
}

void FGStreamerAudioReceiver::CloseLogFiles()
{
    if (PcmFile) { PcmFile->Close(); delete PcmFile; PcmFile = nullptr; }
    if (CsvFile) { CsvFile->Close(); delete CsvFile; CsvFile = nullptr; }
    SamplesWritten = 0;
}

void FGStreamerAudioReceiver::ApplyPendingLog()
{
    FString Dir;
    {
        FScopeLock Lock(&LogLock);
        if (!bLogChangePending) return;
        bLogChangePending = false;
        Dir = PendingLogDir;
    }
    CloseLogFiles();
    if (Dir.IsEmpty()) return;

    IFileManager::Get().MakeDirectory(*Dir, true);
    PcmFile = IFileManager::Get().CreateFileWriter(*(Dir / TEXT("audio.pcm")));
    CsvFile = IFileManager::Get().CreateFileWriter(*(Dir / TEXT("audio_timestamps.csv")));
    if (!PcmFile || !CsvFile)
    {
        UE_LOG(LogTemp, Warning, TEXT("AudioRx: cannot open audio log in %s"), *Dir);
        CloseLogFiles();
        return;
    }
    WriteCsv(FString::Printf(TEXT("# s16le 48000 Hz %d ch in audio.pcm; sample_index counts samples per channel\n"), Channels));
    WriteCsv(TEXT("sample_index,n_samples,seq,capture_ns,recv_ns,target_ns,concealed\n"));
    UE_LOG(LogTemp, Log, TEXT("AudioRx: logging audio to %s"), *Dir);
}

uint32 FGStreamerAudioReceiver::Run()
{
    TArray<uint8> Buf;
    Buf.SetNumUninitialized(4096);

    while (!bStopRequested)
    {
        Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(5));
        uint32 PendingSize = 0;
        while (Socket->HasPendingData(PendingSize))
        {
            int32 Read = 0;
            if (!Socket->Recv(Buf.GetData(), Buf.Num(), Read) || Read <= 0) break;
            HandlePacket(Buf.GetData(), Read, NowNs());
        }

        if (Pipeline)
        {
            CheckBus();
            DrainLog();
        }
        ApplyPendingLog();

        const int64 Now = NowNs();
        if (LastPacketNs > 0 && !bNoSignalLogged &&
            Now - LastPacketNs > static_cast<int64>(Config.NoSignalTimeoutMs) * 1000000LL)
        {
            UE_LOG(LogTemp, Warning, TEXT("AudioRx: no audio for %d ms (video unaffected)"), Config.NoSignalTimeoutMs);
            bNoSignalLogged = true;
            ResetTiming();
            FScopeLock Lock(&StatsLock);
            Stats.bReceiving = false;
        }

        const double T = FPlatformTime::Seconds();
        if (T - LastStatusLog > 10.0)
        {
            LastStatusLog = T;
            const FAudioReceiverStats S = GetStats();
            if (S.bReceiving)
                UE_LOG(LogTemp, Log, TEXT("AudioRx: %u pkts, %u concealed, %u dropped | arrival %.1f ms, playout %.1f ms, %s"),
                       S.PacketsReceived, S.PacketsConcealed, S.PacketsDropped, S.ArrivalDelayMs, S.PlayoutDelayMs,
                       S.bSyncedToVideo ? TEXT("synced to video") : TEXT("audio-only timing"));
        }
    }

    DestroyPipeline();
    CloseLogFiles();
    return 0;
}

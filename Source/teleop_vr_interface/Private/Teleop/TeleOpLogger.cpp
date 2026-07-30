#include "Teleop/TeleOpLogger.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"

// ============================================================================
// FBufferedFileWriter
// ============================================================================

FBufferedFileWriter::FBufferedFileWriter(const FString& Path, const FString& Header,
                                         int32 FlushIntervalRows)
    : FlushInterval_(FlushIntervalRows)
{
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    FileHandle_ = PF.OpenWrite(*Path, false, false);

    if (FileHandle_ && !Header.IsEmpty())
    {
        FTCHARToUTF8 Converted(*Header);
        FileHandle_->Write(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
    }

    Thread_ = FRunnableThread::Create(this, TEXT("TeleOpLogWriter"), 0, TPri_BelowNormal);
}

FBufferedFileWriter::~FBufferedFileWriter()
{
    Stop();
}

void FBufferedFileWriter::Stop()
{
    if (bStopped_.Exchange(true)) return;

    bStop_ = true;
    if (Thread_)
    {
        Thread_->WaitForCompletion();
        delete Thread_;
        Thread_ = nullptr;
    }

    Flush();

    if (FileHandle_)
    {
        delete FileHandle_;
        FileHandle_ = nullptr;
    }
}

void FBufferedFileWriter::WriteRow(const FString& Row)
{
    if (bStop_) return;
    FScopeLock Lock(&Lock_);
    Pending_.Add(Row);
}

uint32 FBufferedFileWriter::Run()
{
    while (!bStop_)
    {
        {
            FScopeLock Lock(&Lock_);
            if (Pending_.Num() >= FlushInterval_)
                Flush();
        }
        FPlatformProcess::Sleep(0.001f);
    }
    return 0;
}

void FBufferedFileWriter::Flush()
{
    if (!FileHandle_ || Pending_.Num() == 0) return;

    FString Combined;
    Combined.Reserve(Pending_.Num() * 128);
    for (const FString& Row : Pending_)
        Combined += Row;
    Pending_.Reset();

    FTCHARToUTF8 Converted(*Combined);
    FileHandle_->Write(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
}

// ============================================================================
// FTeleOpLogger
// ============================================================================

FString FTeleOpLogger::Open(const FString& BaseDir)
{
    const FString SessionDir = BaseDir / MakeSessionTimestamp();
    FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*SessionDir);

    StreamWriter_ = MakeUnique<FBufferedFileWriter>(
        SessionDir / TEXT("stream.csv"), StreamHeader());

    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    EventHandle_ = PF.OpenWrite(*(SessionDir / TEXT("events.log")), false, false);

    LogEvent(FString::Printf(TEXT("SESSION_START utc=%s"), *FDateTime::UtcNow().ToString()));

    return SessionDir;
}

void FTeleOpLogger::Close()
{
    if (!StreamWriter_ && !EventHandle_) return;

    LogEvent(TEXT("SESSION_END"));

    if (StreamWriter_) { StreamWriter_->Stop(); StreamWriter_.Reset(); }
    if (EventHandle_)  { delete EventHandle_;   EventHandle_ = nullptr; }
}

void FTeleOpLogger::WriteStreamRow(const FStreamRow& R)
{
    if (!StreamWriter_) return;

    StreamWriter_->WriteRow(FString::Printf(
        TEXT("%llu;%d;")
        TEXT("%.4f;%d;%.0f;%.6f;%.6f;%.6f;%.6f;%.6f;%.6f;%.6f;")
        TEXT("%.4f;%d;%.0f;%.6f;%.6f;%.6f;%.6f;%.6f;%.6f;%.6f;")
        TEXT("%.6f;%.6f;")
        TEXT("%.2f;%.3f;%.3f;%d;")
        TEXT("%.2f;%.1f;")
        TEXT("%.4f;%d;%.4f;%d\n"),
        R.TimestampNs, R.OperatorState,
        R.LeftClutch,  R.LeftGear,  R.LeftGrasp,
        R.LeftPx,  R.LeftPy,  R.LeftPz,
        R.LeftQw,  R.LeftQx,  R.LeftQy,  R.LeftQz,
        R.RightClutch, R.RightGear, R.RightGrasp,
        R.RightPx, R.RightPy, R.RightPz,
        R.RightQw, R.RightQx, R.RightQy, R.RightQz,
        R.HeadPan, R.HeadTilt,
        R.VideoLatencyMs, R.VideoJitterMs, R.VideoLossPct, R.VideoFps,
        R.DataLatencyMs, R.DataMsgRateHz,
        R.LeftGripperWidth, R.LeftGripperGraspState, R.RightGripperWidth, R.RightGripperGraspState));
}

void FTeleOpLogger::LogEvent(const FString& Message)
{
    if (!EventHandle_) return;
    const FString Line = FString::Printf(TEXT("%llu  %s\n"), NowNs(), *Message);
    FTCHARToUTF8 Converted(*Line);
    EventHandle_->Write(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
}

uint64 FTeleOpLogger::NowNs()
{
    static const int64 UnixEpochOffset = 621355968000000000LL;
    return static_cast<uint64>((FDateTime::UtcNow().GetTicks() - UnixEpochOffset) * 100LL);
}

FString FTeleOpLogger::MakeSessionTimestamp()
{
    const FDateTime Now = FDateTime::UtcNow();
    return FString::Printf(TEXT("%04d%02d%02d_%02d%02d%02d"),
        Now.GetYear(), Now.GetMonth(), Now.GetDay(),
        Now.GetHour(), Now.GetMinute(), Now.GetSecond());
}

FString FTeleOpLogger::StreamHeader()
{
    return
        TEXT("timestamp_ns;operator_state;")
        TEXT("left_clutch;left_gear;left_grasp;")
        TEXT("left_px;left_py;left_pz;left_qw;left_qx;left_qy;left_qz;")
        TEXT("right_clutch;right_gear;right_grasp;")
        TEXT("right_px;right_py;right_pz;right_qw;right_qx;right_qy;right_qz;")
        TEXT("head_pan;head_tilt;")
        TEXT("video_latency_ms;video_jitter_ms;video_loss_pct;video_fps;")
        TEXT("data_latency_ms;data_msg_rate_hz;")
        TEXT("left_gripper_width;left_grasp_state;right_gripper_width;right_grasp_state\n");
}

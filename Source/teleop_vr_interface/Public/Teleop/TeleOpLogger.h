#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"

// ---------------------------------------------------------------------------
// FBufferedFileWriter
//   Background-thread CSV writer. WriteRow() is thread-safe and returns
//   immediately; actual disk I/O happens on a dedicated low-priority thread.
// ---------------------------------------------------------------------------
class FBufferedFileWriter : public FRunnable
{
public:
    FBufferedFileWriter(const FString& Path, const FString& Header,
                        int32 FlushIntervalRows = 500);
    ~FBufferedFileWriter();

    void WriteRow(const FString& Row);
    void Stop();

private:
    virtual uint32 Run() override;
    void Flush();

    IFileHandle*     FileHandle_ = nullptr;
    FRunnableThread* Thread_     = nullptr;
    FCriticalSection Lock_;
    TArray<FString>  Pending_;
    TAtomic<bool>    bStop_    {false};
    TAtomic<bool>    bStopped_ {false};
    int32            FlushInterval_;
};

// ---------------------------------------------------------------------------
// FStreamRow
//   One row of the per-frame continuous log. All pose values are in protocol
//   (robot) coordinates. Zero when the relevant arm is not sending.
// ---------------------------------------------------------------------------
struct FStreamRow
{
    uint64 TimestampNs   = 0;
    uint8  OperatorState = 0;

    // Left controller
    float  LeftClutch    = 0.f;   // 0..1, 1 = fully clutched (not sending)
    uint8  LeftGear      = 1;     // motion scale factor
    float  LeftGrasp     = 0.f;   // 0 or 1
    float  LeftPx = 0.f, LeftPy = 0.f, LeftPz = 0.f;
    float  LeftQw = 1.f, LeftQx = 0.f, LeftQy = 0.f, LeftQz = 0.f;

    // Right controller
    float  RightClutch   = 0.f;
    uint8  RightGear     = 1;
    float  RightGrasp    = 0.f;
    float  RightPx = 0.f, RightPy = 0.f, RightPz = 0.f;
    float  RightQw = 1.f, RightQx = 0.f, RightQy = 0.f, RightQz = 0.f;

    // Head command (radians, delta from origin)
    float  HeadPan  = 0.f;
    float  HeadTilt = 0.f;

    // Video stream stats
    float  VideoLatencyMs = 0.f;
    float  VideoJitterMs  = 0.f;
    float  VideoLossPct   = 0.f;
    int32  VideoFps       = 0;

    // Data stream stats (arm state stream, avatar → operator)
    float  DataLatencyMs  = 0.f;
    float  DataMsgRateHz  = 0.f;
};

// ---------------------------------------------------------------------------
// FTeleOpLogger
//   Plain C++ class — no Unreal reflection overhead.
//   Owned as a TUniquePtr by OperatorPawn.
//
//   Session layout: <BaseDir>/YYYYMMDD_HHMMSS/
//     stream.csv   — FStreamRow, one row per game tick
//     events.log   — timestamped freeform lines (state changes, inputs, etc.)
// ---------------------------------------------------------------------------
class FTeleOpLogger
{
public:
    FTeleOpLogger()  = default;
    ~FTeleOpLogger() { Close(); }

    // Open files. Creates <BaseDir>/YYYYMMDD_HHMMSS/ and returns the full
    // session directory path so callers can co-locate other logs.
    FString Open(const FString& BaseDir);
    void    Close();

    void WriteStreamRow(const FStreamRow& Row);

    // Write a timestamped line to events.log. Use KEY=VALUE pairs after the
    // event type for easy parsing: "STATE_TRANSITION old=IDLE new=ENGAGED"
    void LogEvent(const FString& Message);

    static uint64  NowNs();

private:
    static FString MakeSessionTimestamp();
    static FString StreamHeader();

    TUniquePtr<FBufferedFileWriter> StreamWriter_;
    IFileHandle*                    EventHandle_ = nullptr;
};

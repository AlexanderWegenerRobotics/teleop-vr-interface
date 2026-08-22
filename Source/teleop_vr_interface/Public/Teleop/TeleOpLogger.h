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

    // Robot gripper feedback (avatar → operator). GraspState fields hold the operator-facing
    // EGraspDisplayState value (0 Unknown / 1 Open / 2 Held / 3 Lost), not the raw wire enum.
    float  LeftGripperWidth      = 0.f;
    uint8  LeftGripperGraspState  = 0;
    float  RightGripperWidth     = 0.f;
    uint8  RightGripperGraspState = 0;

    // ---- Remote (avatar) health -------------------------------------------
    // The avatar's OWN view of itself, as carried in MsgHeader. Distinct from
    // everything above, which describes the LINK. The 2026-08-09 desk test
    // showed why that distinction has to be logged: the avatar's control
    // thread faulted, its 200 Hz state thread kept transmitting the last known
    // pose, and every link metric stayed healthy. GetArmRemoteState() already
    // drives a HUD indicator, but nothing wrote it to disk, so afterwards
    // there was no way to establish whether the fault had ever been signalled.
    uint8  ArmRemoteState   = 0;   // SysState as reported by the avatar
    uint8  ArmRemoteFault   = 0;   // FaultCode as reported by the avatar
    uint32 ArmDroppedPackets = 0;  // cumulative gaps in header.sequence

    // Age of the newest arm state sample at the moment this row was written:
    //     receive_wall_clock - header.sample_time_ns
    // sample_time_ns is stamped when the control loop READ the robot, while
    // timestamp_ns is stamped at send time. DataLatencyMs (built from the
    // latter) therefore measures transport and cannot see a frozen control
    // loop; this field can. Negative or absurd values mean the two hosts'
    // clocks disagree -- treat large constant offsets with suspicion.
    float  ArmStateAgeMs    = 0.f;

    // Which video source the main view is showing ("avatar", "TWIN", ...).
    // Without this the viewmode toggle silently changes what
    // VideoLatencyMs refers to -- in the desk test it dropped 91 ms -> 14 ms
    // for six seconds because the operator switched to the loopback twin feed,
    // which is only recoverable by cross-referencing events.log.
    FString VideoSourceName;
};

// One row per arm command actually placed on the wire.
//
// stream.csv is written on the HUD cadence -- about 26 Hz in the 2026-08-09
// session -- while commands go out at the tick rate and the link runs at
// 200 Hz. That sampling is well below Nyquist for the delays being measured:
// nothing faster than ~76 ms is resolvable, and the operator-hand-to-robot lag
// (expected to be tens of ms) came out unmeasurable, with zero of seven
// clutch-stable segments producing a usable cross-correlation.
//
// Logging here, at the send site, fixes that. It records the exact payload
// that went out together with the sequence number, so the hand-to-command lag
// becomes a direct join against the avatar's arm.csv rather than a correlation
// against a heavily undersampled proxy.
//
// Rows are small and the writer is the same background-thread CSV writer used
// for stream.csv, so this costs a struct copy on the game thread.
struct FCommandRow
{
    uint64 TimestampNs = 0;    // when the command was handed to the socket
    uint8  DeviceIndex = 0;    // 0 = left, 1 = right
    uint32 Sequence    = 0;    // MsgHeader.sequence; join key against the avatar log
    bool   bSent       = false; // false = computed but suppressed (arm not ENGAGED)

    // Protocol-frame delta pose, exactly as transmitted.
    float  Px = 0.f, Py = 0.f, Pz = 0.f;
    float  Qw = 1.f, Qx = 0.f, Qy = 0.f, Qz = 0.f;
    float  Gripper = 0.f;

    // Clutch at send time. The hand-to-robot mapping is re-indexed on every
    // clutch edge, so any correlation has to be computed within a segment of
    // constant clutch -- without this the segmentation has to be reconstructed
    // from events.log at 1 ms resolution and guessed at in between.
    float  ClutchFactor = 0.f;
    bool   bFullClutch  = false;
};

// ---------------------------------------------------------------------------
// FTeleOpLogger
//   Plain C++ class — no Unreal reflection overhead.
//   Owned as a TUniquePtr by OperatorPawn.
//
//   Session layout: <BaseDir>/YYYYMMDD_HHMMSS/
//     stream.csv   — FStreamRow, one row per game tick
//     command.csv  — FCommandRow, one row per arm command sent (link rate)
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

    // Called from the arm-command send path, i.e. at the tick/link rate rather
    // than the HUD rate. See FCommandRow.
    void WriteCommandRow(const FCommandRow& Row);

    // Write a timestamped line to events.log. Use KEY=VALUE pairs after the
    // event type for easy parsing: "STATE_TRANSITION old=IDLE new=ENGAGED"
    void LogEvent(const FString& Message);

    static uint64  NowNs();

private:
    static FString MakeSessionTimestamp();
    static FString StreamHeader();
    static FString CommandHeader();

    TUniquePtr<FBufferedFileWriter> StreamWriter_;
    TUniquePtr<FBufferedFileWriter> CommandWriter_;
    IFileHandle*                    EventHandle_ = nullptr;
};

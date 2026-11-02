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

    // Data stream stats (arm state, avatar -> operator). DataLatencyMs is receive
    // clock minus avatar send clock and includes the clock offset; NetDelayMs is the
    // network delay.
    float  DataLatencyMs  = 0.f;
    float  DataMsgRateHz  = 0.f;

    // Robot gripper feedback (avatar → operator). GraspState fields hold the operator-facing
    // EGraspDisplayState value (0 Unknown / 1 Open / 2 Held / 3 Lost), not the raw wire enum.
    float  LeftGripperWidth      = 0.f;
    uint8  LeftGripperGraspState  = 0;
    float  RightGripperWidth     = 0.f;
    uint8  RightGripperGraspState = 0;

    // ---- Remote (avatar) health ----
    // The avatar's own state from MsgHeader, as opposed to the link metrics above. A
    // faulted control thread can leave every link metric healthy.
    uint8  ArmRemoteState   = 0;   // SysState as reported by the avatar
    uint8  ArmRemoteFault   = 0;   // FaultCode as reported by the avatar
    uint32 ArmDroppedPackets = 0;  // cumulative gaps in header.sequence

    // Age of the newest arm sample when this row was written: receive_wall_clock -
    // header.sample_time_ns. Stamped when the control loop read the robot, so it
    // catches a frozen loop that transport latency cannot.
    float  ArmStateAgeMs    = 0.f;

    // ---- Operator-side frame budget ----
    // SendArmCommands runs once per game tick, so the command rate is the frame rate.
    // Logged to see which thread limits it.
    float  FrameMs        = 0.f;
    float  GameThreadMs   = 0.f;
    float  RenderThreadMs = 0.f;
    float  GpuMs          = 0.f;

    // EControllerTrackingState per side: 0 Tracking / 1 Lost / 2 Stale (includes
    // IMU-only dead reckoning).
    uint8  LeftTrackState  = 0;
    uint8  RightTrackState = 0;

    // Which source the main view shows ("avatar", "TWIN", ...); VideoLatencyMs
    // depends on it.
    FString VideoSourceName;

    // Command thread health at the HUD rate. CommandDuplicatePct is the fraction of
    // located poses identical to the previous one, i.e. cached samples instead of
    // fresh predictions.
    float  CommandRateHz       = 0.f;
    float  CommandJitterMs     = 0.f;
    float  CommandDuplicatePct = 0.f;
    // Cumulative, both arms: tracker discontinuities the jump guard discarded.
    int32  CommandRejectedJumps = 0;

    // Network one-way delay, same value as the HUD pill
    // (UComLink::GetNetworkDelayMs): ((recv - send) - echo_hold_us) / 2, worse arm,
    // EWMA. -1 = unknown.
    float  NetDelayMs = -1.f;
};

// One row per arm command placed on the wire, with its sequence number, so
// hand-to-robot lag is a direct join against the avatar's arm.csv. stream.csv runs at
// the HUD rate and is too coarse for that.
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

    // Clutch at send time. The mapping re-indexes on every clutch edge, so
    // correlations are computed per constant-clutch segment.
    float  ClutchFactor = 0.f;
    bool   bFullClutch  = false;

    // All *_ns below are std::chrono::system_clock on this machine (timestamp_ns()),
    // except RobotTxNs / RobotSampleNs, which are the avatar's clock.
    uint8  PoseSource   = 0;   // 1 = OpenXR sampled on the command thread, 0 = game-thread fallback
    uint64 PoseSampleNs = 0;
    float  HandPx = 0.f, HandPy = 0.f, HandPz = 0.f;               // UE world, cm
    float  HandQw = 1.f, HandQx = 0.f, HandQy = 0.f, HandQz = 0.f;
    uint64 CmdSendNs    = 0;

    uint64 RobotRecvNs   = 0;
    uint64 RobotTxNs     = 0;
    uint64 RobotSampleNs = 0;
    uint32 RobotSeq      = 0;
    uint32 AppliedSeq    = 0;
    float  RobotPx = 0.f, RobotPy = 0.f, RobotPz = 0.f;            // avatar world, m
    float  RobotQw = 1.f, RobotQx = 0.f, RobotQy = 0.f, RobotQz = 0.f;
    float  RttMs         = 0.f;
    // Raw (unsmoothed) one-way network delay from the newest echo on this arm;
    // see FStreamRow::NetDelayMs. -1 = unknown.
    float  NetDelayMs    = -1.f;
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

    // Episode boundary: closes the files and reopens them under Dir, aligned with the
    // avatar's episode folder. Safe against concurrent writes from the command
    // thread.
    FString Rotate(const FString& Dir);

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

    FString OpenAt(const FString& Dir);
    void    CloseFiles();
    void    LogEventUnlocked(const FString& Message);

    FCriticalSection                Lock_;
    TUniquePtr<FBufferedFileWriter> StreamWriter_;
    TUniquePtr<FBufferedFileWriter> CommandWriter_;
    IFileHandle*                    EventHandle_ = nullptr;
};

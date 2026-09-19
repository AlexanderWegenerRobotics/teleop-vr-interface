#include "Teleop/CommandThread.h"

#include "Networking/ComLink.h"
#include "Teleop/TeleOpLogger.h"
#include "Shared/AvatarTypes.h"
#include "Engine/Engine.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IXRTrackingSystem.h"
#include "IOpenXRHMD.h"
#include "OpenXRCore.h"

static constexpr float kWorldToMeters = 100.f;
static constexpr int32 kMaxProbeDevices = 16;

// ---------------------------------------------------------------------------
// FArmRetarget
// ---------------------------------------------------------------------------

FVector FOneEuroFilter::Apply(const FVector& Raw, float Dt,
                              float MinCutoff, float Beta, float DerivCutoff)
{
    if (MinCutoff <= 0.f || Dt <= KINDA_SMALL_NUMBER) return Raw;

    if (!bPrimed)
    {
        Value   = Raw;
        Deriv   = FVector::ZeroVector;
        bPrimed = true;
        return Raw;
    }

    auto Alpha = [Dt](float CutoffHz)
    {
        const float Tau = 1.f / (2.f * PI * FMath::Max(CutoffHz, KINDA_SMALL_NUMBER));
        return 1.f / (1.f + Tau / Dt);
    };

    const FVector RawDeriv = (Raw - Value) / Dt;
    const float   DAlpha   = Alpha(DerivCutoff);
    Deriv = DAlpha * RawDeriv + (1.f - DAlpha) * Deriv;

    const float A = Alpha(MinCutoff + Beta * Deriv.Size());
    Value = A * Raw + (1.f - A) * Value;
    return Value;
}

void FArmRetarget::Reset()
{
    ScaledTranslation = FVector::ZeroVector;
    BankedTranslation = FVector::ZeroVector;
    BankedRotation    = FQuat::Identity;
    bPrevValid        = false;
    bOriginValid      = false;
    bWasFullClutch    = true;
    Filter.Reset();
}

FQuat FArmRetarget::GetRotation(const FQuat& CurrentRot) const
{
    if (!bOriginValid) return FQuat::Identity;
    const FQuat Live = OriginRot.Inverse() * CurrentRot;
    return BankedRotation * Live;
}

void FArmRetarget::Advance(const FTransform& Pose, float Dt,
                           const FOperatorInputSnapshot& In, int32 Index)
{
    const bool    bFullClutch = In.bFullClutch[Index];
    const float   ScaleFactor = In.ScaleFactor[Index];
    const FQuat   Rot         = Pose.GetRotation();

    // Filtered before anything else, so the origin, the jump guard and the
    // integration all see one consistent control point. Priming returns the raw
    // value, so re-anchoring on a clutch edge is still seamless.
    const FVector RawControlPoint = Pose.GetLocation() + Rot.RotateVector(In.ControlPointOffset[Index]);
    const FVector ControlPoint = bFullClutch
        ? RawControlPoint
        : Filter.Apply(RawControlPoint, Dt, In.FilterMinCutoff[Index],
                       In.FilterBeta[Index], In.FilterDerivCutoff[Index]);
    if (bFullClutch) Filter.Reset();

    if (!bOriginValid)
    {
        OriginRot        = Rot;
        OriginLoc        = Pose.GetLocation();
        PrevControlPoint = ControlPoint;
        bPrevValid       = true;
        bOriginValid     = true;
        bWasFullClutch   = bFullClutch;
        return;
    }

    // Falling edge: operator let go. Bank what has accumulated so the next
    // segment starts from zero without losing the pose reached so far.
    if (!bWasFullClutch && bFullClutch)
    {
        BankedRotation = (BankedRotation * (OriginRot.Inverse() * Rot));
        BankedRotation.Normalize();
        BankedTranslation += ScaledTranslation;
        ScaledTranslation = FVector::ZeroVector;
    }

    // Rising edge: re-anchor so the first increment of the new segment is zero.
    if (bWasFullClutch && !bFullClutch)
    {
        OriginRot        = Rot;
        OriginLoc        = Pose.GetLocation();
        PrevControlPoint = ControlPoint;
        bPrevValid       = true;
    }

    if (bFullClutch)
    {
        // Held clutched: the origin follows the hand, so releasing produces no jump.
        OriginRot        = Rot;
        OriginLoc        = Pose.GetLocation();
        PrevControlPoint = ControlPoint;
        bPrevValid       = true;
    }
    else if (bPrevValid)
    {
        const FVector Step = ControlPoint - PrevControlPoint;

        // A step implying more than MaxTrackedSpeed is a tracker discontinuity,
        // not an operator. Integrating one verbatim is how a single-frame glitch
        // became a 143 mm command step and faulted the arm on 2026-09-16.
        // Re-seed and skip: the operator loses this sample's motion, which at
        // 90 Hz is 11 ms of it.
        const float GuardDt = FMath::Min(Dt, In.MaxGuardWindow[Index]);
        const float MaxStep = In.MaxTrackedSpeed[Index] * GuardDt;
        if (GuardDt > KINDA_SMALL_NUMBER && In.MaxTrackedSpeed[Index] > 0.f && Step.Size() > MaxStep)
        {
            ++RejectedJumps;
        }
        else
        {
            // Constant gain, so this telescopes to ScaleFactor * (Current - Origin).
            ScaledTranslation += Step * ScaleFactor;
        }
        PrevControlPoint = ControlPoint;
    }

    bWasFullClutch = bFullClutch;
}

// ---------------------------------------------------------------------------
// FTeleopCommandThread
// ---------------------------------------------------------------------------

FTeleopCommandThread::FTeleopCommandThread(UComLink* InComLink, FTeleOpLogger* InLogger, float InRateHz)
    : ComLink_(InComLink), Logger_(InLogger), RateHz_(FMath::Max(1.f, InRateHz))
{
}

FTeleopCommandThread::~FTeleopCommandThread()
{
    StopThread();
}

void FTeleopCommandThread::StartThread()
{
    if (Thread_) return;
    bStop_ = false;
    Thread_ = FRunnableThread::Create(this, TEXT("TeleopCommandThread"), 128 * 1024,
                                      TPri_AboveNormal);
    UE_LOG(LogTemp, Log, TEXT("CommandThread: started at %.0f Hz"), RateHz_);
}

void FTeleopCommandThread::StopThread()
{
    if (!Thread_) return;
    bStop_ = true;
    Thread_->WaitForCompletion();
    delete Thread_;
    Thread_ = nullptr;
    UE_LOG(LogTemp, Log, TEXT("CommandThread: stopped (%llu samples, %llu duplicates)"),
           SampleCount_, DuplicateCount_);
}

void FTeleopCommandThread::Stop() { bStop_ = true; }

void FTeleopCommandThread::PublishInput(const FOperatorInputSnapshot& In)
{
    FScopeLock Lock(&InputMutex_);
    Input_ = In;
}

FCommandThreadOutput FTeleopCommandThread::ReadOutput() const
{
    FScopeLock Lock(&OutputMutex_);
    return Output_;
}

void FTeleopCommandThread::RequestCaptureOrigin() { bCaptureOriginPending_ = true; }

// ---------------------------------------------------------------------------
// OpenXR
// ---------------------------------------------------------------------------

bool FTeleopCommandThread::ResolveOpenXR()
{
    if (bXrResolved_) return Xr_ != nullptr;

    // The thread starts in BeginPlay, which can be well before the OpenXR
    // session is running, so this retries rather than latching a failure.
    const double Now = FPlatformTime::Seconds();
    if (Now - LastResolveAttemptSec_ < 1.0) return false;
    LastResolveAttemptSec_ = Now;

    if (!GEngine || !GEngine->XRSystem.IsValid()) return false;
    Xr_ = GEngine->XRSystem->GetIOpenXRHMD();
    if (!Xr_ || !Xr_->IsRunning())
    {
        Xr_ = nullptr;
        return false;
    }

    // Map hand paths to whatever device ids OpenXRInput assigned.
    const XrInstance Instance = Xr_->GetInstance();
    for (int32 Id = 0; Id < kMaxProbeDevices; ++Id)
    {
        const XrPath Path = Xr_->GetTrackedDevicePath(Id);
        if (Path == XR_NULL_PATH) continue;

        char Buf[XR_MAX_PATH_LENGTH] = {};
        uint32 Written = 0;
        if (XR_FAILED(xrPathToString(Instance, Path, sizeof(Buf), &Written, Buf))) continue;

        const FString S = ANSI_TO_TCHAR(Buf);
        if (S.Contains(TEXT("/user/hand/left")))  HandDeviceId_[0] = Id;
        if (S.Contains(TEXT("/user/hand/right"))) HandDeviceId_[1] = Id;
    }

    if (HandDeviceId_[0] < 0 || HandDeviceId_[1] < 0)
    {
        Xr_ = nullptr;
        return false;
    }

    bXrResolved_ = true;
    UE_LOG(LogTemp, Log, TEXT("CommandThread: OpenXR direct sampling, hand device ids L=%d R=%d"),
           HandDeviceId_[0], HandDeviceId_[1]);
    return true;
}

bool FTeleopCommandThread::LocatePoses(const FOperatorInputSnapshot& In,
                                      FTransform OutPose[2], bool bOutValid[2])
{
    bOutValid[0] = bOutValid[1] = false;
    if (!Xr_) return false;

    const XrSpace Base = reinterpret_cast<XrSpace>(In.XrTrackingSpace);
    if (Base == XR_NULL_HANDLE || In.XrDisplayTimeNs == 0) return false;

    // The cached frame time only advances once per rendered frame. Asking for a
    // pose at that same instant every loop returns the same answer, which is the
    // failure this thread exists to avoid. Anchor it to wall clock and ask for
    // "now"; the runtime predicts forward from its own tracking data.
    const double NowSec = FPlatformTime::Seconds();
    if (In.XrDisplayTimeNs != LastDisplayTime_)
    {
        LastDisplayTime_      = In.XrDisplayTimeNs;
        DisplayTimeAnchorSec_ = NowSec;
    }
    const XrTime SampleTime = static_cast<XrTime>(
        LastDisplayTime_ + static_cast<int64>((NowSec - DisplayTimeAnchorSec_) * 1e9));

    // TRACKED, not merely VALID. A runtime that has lost optical lock keeps
    // reporting VALID while dead-reckoning off the IMU; the game-thread path
    // treats that as Stale and stops integrating, and so does this one.
    const XrSpaceLocationFlags Need =
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT   | XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;

    for (int32 i = 0; i < 2; ++i)
    {
        // Safe off the game thread: FReadScopeLock, no pipelined frame state.
        const XrSpace Hand = Xr_->GetTrackedDeviceSpace(HandDeviceId_[i]);
        if (Hand == XR_NULL_HANDLE) continue;

        XrSpaceLocation Loc{ XR_TYPE_SPACE_LOCATION };
        if (XR_FAILED(xrLocateSpace(Hand, Base, SampleTime, &Loc))) continue;
        if ((Loc.locationFlags & Need) != Need) continue;

        OutPose[i]   = ToFTransform(Loc.pose, kWorldToMeters) * In.TrackingToWorld;
        bOutValid[i] = true;
    }
    return bOutValid[0] || bOutValid[1];
}

// ---------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------

void FTeleopCommandThread::SendArm(uint8 Index, const FOperatorInputSnapshot& In,
                                   const FTransform& Pose, float Dt)
{
    FArmRetarget& R = Retarget_[Index];
    R.Advance(Pose, Dt, In, Index);

    const FVector Translation = R.GetTranslation();
    const FQuat   Rotation    = R.GetRotation(Pose.GetRotation());

    const FVector Local = In.bHMDOriginValid ? In.HMDYawQuat.UnrotateVector(Translation)
                                             : Translation;

    ArmCommandMsg Msg{};
    CoordConvert::UnrealToProtocolFloat(Local, Msg.position[0], Msg.position[1], Msg.position[2]);
    CoordConvert::UnrealToProtocolQuatFloat(Rotation, Msg.quaternion[0], Msg.quaternion[1],
                                            Msg.quaternion[2], Msg.quaternion[3]);
    Msg.gripper = In.bGraspHeld[Index] ? 1.0f : 0.0f;

    const bool bSend = In.bArmActive[Index];
    if (bSend && ComLink_) ComLink_->SendArmCommand(Msg, Index);

    if (Logger_)
    {
        FCommandRow Row;
        Row.TimestampNs  = FTeleOpLogger::NowNs();
        Row.DeviceIndex  = Index;
        Row.Sequence     = Msg.header.sequence;
        Row.bSent        = bSend;
        Row.Px = Msg.position[0];   Row.Py = Msg.position[1];   Row.Pz = Msg.position[2];
        Row.Qw = Msg.quaternion[0]; Row.Qx = Msg.quaternion[1];
        Row.Qy = Msg.quaternion[2]; Row.Qz = Msg.quaternion[3];
        Row.Gripper      = Msg.gripper;
        Row.ClutchFactor = In.bFullClutch[Index] ? 0.f : 1.f;
        Row.bFullClutch  = In.bFullClutch[Index];
        Logger_->WriteCommandRow(Row);
    }

    FScopeLock Lock(&OutputMutex_);
    for (int32 k = 0; k < 3; ++k) Output_.Position[Index][k]   = Msg.position[k];
    for (int32 k = 0; k < 4; ++k) Output_.Quaternion[Index][k] = Msg.quaternion[k];
    Output_.Gripper[Index]      = Msg.gripper;
    Output_.bSent[Index]        = bSend;
    Output_.bFullClutch[Index]  = In.bFullClutch[Index];
    Output_.RejectedJumps[Index] = R.RejectedJumps;
}

uint32 FTeleopCommandThread::Run()
{
    const double Period = 1.0 / RateHz_;
    double Next = FPlatformTime::Seconds() + Period;

    double RateWindowStart = FPlatformTime::Seconds();
    uint32 RateWindowCount = 0;
    double WorstJitter = 0.0;

    double LastLoopSec = FPlatformTime::Seconds();

    while (!bStop_)
    {
        const double LoopSec = FPlatformTime::Seconds();
        // Measured, not nominal: the filter and the jump guard are both
        // functions of the real interval, and a hitch must widen the guard
        // rather than trigger it.
        const float Dt = static_cast<float>(FMath::Clamp(LoopSec - LastLoopSec, 1e-4, 0.5));
        LastLoopSec = LoopSec;

        const bool bXrOk = ResolveOpenXR();

        FOperatorInputSnapshot In;
        {
            FScopeLock Lock(&InputMutex_);
            In = Input_;
        }

        if (bCaptureOriginPending_.Exchange(false))
        {
            Retarget_[0].Reset();
            Retarget_[1].Reset();
        }

        FTransform Pose[2];
        bool bValid[2] = { false, false };
        bool bHavePose = bXrOk && LocatePoses(In, Pose, bValid);

        if (!bHavePose)
        {
            for (int32 i = 0; i < 2; ++i)
            {
                Pose[i]   = In.HandPose[i];
                bValid[i] = In.bHandValid[i];
            }
            bHavePose = bValid[0] || bValid[1];
        }

        if (bHavePose)
        {
            for (int32 i = 0; i < 2; ++i)
            {
                if (!bValid[i]) continue;
                ++SampleCount_;
                if (Pose[i].Equals(LastLocated_[i], 0.f)) ++DuplicateCount_;
                LastLocated_[i] = Pose[i];
                SendArm(static_cast<uint8>(i), In, Pose[i], Dt);
            }
            ++RateWindowCount;
        }

        // --- pacing: sleep to just short of the deadline, then spin ---------
        const double Now = FPlatformTime::Seconds();
        const double Remaining = Next - Now;
        if (Remaining > 0.0)
        {
            if (Remaining > 0.002) FPlatformProcess::SleepNoStats(static_cast<float>(Remaining - 0.001));
            while (FPlatformTime::Seconds() < Next) { FPlatformProcess::SleepNoStats(0.f); }
        }
        else
        {
            WorstJitter = FMath::Max(WorstJitter, -Remaining);
        }

        Next += Period;
        const double After = FPlatformTime::Seconds();
        if (Next < After) Next = After + Period;   // fell behind: resynchronise

        if (After - RateWindowStart >= 1.0)
        {
            FScopeLock Lock(&OutputMutex_);
            Output_.SendRateHz   = static_cast<float>(RateWindowCount / (After - RateWindowStart));
            Output_.LoopJitterMs = static_cast<float>(WorstJitter * 1000.0);
            Output_.DuplicatePct = SampleCount_ > 0
                ? static_cast<float>(100.0 * DuplicateCount_ / SampleCount_) : 0.f;
            Output_.bUsingOpenXRDirect = (Xr_ != nullptr);
            RateWindowStart = After;
            RateWindowCount = 0;
            WorstJitter = 0.0;
        }
    }
    return 0;
}

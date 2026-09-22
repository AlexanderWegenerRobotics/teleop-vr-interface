#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/CriticalSection.h"
#include "Shared/protocol.hpp"

class UComLink;
class FTeleOpLogger;
class IOpenXRHMD;

// Operator input the game thread owns. Enhanced Input only updates on the game
// tick, so clutch and grasp EDGES stay at frame rate no matter how fast this
// thread runs; only the pose integration is decoupled.
struct FOperatorInputSnapshot
{
    bool  bFullClutch[2]  = { true, true };
    bool  bGraspHeld[2]   = { false, false };
    float ScaleFactor[2]  = { 2.f, 2.f };
    bool  bArmActive[2]   = { false, false };   // ENGAGED and reset idle
    FVector ControlPointOffset[2] = { FVector::ZeroVector, FVector::ZeroVector };
    FQuat HMDYawQuat      = FQuat::Identity;
    bool  bHMDOriginValid = false;

    // FOpenXRHMD::GetTrackingSpace() and GetDisplayTime() read pipelined frame
    // state and check(IsInGameThread()), so they cannot be called from the
    // command thread. GetTrackedDeviceSpace() is FReadScopeLock-protected and
    // is safe there, so only these two have to be ferried across.
    uint64 XrTrackingSpace = 0;   // XrSpace handle, opaque here
    int64  XrDisplayTimeNs = 0;   // XrTime of the frame that cached it

    // xrLocateSpace answers in tracking space; the game-thread path works in
    // world space and the HMD yaw correction is a world-space rotation. Without
    // this the two paths disagree by the pawn's own rotation.
    FTransform TrackingToWorld = FTransform::Identity;

    // Fallback pose, from the tracked components. Used when OpenXR direct
    // sampling is unavailable, so a failed probe degrades to today's
    // frame-rate behaviour instead of to no commands at all.
    FTransform HandPose[2];
    bool       bHandValid[2] = { false, false };

    // Mirrored from UTrackedControllerComponent's UPROPERTYs so the editor
    // stays the single place these are tuned.
    float FilterMinCutoff[2]   = { 0.f, 0.f };
    float FilterBeta[2]        = { 0.05f, 0.05f };
    float FilterDerivCutoff[2] = { 1.f, 1.f };
    float MaxTrackedSpeed[2]   = { 150.f, 150.f };
    float MaxGuardWindow[2]    = { 0.1f, 0.1f };
};

// One-euro filter (Casiez et al.), ported from UTrackedControllerComponent so
// the OpenXR fast path filters the same way the game-thread path does. The
// cutoff rises with speed: heavy smoothing at rest, little added lag while the
// hand is actually moving. MinCutoff <= 0 disables it.
struct FOneEuroFilter
{
    bool    bPrimed = false;
    FVector Value   = FVector::ZeroVector;
    FVector Deriv   = FVector::ZeroVector;

    void    Reset() { bPrimed = false; }
    FVector Apply(const FVector& Raw, float Dt, float MinCutoff, float Beta, float DerivCutoff);
};

// What the thread produces, for the ghost overlay, HUD and stats.
struct FCommandThreadOutput
{
    float  Position[2][3]   = {};
    float  Quaternion[2][4] = { {1.f,0.f,0.f,0.f}, {1.f,0.f,0.f,0.f} };
    float  Gripper[2]       = {};
    bool   bSent[2]         = {};
    bool   bFullClutch[2]   = { true, true };

    float  SendRateHz    = 0.f;
    float  LoopJitterMs  = 0.f;
    // Fraction of consecutive located poses that were bit-identical. The whole
    // point of the thread is that this stays near zero: a high value means we
    // are re-reading one cached pose instead of resampling the runtime.
    float  DuplicatePct  = 0.f;
    bool   bUsingOpenXRDirect = false;
    // Cumulative tracker discontinuities discarded by the jump guard, per arm.
    int32  RejectedJumps[2] = { 0, 0 };
};

// Per-arm delta-from-origin retarget state. Plain data owned by the thread —
// no UObject, no engine tick.
struct FArmRetarget
{
    bool    bOriginValid = false;
    FQuat   OriginRot    = FQuat::Identity;
    FVector OriginLoc    = FVector::ZeroVector;

    FVector PrevControlPoint = FVector::ZeroVector;
    bool    bPrevValid       = false;

    FVector ScaledTranslation = FVector::ZeroVector;
    FVector BankedTranslation = FVector::ZeroVector;
    FQuat   BankedRotation    = FQuat::Identity;

    bool    bWasFullClutch = true;

    FOneEuroFilter Filter;
    int32   RejectedJumps = 0;

    void Reset();
    // Mirrors UpdateClutch + UpdateScaledTranslation + GetDeltaPose, with the
    // clutch supplied as a level; the edge is detected here so banking is
    // always atomic with integration.
    void Advance(const FTransform& Pose, float Dt, const FOperatorInputSnapshot& In, int32 Index);
    FVector GetTranslation() const { return BankedTranslation + ScaledTranslation; }
    FQuat   GetRotation(const FQuat& CurrentRot) const;
};

class TELEOP_VR_INTERFACE_API FTeleopCommandThread : public FRunnable
{
public:
    FTeleopCommandThread(UComLink* InComLink, FTeleOpLogger* InLogger, float InRateHz);
    virtual ~FTeleopCommandThread();

    void StartThread();
    void StopThread();

    void PublishInput(const FOperatorInputSnapshot& In);
    FCommandThreadOutput ReadOutput() const;

    // Posted by the game thread, applied by the loop at a tick boundary so a
    // re-anchor can never land mid-integration.
    //
    // ArmIndex -1 re-anchors both arms (engage); 0 or 1 re-anchors that arm
    // alone. Per-arm matters after a single-arm reset: zeroing the other arm
    // there would make IT jump instead.
    void RequestCaptureOrigin(int32 ArmIndex = -1);

    virtual uint32 Run() override;
    virtual void   Stop() override;

private:
    bool ResolveOpenXR();
    bool LocatePoses(const FOperatorInputSnapshot& In, FTransform OutPose[2], bool bOutValid[2]);
    void SendArm(uint8 Index, const FOperatorInputSnapshot& In, const FTransform& Pose, float Dt);

    UComLink*      ComLink_ = nullptr;
    FTeleOpLogger* Logger_  = nullptr;
    float          RateHz_  = 90.f;

    FRunnableThread* Thread_ = nullptr;
    TAtomic<bool>    bStop_{false};
    // Bit 0 = left, bit 1 = right. A bitmask rather than a bool so two
    // requests landing in the same tick cannot cancel each other.
    TAtomic<uint8>   CaptureOriginPending_{0};

    mutable FCriticalSection InputMutex_;
    FOperatorInputSnapshot   Input_;

    mutable FCriticalSection OutputMutex_;
    FCommandThreadOutput     Output_;

    FArmRetarget Retarget_[2];

    // --- OpenXR direct sampling ------------------------------------------
    IOpenXRHMD* Xr_ = nullptr;
    int32       HandDeviceId_[2] = { -1, -1 };
    bool        bXrResolved_ = false;
    double      LastResolveAttemptSec_ = 0.0;

    // Mapping from the frame's predicted display time to wall clock, so we can
    // ask for a pose at "now" between frames instead of re-requesting the same
    // instant and getting the same answer back.
    int64  LastDisplayTime_ = 0;
    double DisplayTimeAnchorSec_ = 0.0;

    FTransform LastLocated_[2];
    uint64 SampleCount_ = 0;
    uint64 DuplicateCount_ = 0;
};

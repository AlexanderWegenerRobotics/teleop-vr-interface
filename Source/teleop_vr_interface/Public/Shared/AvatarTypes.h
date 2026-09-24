#pragma once

#include "CoreMinimal.h"
#include "Shared/protocol.hpp"
#include "AvatarTypes.generated.h"

UENUM(BlueprintType)
enum class ESysState : uint8 {
    Offline  = 0,
    Idle     = 1,
    Homing   = 2,
    Awaiting = 3,
    Engaged  = 4,
    Paused   = 5,
    Fault      = 6,
    Stop       = 7,
    Recovering = 8
};

// Which command channel may move one arm. Mirrors CommandAuthority in
// teleop-simulator/include/common.hpp -- the values have to agree, because the
// avatar reads this straight off the wire as a uint8.
//
// Named for the mechanism rather than for DAgger: the same mutex is what stops
// the VR channel and the orchestrator's absolute channel from fighting, which
// they do today whenever both are live, DAgger or no DAgger.
//
// Unset means no one has claimed the arm and the avatar is gating nothing --
// exactly the behaviour that existed before authority. It is a real state the
// avatar publishes, not a UI placeholder.
UENUM(BlueprintType)
enum class EControlAuthority : uint8 {
    Policy = 0,
    Human  = 1,
    Hold   = 2,
    Unset  = 255
};

inline FString AuthorityToString(EControlAuthority A) {
    switch (A) {
    case EControlAuthority::Policy: return TEXT("POLICY");
    case EControlAuthority::Human:  return TEXT("HUMAN");
    case EControlAuthority::Hold:   return TEXT("HOLD");
    default:                        return TEXT("UNSET");
    }
}

// One character per state, for the fixed-width readout used when the two arms
// disagree: "L= RH" is left HOLD, right HUMAN.
//
// '=' rather than a letter for HOLD -- the two bars read as a pause, and every
// letter that fits is taken or ambiguous ('H' is HUMAN, and 'O' for hOld next
// to it was genuinely unreadable on the headset). '?' for Unset says "nobody
// has claimed this arm", which is a question rather than a state.
inline FString AuthorityToInitial(EControlAuthority A) {
    switch (A) {
    case EControlAuthority::Policy: return TEXT("P");
    case EControlAuthority::Human:  return TEXT("H");
    case EControlAuthority::Hold:   return TEXT("=");
    default:                        return TEXT("?");
    }
}

inline ESysState ToESysState(SysState S) {
    return static_cast<ESysState>(S);
}

inline SysState ToSysState(ESysState S) {
    return static_cast<SysState>(S);
}

inline FString StateToString(ESysState S) {
    switch (S) {
    case ESysState::Offline:  return TEXT("OFFLINE");
    case ESysState::Idle:     return TEXT("IDLE");
    case ESysState::Homing:   return TEXT("HOMING");
    case ESysState::Awaiting: return TEXT("AWAITING");
    case ESysState::Engaged:  return TEXT("ENGAGED");
    case ESysState::Paused:   return TEXT("PAUSED");
    case ESysState::Fault:      return TEXT("FAULT");
    case ESysState::Stop:       return TEXT("STOP");
    case ESysState::Recovering: return TEXT("RECOVERING");
    default:                    return TEXT("UNKNOWN");
    }
}

inline FString FaultToString(FaultCode F) {
    switch (F) {
    case FaultCode::NONE:                return TEXT("NONE");
    case FaultCode::JOINT_LIMIT:         return TEXT("JOINT_LIMIT");
    case FaultCode::JOINT_LOCKED:        return TEXT("JOINT_LOCKED");
    case FaultCode::HIGH_EXTERNAL_FORCE: return TEXT("HIGH_EXT_FORCE");
    case FaultCode::VELOCITY_LIMIT:      return TEXT("VELOCITY_LIMIT");
    case FaultCode::IMPLAUSIBLE_COMMAND: return TEXT("IMPLAUSIBLE_CMD");
    case FaultCode::COMM_LOSS:           return TEXT("COMM_LOSS");
    case FaultCode::INTERNAL_ERROR:      return TEXT("INTERNAL_ERROR");
    case FaultCode::HMD_NOT_WORN:        return TEXT("HMD_NOT_WORN");
    case FaultCode::COLLISION_RISK:      return TEXT("COLLISION_RISK");
    case FaultCode::WORKSPACE_LIMIT:     return TEXT("WORKSPACE_LIMIT");
    default:                             return TEXT("UNKNOWN");
    }
}

USTRUCT(BlueprintType)
struct FTrackedPose {
    GENERATED_BODY()

    UPROPERTY() FVector Position = FVector::ZeroVector;
    UPROPERTY() FRotator Orientation = FRotator::ZeroRotator;
    UPROPERTY() bool bIsValid = false;
    UPROPERTY() double Timestamp = 0.0;
};

namespace CoordConvert {
    inline void UnrealToProtocolFloat(const FVector& In, float& OutX, float& OutY, float& OutZ) {
        OutX = static_cast<float>(In.X / 100.0);
        OutY = -static_cast<float>(In.Y / 100.0);
        OutZ = static_cast<float>(In.Z / 100.0);
    }

    // Forward path: UE (X-fwd, Y-right, Z-up)  →  protocol (X-fwd, Y-left, Z-up).
    // The Y-axis flip is a similarity transform R_proto = R_flip * R_UE * R_flip
    // where R_flip = diag(1,-1,1).  For a quaternion this negates the X and Z
    // imaginary components (Y is unchanged because its axis is the flip axis).
    // This is the exact inverse of ProtocolToUnrealQuat below.
    inline void UnrealToProtocolQuatFloat(const FQuat& Q, float& Q0, float& Q1, float& Q2, float& Q3) {
        Q0 =  static_cast<float>(Q.W);   // w — unchanged
        Q1 = -static_cast<float>(Q.X);   // x — negated  (roll axis)
        Q2 =  static_cast<float>(Q.Y);   // y — unchanged (pitch axis, same in both frames)
        Q3 = -static_cast<float>(Q.Z);   // z — negated  (yaw axis)
    }

    inline FVector ProtocolToUnreal(float X, float Y, float Z) {
        return FVector(X * 100.0, -Y * 100.0, Z * 100.0);
    }

    // Backward path only — do NOT use for sending commands.
    // Converts a rotation quaternion from protocol convention (X-fwd, Y-left, Z-up)
    // to UE convention (X-fwd, Y-right, Z-up) via the similarity transform
    // R_UE = R_flip * R_proto * R_flip  where R_flip = diag(1,-1,1).
    // Negates the X and Z imaginary components (not Y).
    // Protocol convention: Q0=w, Q1=x, Q2=y, Q3=z.
    inline FQuat ProtocolToUnrealQuat(float Q0, float Q1, float Q2, float Q3) {
        return FQuat(-Q1, Q2, -Q3, Q0);  // FQuat(x, y, z, w): negate x and z
    }

    // Legacy overload kept for existing call sites; now consistent with ProtocolToUnreal.
    inline FRotator ProtocolToUnrealRot(float Q0, float Q1, float Q2, float Q3) {
        return ProtocolToUnrealQuat(Q0, Q1, Q2, Q3).Rotator();
    }
}
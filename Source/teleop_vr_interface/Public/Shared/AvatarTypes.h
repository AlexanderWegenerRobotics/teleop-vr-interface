#pragma once

#include "CoreMinimal.h"
#include "AvatarTypes.generated.h"

// ---------------------------------------------------------------------------
// ESysState — mirrors SysState enum in common.hpp exactly.
// Values must not change without matching the avatar side.
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class ESysState : uint8
{
    Offline = 0,
    Idle = 1,
    Homing = 2,
    Awaiting = 3,
    Engaged = 4,
    Paused = 5,
    Fault = 6,
    Stop = 7
};

inline FString StateToString(ESysState S) {
    switch (S) {
    case ESysState::Offline:  return TEXT("OFFLINE");
    case ESysState::Idle:     return TEXT("IDLE");
    case ESysState::Homing:   return TEXT("HOMING");
    case ESysState::Awaiting: return TEXT("AWAITING");
    case ESysState::Engaged:  return TEXT("ENGAGED");
    case ESysState::Paused:   return TEXT("PAUSED");
    case ESysState::Fault:    return TEXT("FAULT");
    case ESysState::Stop:     return TEXT("STOP");
    default:                  return TEXT("UNKNOWN");
    }
}

// ---------------------------------------------------------------------------
// Wire structs — binary layout must match common.hpp #pragma pack(push,1).
// Never add UE reflection macros inside these structs.
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

struct FAvatarStateMsg
{
    uint8    SystemState;
    uint64   TimestampNs;
};

struct FAvatarCommandMsg
{
    uint8    RequestedState;
    uint8    SessionId;
    uint64   TimestampNs;
};

struct FArmStateMsg
{
    uint8    DeviceId;
    uint8    State;
    float    Position[3];
    float    Quaternion[4];
    float    TauExt[7];
    uint64   TimestampNs;
};

struct FArmCommandMsg
{
    uint8    DeviceId;
    uint8    State;
    float    Position[3];
    float    Quaternion[4];
    float    Gripper;
    uint64   TimestampNs;
};

struct FHeadStateMsg
{
    uint8    DeviceId;
    uint8    State;
    float    Pan;
    float    Tilt;
    uint64   TimestampNs;
};

struct FHeadCommandMsg
{
    uint8    DeviceId;
    uint8    State;
    float    Pan;
    float    Tilt;
    uint64   TimestampNs;
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Size assertions — catch layout drift at compile time.
// ---------------------------------------------------------------------------

static_assert(sizeof(FAvatarStateMsg) == 9, "FAvatarStateMsg size mismatch");
static_assert(sizeof(FAvatarCommandMsg) == 10, "FAvatarCommandMsg size mismatch");
static_assert(sizeof(FArmStateMsg) == 66, "FArmStateMsg size mismatch");
static_assert(sizeof(FArmCommandMsg) == 42, "FArmCommandMsg size mismatch");
static_assert(sizeof(FHeadStateMsg) == 18, "FHeadStateMsg size mismatch");
static_assert(sizeof(FHeadCommandMsg) == 18, "FHeadCommandMsg size mismatch");

// ---------------------------------------------------------------------------
// FTrackedPose — internal Unreal representation (cm, left-handed).
// Used by PoseMapper to hold controller and head poses.
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FTrackedPose
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Position = FVector::ZeroVector;

    UPROPERTY()
    FRotator Orientation = FRotator::ZeroRotator;

    UPROPERTY()
    bool bIsValid = false;

    UPROPERTY()
    double Timestamp = 0.0;
};

// ---------------------------------------------------------------------------
// CoordConvert
// Protocol convention: meters, right-handed Z-up (MuJoCo)
// Unreal convention:   centimeters, left-handed Z-up
//
//   Unreal->Protocol:  X_p =  X_ue/100,  Y_p = -Y_ue/100,  Z_p = Z_ue/100
//   Protocol->Unreal:  X_ue = X_p*100,   Y_ue = -Y_p*100,  Z_ue = Z_p*100
//   Quaternion Y negates due to handedness flip.
// ---------------------------------------------------------------------------

namespace CoordConvert
{
    // Double variants — general use
    inline void UnrealToProtocol(const FVector& In,
        double& OutX, double& OutY, double& OutZ)
    {
        OutX = In.X / 100.0;
        OutY = -In.Y / 100.0;
        OutZ = In.Z / 100.0;
    }

    inline void UnrealToProtocolQuat(const FRotator& In,
        double& QX, double& QY, double& QZ, double& QW)
    {
        FQuat Q = In.Quaternion();
        QX = Q.X;
        QY = -Q.Y;
        QZ = Q.Z;
        QW = Q.W;
    }

    inline FVector ProtocolToUnreal(double X, double Y, double Z)
    {
        return FVector(X * 100.0, -Y * 100.0, Z * 100.0);
    }

    inline FRotator ProtocolToUnrealRot(double QX, double QY, double QZ, double QW)
    {
        FQuat Q(QX, -QY, QZ, QW);
        return Q.Rotator();
    }

    // Float variants — for wire struct fields (FArmCommandMsg, FHeadCommandMsg)
    inline void UnrealToProtocolFloat(const FVector& In,
        float& OutX, float& OutY, float& OutZ)
    {
        OutX = static_cast<float>(In.X / 100.0);
        OutY = -static_cast<float>(In.Y / 100.0);
        OutZ = static_cast<float>(In.Z / 100.0);
    }

    inline void UnrealToProtocolQuatFloat(const FRotator& In,
        float& QX, float& QY, float& QZ, float& QW)
    {
        FQuat Q = In.Quaternion();
        QX = static_cast<float>(Q.X);
        QY = -static_cast<float>(Q.Y);
        QZ = static_cast<float>(Q.Z);
        QW = static_cast<float>(Q.W);
    }
}
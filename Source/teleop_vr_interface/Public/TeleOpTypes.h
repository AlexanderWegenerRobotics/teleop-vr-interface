#pragma once

#include "CoreMinimal.h"
#include <msgpack.hpp>
#include <vector>
#include "TeleOpTypes.generated.h"

// ============================================================================
// Message type identifiers
// 0x0_ : Operator -> Simulator
// 0x1_ : Simulator -> Operator
// 0x2_ : Configuration (bidirectional, TCP)
// ============================================================================
enum class EMsgType : uint8
{
	// Operator -> Simulator
	HeadPose = 0x01,
	HandLeft = 0x02,
	HandRight = 0x03,
	ModeCommand = 0x04,

	// Simulator -> Operator
	RobotStateRight = 0x10,
	RobotStateLeft = 0x11,
	PanTiltState = 0x12,
	SystemStatus = 0x13,

	// Configuration (future TCP)
	ConfigUpdate = 0x20,
};

// ============================================================================
// System modes
// ============================================================================
enum class EOpMode : uint8
{
	Offline = 0,
	Idle = 1,
	Engaged = 2,
	Paused = 3,
	Stopped = 4,
	Error = 5,
};

// ============================================================================
// Robot status flags (bitfield)
// ============================================================================
namespace RobotStatus
{
	constexpr uint8 OK = 0x00;
	constexpr uint8 Collision = 0x01;
	constexpr uint8 JointLimit = 0x02;
	constexpr uint8 Singularity = 0x04;
	constexpr uint8 EStop = 0x08;
}

// ============================================================================
// Tracked pose — used internally in Unreal (Unreal conventions: cm, left-handed)
// ============================================================================
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

// ============================================================================
// Wire-format structs (protocol convention: meters, right-handed, Z-up, quaternions)
// These represent exactly what goes on the wire.
// ============================================================================

// --- Outgoing: Operator -> Simulator ---

struct FWirePose
{
	uint64	Timestamp = 0;
	uint32	Sequence = 0;
	uint8	Type = 0;
	double	PX = 0, PY = 0, PZ = 0;
	double	QX = 0, QY = 0, QZ = 0, QW = 1.0;
	double	TriggerValue = 0.0;		// only used for hand messages

	// Pack as flat msgpack array: [ts, seq, type, px, py, pz, qx, qy, qz, qw]
	// For hand messages appends trigger: [ts, seq, type, px, py, pz, qx, qy, qz, qw, trigger]
	msgpack::sbuffer Pack() const
	{
		msgpack::sbuffer buf;
		msgpack::packer<msgpack::sbuffer> pk(&buf);

		bool bIsHand = (Type == static_cast<uint8>(EMsgType::HandLeft) ||
			Type == static_cast<uint8>(EMsgType::HandRight));
		pk.pack_array(bIsHand ? 11 : 10);
		pk.pack(Timestamp);
		pk.pack(Sequence);
		pk.pack(Type);
		pk.pack(PX); pk.pack(PY); pk.pack(PZ);
		pk.pack(QX); pk.pack(QY); pk.pack(QZ); pk.pack(QW);
		if (bIsHand) pk.pack(TriggerValue);

		return buf;
	}
};

struct FWireModeCommand
{
	uint64	Timestamp = 0;
	uint32	Sequence = 0;
	uint8	Mode = 0;

	msgpack::sbuffer Pack() const
	{
		msgpack::sbuffer buf;
		msgpack::packer<msgpack::sbuffer> pk(&buf);

		pk.pack_array(4);
		pk.pack(Timestamp);
		pk.pack(Sequence);
		pk.pack(static_cast<uint8>(EMsgType::ModeCommand));
		pk.pack(Mode);

		return buf;
	}
};

// --- Incoming: Simulator -> Operator ---

struct FWireRobotState
{
	uint64	Timestamp = 0;
	uint32	Sequence = 0;
	uint8	Type = 0;
	double	JointPositions[7] = {};
	double	EE_PX = 0, EE_PY = 0, EE_PZ = 0;
	double	EE_QX = 0, EE_QY = 0, EE_QZ = 0, EE_QW = 1.0;
	double	GripperWidth = 0.0;
	uint8	StatusFlags = 0;

	// Unpack from flat msgpack array
	bool Unpack(const msgpack::object& obj)
	{
		if (obj.type != msgpack::type::ARRAY || obj.via.array.size < 19) return false;
		auto* a = obj.via.array.ptr;

		Timestamp = a[0].as<uint64>();
		Sequence = a[1].as<uint32>();
		Type = a[2].as<uint8>();
		for (int i = 0; i < 7; ++i) JointPositions[i] = a[3 + i].as<double>();
		EE_PX = a[10].as<double>(); EE_PY = a[11].as<double>(); EE_PZ = a[12].as<double>();
		EE_QX = a[13].as<double>(); EE_QY = a[14].as<double>();
		EE_QZ = a[15].as<double>(); EE_QW = a[16].as<double>();
		GripperWidth = a[17].as<double>();
		StatusFlags = a[18].as<uint8>();
		return true;
	}
};

struct FWirePanTiltState
{
	uint64	Timestamp = 0;
	uint32	Sequence = 0;
	double	Pan = 0.0;
	double	Tilt = 0.0;

	bool Unpack(const msgpack::object& obj)
	{
		if (obj.type != msgpack::type::ARRAY || obj.via.array.size < 5) return false;
		auto* a = obj.via.array.ptr;

		Timestamp = a[0].as<uint64>();
		Sequence = a[1].as<uint32>();
		// a[2] is type, skip
		Pan = a[3].as<double>();
		Tilt = a[4].as<double>();
		return true;
	}
};

struct FWireSystemStatus
{
	uint64	Timestamp = 0;
	uint32	Sequence = 0;
	uint8	SimState = 0;
	double	SimFps = 0.0;
	uint8	ErrorCode = 0;

	bool Unpack(const msgpack::object& obj)
	{
		if (obj.type != msgpack::type::ARRAY || obj.via.array.size < 6) return false;
		auto* a = obj.via.array.ptr;

		Timestamp = a[0].as<uint64>();
		Sequence = a[1].as<uint32>();
		// a[2] is type, skip
		SimState = a[3].as<uint8>();
		SimFps = a[4].as<double>();
		ErrorCode = a[5].as<uint8>();
		return true;
	}
};

// ============================================================================
// Coordinate conversion utilities
// Protocol: meters, right-handed Z-up (robotics / MuJoCo convention)
// Unreal:   centimeters, left-handed Z-up
//
// Conversion:
//   Unreal -> Protocol:  X_proto =  X_ue / 100,  Y_proto = -Y_ue / 100,  Z_proto = Z_ue / 100
//   Protocol -> Unreal:  X_ue = X_proto * 100,    Y_ue = -Y_proto * 100,  Z_ue = Z_proto * 100
//
// Quaternion Y component also negates due to handedness flip.
// ============================================================================

namespace CoordConvert
{
	// Unreal FVector (cm, left-hand) -> Protocol (m, right-hand)
	inline void UnrealToProtocol(const FVector& In, double& OutX, double& OutY, double& OutZ)
	{
		OutX = In.X / 100.0;
		OutY = -In.Y / 100.0;
		OutZ = In.Z / 100.0;
	}

	// Unreal FRotator -> Protocol quaternion (right-hand)
	inline void UnrealToProtocolQuat(const FRotator& In, double& QX, double& QY, double& QZ, double& QW)
	{
		FQuat Q = In.Quaternion();
		QX = Q.X;
		QY = -Q.Y;
		QZ = Q.Z;
		QW = Q.W;
	}

	// Protocol (m, right-hand) -> Unreal FVector (cm, left-hand)
	inline FVector ProtocolToUnreal(double X, double Y, double Z)
	{
		return FVector(X * 100.0, -Y * 100.0, Z * 100.0);
	}

	// Protocol quaternion (right-hand) -> Unreal FRotator
	inline FRotator ProtocolToUnrealRot(double QX, double QY, double QZ, double QW)
	{
		FQuat Q(QX, -QY, QZ, QW);
		return Q.Rotator();
	}
}
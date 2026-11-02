#pragma once

#include <cstdint>
#include <chrono>

enum class SysState : uint8_t {
    OFFLINE     = 0,
    IDLE        = 1,
    HOMING      = 2,
    AWAITING    = 3,
    ENGAGED     = 4,
    PAUSED      = 5,
    FAULT       = 6,
    STOP        = 7,
    RECOVERING  = 8,
    UNDEFINED   = 255
};

enum class FaultCode : uint8_t {
    NONE                = 0,
    JOINT_LIMIT         = 1,
    JOINT_LOCKED        = 2,
    HIGH_EXTERNAL_FORCE = 3,
    VELOCITY_LIMIT      = 4,
    IMPLAUSIBLE_COMMAND = 5,
    COMM_LOSS           = 6,
    INTERNAL_ERROR      = 7,
    HMD_NOT_WORN        = 8,
    COLLISION_RISK      = 9,
    WORKSPACE_LIMIT     = 10
};

enum class DeviceId : uint8_t {
    LEFT_ARM  = 1,
    RIGHT_ARM = 2,
    HEAD      = 3,
    AVATAR    = 4
};

enum class GraspState : uint8_t {
    OPEN = 0,
    HELD = 1,
    LOST = 2
};

inline uint64_t timestamp_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

#pragma pack(push, 1)

struct MsgHeader {
    uint32_t  sequence;
    uint64_t  timestamp_ns;
    // Wall-clock time the data was sampled, as opposed to timestamp_ns (send time).
    // The 200 Hz state thread resends the last pose with a fresh timestamp even if
    // the 1 kHz control thread is dead, so compute staleness from this field. 0 =
    // unknown.
    // Must stay byte-identical to MsgHeader in the avatar repo's include/common.hpp:
    // raw memcpy wire format, no versioning.
    uint64_t  sample_time_ns;
    SysState  state;
    FaultCode fault_code;
    DeviceId  device_id;
};

struct ArmCommandMsg {
    MsgHeader header;
    float     position[3];
    float     quaternion[4];
    float     gripper;
    // Operator clutch, 1 = clutched: the hand is decoupled from the setpoint and the
    // operator is repositioning, not demonstrating. On the wire so the avatar can log
    // it with the episode. Senders that never clutch (playback, policy) send 0.
    uint8_t   clutch;
};

struct ArmStateMsg {
    MsgHeader header;
    float     position[3];
    float     quaternion[4];
    float     joint_positions[7];
    float     tau_ext[7];
    uint8_t     recovering;
    float       gripper_width;
    GraspState  grasp_state;
    // Sequence number of the command this arm consumed, echoed for single-clock
    // round-trip time. 0 = unknown.
    uint32_t  applied_cmd_sequence;
    // Network-delay echo (see common.hpp):
    // echo_cmd_sequence: newest command sequence the avatar received. echo_hold_us:
    // avatar time from that command's arrival to this send. One-way delay =
    // ((recv_now - send_time(echo)) - hold) / 2. 0 / 0 = unknown.
    uint32_t  echo_cmd_sequence;
    uint32_t  echo_hold_us;
    // Estimated external wrench at the EE, base frame (fx fy fz tx ty tz).
    float     F_ext[6];
};

struct HeadCommandMsg {
    MsgHeader header;
    float     pan;
    float     tilt;
};

struct HeadStateMsg {
    MsgHeader header;
    float     pan;
    float     tilt;
};

#pragma pack(pop)

// Wire contract: these sizes must equal the avatar repo's include/common.hpp and
// teleop_orchestrator/live/wire.py. A mismatch drops packets silently on size.
static_assert(sizeof(MsgHeader)      == 23,  "MsgHeader size mismatch");
static_assert(sizeof(ArmCommandMsg)  == 56,  "ArmCommandMsg size mismatch");
static_assert(sizeof(ArmStateMsg)    == 149, "ArmStateMsg size mismatch");
static_assert(sizeof(HeadCommandMsg) == 31,  "HeadCommandMsg size mismatch");
static_assert(sizeof(HeadStateMsg)   == 31,  "HeadStateMsg size mismatch");

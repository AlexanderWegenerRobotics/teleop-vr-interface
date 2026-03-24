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
    E_STOP      = 8,
    CLUTCH      = 9,
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

inline uint64_t timestamp_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

#pragma pack(push, 1)

struct MsgHeader {
    uint32_t  sequence;
    uint64_t  timestamp_ns;
    SysState  state;
    FaultCode fault_code;
    DeviceId  device_id;
};

struct ArmCommandMsg {
    MsgHeader header;
    float     position[3];
    float     quaternion[4];
    float     gripper;
};

struct ArmStateMsg {
    MsgHeader header;
    float     position[3];
    float     quaternion[4];
    float     joint_positions[7];
    float     tau_ext[7];
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

static_assert(sizeof(MsgHeader)      == 15, "MsgHeader size mismatch");
static_assert(sizeof(ArmCommandMsg)  == 47, "ArmCommandMsg size mismatch");
static_assert(sizeof(ArmStateMsg)    == 99, "ArmStateMsg size mismatch");
static_assert(sizeof(HeadCommandMsg) == 23, "HeadCommandMsg size mismatch");
static_assert(sizeof(HeadStateMsg)   == 23, "HeadStateMsg size mismatch");

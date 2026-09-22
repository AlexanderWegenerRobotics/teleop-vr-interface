#pragma once

#include <cstdint>
#include <string>
#include <msgpack.hpp>
// See CommandLink.h: msgpack drags in <windows.h> via <winsock2.h> on MSVC and
// leaves its A/W function macros behind for the whole translation unit.
#include "Shared/WindowsMacroCleanup.h"

struct AnnotationMsg {
    double      timestamp  = 0.0;
    std::string label;
    uint8_t     atype      = 0;
    float       confidence = 0.0f;
    float       score      = 0.0f;
    uint64_t    frame_id   = 0;
    MSGPACK_DEFINE_MAP(timestamp, label, atype, confidence, score, frame_id)
};

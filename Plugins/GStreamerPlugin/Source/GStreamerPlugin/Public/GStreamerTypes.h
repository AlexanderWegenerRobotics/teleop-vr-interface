#pragma once

#include "CoreMinimal.h"

// Forward declarations only - no GStreamer includes in headers
struct _GstElement;
struct _GstPipeline;
struct _GstBus;
struct _GstSample;
struct _GstBuffer;
struct _GstCaps;

namespace GStreamerWrapper
{
    using GstElementPtr = _GstElement*;
    using GstPipelinePtr = _GstPipeline*;
    using GstBusPtr = _GstBus*;
    using GstSamplePtr = _GstSample*;
    using GstBufferPtr = _GstBuffer*;
    using GstCapsPtr = _GstCaps*;
}
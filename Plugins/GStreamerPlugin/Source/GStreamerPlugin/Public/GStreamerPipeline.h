#pragma once

#include "CoreMinimal.h"

// Forward declarations for GStreamer functions
extern "C" void* GStreamerCreatePipeline(const char* description);
extern "C" bool GStreamerStartPipeline(void* pipeline);
extern "C" void GStreamerStopPipeline(void* pipeline);
extern "C" void GStreamerDestroyPipeline(void* pipeline);

class GSTREAMERPLUGIN_API FGStreamerPipeline
{
public:
    FGStreamerPipeline();
    ~FGStreamerPipeline();
    
    bool Create(const FString& PipelineDescription);
    bool Start();
    void Stop();
    
private:
    void* Pipeline;
};
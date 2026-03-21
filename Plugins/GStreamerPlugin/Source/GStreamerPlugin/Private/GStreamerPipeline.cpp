#include "GStreamerPipeline.h"
#include <string>

FGStreamerPipeline::FGStreamerPipeline()
    : Pipeline(nullptr)
{
}

FGStreamerPipeline::~FGStreamerPipeline()
{
    if (Pipeline)
    {
        GStreamerDestroyPipeline(Pipeline);
    }
}

bool FGStreamerPipeline::Create(const FString& PipelineDescription)
{
    // Convert FString to std::string to const char*
    FTCHARToUTF8 Converter(*PipelineDescription);
    const char* desc = Converter.Get();
    
    Pipeline = GStreamerCreatePipeline(desc);
    return (Pipeline != nullptr);
}

bool FGStreamerPipeline::Start()
{
    return GStreamerStartPipeline(Pipeline);
}

void FGStreamerPipeline::Stop()
{
    GStreamerStopPipeline(Pipeline);
}
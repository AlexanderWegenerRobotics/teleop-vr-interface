#pragma once

#include "CoreMinimal.h"
#include "GStreamerTypes.h"

class GSTREAMERPLUGIN_API FGStreamerMediaPlayer
{
public:
    FGStreamerMediaPlayer();
    ~FGStreamerMediaPlayer();
    
    bool Initialize();
    void Shutdown();
    
private:
    // Use forward declared types
    GStreamerWrapper::GstPipelinePtr Pipeline;
    bool bIsInitialized;
};
// This file deliberately does NOT include GStreamer headers
// to avoid the GError conflict

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGStreamerPlugin, Log, All);

// Forward declare the init/deinit functions
extern "C" void GStreamerInit();
extern "C" void GStreamerDeinit();

class FGStreamerPluginModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UE_LOG(LogGStreamerPlugin, Log, TEXT("GStreamer Plugin: Initializing"));
        GStreamerInit();
        UE_LOG(LogGStreamerPlugin, Log, TEXT("GStreamer Plugin: Ready"));
    }

    virtual void ShutdownModule() override
    {
        GStreamerDeinit();
        UE_LOG(LogGStreamerPlugin, Log, TEXT("GStreamer Plugin: Shutdown"));
    }
};

IMPLEMENT_MODULE(FGStreamerPluginModule, GStreamerPlugin)
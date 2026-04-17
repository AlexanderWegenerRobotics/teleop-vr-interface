#include "Modules/ModuleManager.h"
#include "VideoEncoderFactory.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

class FVideoEncoderModuleImpl : public IModuleInterface
{
public:
    virtual void StartupModule() override{
        AVEncoder::FVideoEncoderFactory::Get();
    }
};

PRAGMA_ENABLE_DEPRECATION_WARNINGS

IMPLEMENT_MODULE(FVideoEncoderModuleImpl, VideoEncoderModule);
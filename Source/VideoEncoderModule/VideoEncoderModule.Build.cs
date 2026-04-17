using UnrealBuildTool;

public class VideoEncoderModule : ModuleRules
{
    public VideoEncoderModule(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        bEnableExceptions = true;
        bUseUnity = false;
        bUsePrecompiled = false;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "AVEncoder",
        });
    }
}
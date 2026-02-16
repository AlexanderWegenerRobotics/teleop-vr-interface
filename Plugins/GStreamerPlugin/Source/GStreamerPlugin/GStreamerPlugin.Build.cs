using UnrealBuildTool;
using System.IO;

public class GStreamerPlugin : ModuleRules
{
    public GStreamerPlugin(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;
        bEnableUndefinedIdentifierWarnings = false;
        bEnableExceptions = true;
        bUseRTTI = true;
        
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "RenderCore",
            "RHI"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Projects"
        });

        string GStreamerPath = Path.Combine(ModuleDirectory, "../../ThirdParty/GStreamer");
        string GStreamerIncludePath = Path.Combine(GStreamerPath, "Include");
        string GStreamerLibPath = Path.Combine(GStreamerPath, "Lib");
        string GStreamerBinPath = Path.Combine(GStreamerPath, "Bin");

        PublicSystemIncludePaths.Add(Path.Combine(GStreamerIncludePath, "gstreamer-1.0"));
        PublicSystemIncludePaths.Add(Path.Combine(GStreamerIncludePath, "glib-2.0"));
        PublicSystemIncludePaths.Add(Path.Combine(GStreamerLibPath, "glib-2.0/include"));

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("WIN32_LEAN_AND_MEAN");
            PublicDefinitions.Add("NOMINMAX");
            PublicDefinitions.Add("NOGDI");
            PublicDefinitions.Add("_CRT_SECURE_NO_WARNINGS");
            
            // CRITICAL FIX: Force undefine GError through preprocessor
            // This creates a forced include file that undefs GError before anything else
            string ForceIncludePath = Path.Combine(ModuleDirectory, "Public", "GStreamerForceInclude.h");
            PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
            
            // Add as a forced include using compiler flag
            PublicAdditionalLibraries.Add(Path.Combine(GStreamerLibPath, "gstreamer-1.0.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(GStreamerLibPath, "gstapp-1.0.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(GStreamerLibPath, "gstvideo-1.0.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(GStreamerLibPath, "glib-2.0.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(GStreamerLibPath, "gobject-2.0.lib"));

            RuntimeDependencies.Add(Path.Combine(GStreamerBinPath, "*.dll"));
        }
        
        // Shadow Unreal's GError definition
        bLegacyPublicIncludePaths = false;
    }
}
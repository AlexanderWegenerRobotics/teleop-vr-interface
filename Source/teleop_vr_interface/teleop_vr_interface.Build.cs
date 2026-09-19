using UnrealBuildTool;
using System.IO;

public class teleop_vr_interface : ModuleRules
{
	public teleop_vr_interface(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        bEnableExceptions = true;

        // Msgpack
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../../ThirdParty/msgpack/include"));
        PublicDefinitions.Add("MSGPACK_NO_BOOST");

        // Eigen use system include to suppress warnings
        PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "../../ThirdParty/Eigen"));

        // Third-party OpenXR headers (<openxr/openxr.h>), which OpenXRCore.h
        // includes. The xr* entry points themselves are OPENXRHMD_API globals
        // in OpenXRDynamicAPI, so no loader link is required.
        PublicIncludePathModuleNames.Add("OpenXR");

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "RenderCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "SlateRHIRenderer",
            "MediaAssets", "Renderer", "RHI", "Sockets", "Networking", "EyeTracker", "ViveOpenXREyeTracker",
            "HeadMountedDisplay", "OpenXRHMD", "Json", "JsonUtilities", "ImageWrapper", "GStreamerPlugin",
        });

	}
}
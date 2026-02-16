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

        // Eigen – use system include to suppress warnings
        PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "../../ThirdParty/Eigen"));

	
		 PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "RenderCore", "EnhancedInput",
            "NDIIO", "MediaAssets", "Renderer", "RHI", "Sockets", "Networking",
            "HeadMountedDisplay", "OpenXRHMD", "XRBase", "OpenCV", "OpenCVHelper",
            "AudioMixer", "Synthesis", "Json", "JsonUtilities", "ImageWrapper"
        });

		PrivateDependencyModuleNames.AddRange(new string[] {
            "Slate", "SlateCore", "UMG", "TextToSpeech", "Niagara", "AdvancedWidgets", "XRVisualization"
        });
	}
}

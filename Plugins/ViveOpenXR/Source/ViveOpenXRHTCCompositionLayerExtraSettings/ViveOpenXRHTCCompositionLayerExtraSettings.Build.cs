// Copyright HTC Corporation. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ViveOpenXRHTCCompositionLayerExtraSettings : ModuleRules
{
    public ViveOpenXRHTCCompositionLayerExtraSettings(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivatePCHHeaderFile = @"Private\OpenXRCommon.h";
        var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

        PrivateIncludePaths.AddRange(
            new string[] {
            "ViveOpenXRHTCCompositionLayerExtraSettings/Private/External",
            EngineDir + "/Source/ThirdParty/OpenXR/include",
            EngineDir + "/Plugins/Runtime/OpenXR/Source/OpenXRHMD/Private",
            EngineDir + "/Plugins/Runtime/OpenXR/Source/OpenXRInput/Private",
            // ... add public include paths required here ...
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
				"ApplicationCore",
                "Engine",
                "InputDevice",
                "InputCore",
                "HeadMountedDisplay",
                "OpenXRHMD",
				"OpenXRInput"
			}
		);

        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenXR");

        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }
    }
}

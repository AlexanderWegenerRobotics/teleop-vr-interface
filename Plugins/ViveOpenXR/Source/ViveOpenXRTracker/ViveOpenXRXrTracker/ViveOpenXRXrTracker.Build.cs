// Copyright HTC Corporation. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
    public class ViveOpenXRXrTracker : ModuleRules
    {
        public ViveOpenXRXrTracker(ReadOnlyTargetRules Target) : base(Target)
        {
            PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
            PrivatePCHHeaderFile = @"Private\OpenXRCommon.h";
            var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

            PrivateIncludePaths.AddRange(
                new string[] {
                "ViveOpenXRTracker/ViveOpenXRXrTracker/Private/External",
                EngineDir + "/Source/ThirdParty/OpenXR/include",
                EngineDir + "/Plugins/Runtime/OpenXR/Source/OpenXRHMD/Private",
                EngineDir + "/Plugins/Runtime/OpenXR/Source/OpenXRInput/Private",
                }
                );

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                "Core",
                "CoreUObject",
                "ApplicationCore",
                "InputDevice",
                "InputCore",
                "Engine",
                "Slate",
                "SlateCore",
                "HeadMountedDisplay",
                "UMG",
                "OpenXRHMD",
                "OpenXRInput",
                "XRBase"
                }
                );

            PublicDependencyModuleNames.Add("EnhancedInput");

            AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenXR");

            if (Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.Add("UnrealEd");
                PrivateDependencyModuleNames.Add("InputEditor");
            }
        }
    }
}


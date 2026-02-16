// Copyright HTC Corporation. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class ViveOpenXRFuture : ModuleRules
{
    public ViveOpenXRFuture(ReadOnlyTargetRules Target) : base(Target)
    {
        var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
        PrivateIncludePaths.AddRange(
            new string[] {
                EngineDir + "/Plugins/Runtime/OpenXR/Source/OpenXRHMD/Private",
                // ... add public include paths required here ...
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "ViveOpenXRHMD",
                "ViveOpenXRWrapper",
                "ViveOpenXRCommon",
                "OpenXRHMD",
                "XRBase",
            }
        );
    }
}
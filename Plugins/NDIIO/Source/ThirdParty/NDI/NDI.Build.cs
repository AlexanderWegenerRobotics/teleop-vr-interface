/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided 
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

using System;
using System.IO;

using UnrealBuildTool;

public class NDI : ModuleRules
{
    public NDI(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Includes"));

            PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Libraries/Win64", "Processing.NDI.Lib.x64.lib"));

            // Delay-load the DLL from the runtime directory (determined at runtime)
            PublicDelayLoadDLLs.Add("Processing.NDI.Lib.x64.dll");

            // Ensure that we define our c++ define
            PublicDefinitions.Add("NDI_SDK_ENABLED");
        }
        else if ((Target.Platform == UnrealTargetPlatform.Linux)
            || ((Target.Version.MajorVersion == 4) && (Target.Platform.ToString() == "LinuxAArch64"))
            || ((Target.Version.MajorVersion == 5) && (Target.Platform.ToString() == "LinuxArm64"))
            )
        {
            PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Includes"));

            PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Libraries/Linux", "libndi.so"));
            RuntimeDependencies.Add(Path.Combine("$(BinaryOutputDir)", "libndi.so.6"), Path.Combine(ModuleDirectory, "Libraries/Linux", "libndi.so.6"));

            // Ensure that we define our c++ define
            PublicDefinitions.Add("NDI_SDK_ENABLED");
        }
    }
}

/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided 
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

using System;
using System.IO;

using UnrealBuildTool;

public class NDIIO : ModuleRules
{
	public NDIIO(ReadOnlyTargetRules Target) : base(Target)
	{
#if UE_5_2_OR_LATER
		IWYUSupport = IWYUSupport.Full;
#else
		bEnforceIWYU = true;
#endif
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		#region Public Includes

		// Include the Public include paths
		if (Directory.Exists(Path.Combine(ModuleDirectory, "Public")))
		{
			PublicIncludePaths.AddRange(new string[] {
				// ... add public include paths required here ...
				Path.Combine(ModuleDirectory, "Public" ),
			});
		}

		// Define the public dependencies
		PublicDependencyModuleNames.AddRange(new string[] {
			"Engine",
			"Core",
			"CoreUObject",
			"Projects",
			"NDIIOShaders"
		});

		#endregion

		#region Private Includes

		if (Directory.Exists(Path.Combine(ModuleDirectory, "Private")))
		{
			PrivateIncludePaths.AddRange(new string[] {
				// ... add other private include paths required here ...
				Path.Combine(ModuleDirectory, "Private" )
			});
		}

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Renderer",
			"RenderCore",
			"RHI",
			"Slate",
			"SlateCore",
			"UMG",
			"ImageWrapper",
			"AudioMixer",
			"AudioExtensions",

			"InputCore",

			"Media",
			"MediaAssets",
			"MediaIOCore",
			"MediaUtils",
			"TimeManagement",

			"CinematicCamera",

			"XmlParser"
		});

		#endregion

		#region Editor Includes

		if (Target.bBuildEditor == true)
		{
			PrivateIncludePathModuleNames.AddRange(new string[] {
				"AssetTools",
				"TargetPlatform",
			});

			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd",
				"AssetTools",
				"MaterialUtilities"
			});
		}

		#endregion

		#region ThirdParty Includes

		PublicDependencyModuleNames.Add("NDI");

		#endregion
	}
}

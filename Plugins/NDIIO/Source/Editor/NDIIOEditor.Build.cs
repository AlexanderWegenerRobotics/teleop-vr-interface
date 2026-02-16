/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided 
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

using System;
using System.IO;

using UnrealBuildTool;

public class NDIIOEditor : ModuleRules
{
	public NDIIOEditor(ReadOnlyTargetRules Target) : base(Target)
	{
#if UE_5_2_OR_LATER
		IWYUSupport = IWYUSupport.Full;
#else
		bEnforceIWYU = true;
#endif
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		#region Public Includes

		if (Directory.Exists(Path.Combine(ModuleDirectory, "Public")))
		{
			PublicIncludePaths.AddRange(new string[] {
				// ... add public include paths required here ...
				Path.Combine(ModuleDirectory, "Public" ),
			});
		}

		PublicDependencyModuleNames.AddRange(new string[] {
			"Engine",
			"Core",
			"CoreUObject"
		});

		#endregion

		if (Target.bBuildEditor == true)
		{
			#region Private Includes

			if (Directory.Exists(Path.Combine(ModuleDirectory, "Private")))
			{
				PrivateIncludePaths.AddRange(new string[] {
					// ... add other private include paths required here ...
					Path.Combine(ModuleDirectory, "Private" ),
					Path.Combine(ModuleDirectory, "../Core/Private"),
				});
			}

			#endregion

			PrivateIncludePathModuleNames.AddRange(new string[] {
				"AssetTools",
				"TargetPlatform",
			});

			PrivateDependencyModuleNames.AddRange(new string[] {
				"Projects",
				"UnrealEd",
				"AssetTools",
				"MaterialUtilities",
				"Renderer",
				"RenderCore",
				"PlacementMode",
				"CinematicCamera",

				"RHI",
				"Slate",
				"SlateCore",
				"UMG",
				"ImageWrapper",

				"Media",
				"MediaAssets",
				"MediaUtils",

				"AssetTools",
				"TargetPlatform",
				"PropertyEditor",
				"DetailCustomizations",
				"EditorStyle",

				"NDIIO"
			});
		}
	}
}

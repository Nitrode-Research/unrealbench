// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RTSTests : ModuleRules
{
	public RTSTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AIModule",
				"AssetRegistry",
				"Core",
				"CoreUObject",
				"Engine",
				"EnhancedInput",
				"InputCore",
				"NavigationSystem",
				"RenderCore",
				"RTS",
				"Slate",
				"SlateCore",
				"UMG",
				"UnrealEd"
			});
	}
}

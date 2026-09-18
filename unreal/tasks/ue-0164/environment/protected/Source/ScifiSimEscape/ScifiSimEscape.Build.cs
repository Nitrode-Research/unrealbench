// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ScifiSimEscape : ModuleRules
{
	public ScifiSimEscape(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"ScifiSimEscape",
			"ScifiSimEscape/Variant_Horror",
			"ScifiSimEscape/Variant_Horror/UI",
			"ScifiSimEscape/Variant_Shooter",
			"ScifiSimEscape/Variant_Shooter/AI",
			"ScifiSimEscape/Variant_Shooter/UI",
			"ScifiSimEscape/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}

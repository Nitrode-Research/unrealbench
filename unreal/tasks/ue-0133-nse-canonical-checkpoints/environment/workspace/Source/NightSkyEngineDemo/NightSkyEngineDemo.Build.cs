// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NightSkyEngineDemo : ModuleRules
{
	public NightSkyEngineDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"NightSkyEngine",
            "GameplayTags",
		});

		DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "DeveloperSettings", "AssetRegistry" });
        }

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}

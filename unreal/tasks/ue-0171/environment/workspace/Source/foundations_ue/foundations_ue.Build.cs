// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class foundations_ue : ModuleRules
{
	public foundations_ue(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

		PrivateDependencyModuleNames.AddRange(new string[] { "AIModule", "NavigationSystem", "CableComponent", "ProceduralMeshComponent", "Json", "Slate", "SlateCore" });
		PrivateIncludePaths.Add(ModuleDirectory);
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.AddRange(new string[] { "MeshDescription", "StaticMeshDescription", "AssetRegistry" });
		}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}

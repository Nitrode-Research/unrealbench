// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class foundations_ueTarget : TargetRules
{
	public foundations_ueTarget(TargetInfo Target) : base(Target)
	{
		BuildEnvironment = TargetBuildEnvironment.Shared;
        bOverrideBuildEnvironment = true;
        Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("foundations_ue");
	}
}

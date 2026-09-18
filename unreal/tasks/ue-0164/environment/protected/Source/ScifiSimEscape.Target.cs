// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class ScifiSimEscapeTarget : TargetRules
{
	public ScifiSimEscapeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		BuildEnvironment = TargetBuildEnvironment.Shared;
		bOverrideBuildEnvironment = true;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("ScifiSimEscape");
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class foundations_ueEditorTarget : TargetRules
{
	public foundations_ueEditorTarget( TargetInfo Target) : base(Target)
	{
		BuildEnvironment = TargetBuildEnvironment.Shared;
        bOverrideBuildEnvironment = true;
        Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("foundations_ue");
	}
}

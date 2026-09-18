// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/RTSCameraPawn.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/RTSPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FRTSVerifyPIECameraPossessionCommand,
	FAutomationTestBase*, Test,
	double, DeadlineSeconds);

bool FRTSVerifyPIECameraPossessionCommand::Update()
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType != EWorldType::PIE || WorldContext.World() == nullptr)
		{
			continue;
		}

		APlayerController* PlayerController = WorldContext.World()->GetFirstPlayerController();
		if (PlayerController == nullptr || PlayerController->GetPawn() == nullptr)
		{
			break;
		}

		Test->TestTrue(
			TEXT("PIE creates the native RTS player controller"),
			PlayerController->IsA<ARTSPlayerController>());
		Test->TestTrue(
			TEXT("PIE possesses the native fixed-angle camera pawn"),
			PlayerController->GetPawn()->IsA<ARTSCameraPawn>());
		Test->TestEqual(
			TEXT("The possessed camera pawn is the active PIE view target"),
			PlayerController->GetViewTarget(),
			static_cast<AActor*>(PlayerController->GetPawn()));
		return true;
	}

	if (FPlatformTime::Seconds() >= DeadlineSeconds)
	{
		Test->AddError(TEXT("PIE did not create and possess the RTS camera within ten seconds."));
		return true;
	}

	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1PIEPossessionTest,
	"Task0169.Headless.RTS.Milestone1.Baseline.PIEPossession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone1PIEPossessionTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR SandboxMapPackage[] = TEXT("/Game/RTS/Maps/M1_CombatSandbox");
	if (!TestTrue(TEXT("The sandbox opens before PIE starts"), AutomationOpenMap(SandboxMapPackage, true)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyPIECameraPossessionCommand(this, FPlatformTime::Seconds() + 10.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/RTSCameraPawn.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Match/RTSMatchSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Presentation/RTSWorldOverlaySubsystem.h"
#include "Structures/RTSDeploymentViewSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"
#include "World/RTSStartingZone.h"

#if WITH_DEV_AUTOMATION_TESTS

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FRTSVerifyHeadquartersDeploymentCommand,
	FAutomationTestBase*, Test,
	double, DeadlineSeconds);

namespace
{
UWorld* FindDeploymentWorld()
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::PIE)
		{
			return WorldContext.World();
		}
	}
	return nullptr;
}
}

bool FRTSVerifyHeadquartersDeploymentCommand::Update()
{
	UWorld* World = nullptr;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::PIE)
		{
			World = WorldContext.World();
			break;
		}
	}
	if (World == nullptr)
	{
		if (FPlatformTime::Seconds() >= DeadlineSeconds)
		{
			Test->AddError(TEXT("PIE did not expose the Milestone 2 deployment world."));
			return true;
		}
		return false;
	}

	ARTSCombatUnit* CommandVehicle = nullptr;
	ARTSStartingZone* PlayerZone = nullptr;
	for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
	{
		if (UnitIterator->GetGenericTeamId() == RTSTeams::Player
			&& UnitIterator->IsMilestone2Unit()
			&& UnitIterator->GetUnitType() == ERTSUnitType::CommandVehicle)
		{
			CommandVehicle = *UnitIterator;
			break;
		}
	}
	for (TActorIterator<ARTSStartingZone> ZoneIterator(World); ZoneIterator; ++ZoneIterator)
	{
		if (ZoneIterator->GetGenericTeamId() == RTSTeams::Player)
		{
			PlayerZone = *ZoneIterator;
			break;
		}
	}
	URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>();
	URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>();
	URTSMatchSubsystem* Match = World->GetSubsystem<URTSMatchSubsystem>();
	URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>();
	URTSWorldOverlaySubsystem* Overlays = World->GetSubsystem<URTSWorldOverlaySubsystem>();
	if (CommandVehicle == nullptr || PlayerZone == nullptr || Economy == nullptr || Structures == nullptr || Match == nullptr || Commands == nullptr || Overlays == nullptr)
	{
		if (FPlatformTime::Seconds() >= DeadlineSeconds)
		{
			Test->AddError(TEXT("The deployment fixture did not initialize its typed roster and subsystems."));
			return true;
		}
		return false;
	}

	const FRTSMilestone2Configuration Configuration = FRTSMilestone2Configuration::Load();
	const FRTSEconomySnapshot Opening = Economy->GetSnapshot(RTSTeams::Player);
	Test->TestEqual(TEXT("The opening roster consumes two Infantry plus one Light Vehicle"), Opening.SupplyUsed, 4);
	Test->TestEqual(TEXT("No HQ contribution exists before deployment"), Opening.PowerGeneration, 0);
	Test->TestEqual(TEXT("No supply capacity exists before deployment"), Opening.SupplyCapacity, 0);
	Test->TestEqual(TEXT("The Match subsystem owns the mobile command identity"), Match->GetCommandVehicle(RTSTeams::Player), CommandVehicle);

	const FVector ValidLocation = CommandVehicle->GetActorLocation();
	ARTSCombatUnit* CommandVehicles[] = {CommandVehicle};
	const FRTSCommandResult LegalMove = Commands->IssueMove(
		CommandVehicles,
		PlayerZone->GetActorLocation() + FVector(1000.0f, 0.0f, 0.0f));
	Test->TestEqual(TEXT("The Command Vehicle accepts movement inside its starting zone"), LegalMove.GetAcceptedCount(), 1);
	CommandVehicle->GetOrderComponent()->Cancel();
	CommandVehicle->SetActorLocation(ValidLocation, false, nullptr, ETeleportType::TeleportPhysics);
	const FRTSCommandResult IllegalMove = Commands->IssueMove(
		CommandVehicles,
		PlayerZone->GetActorLocation() + FVector(PlayerZone->GetRadius() + 1000.0f, 0.0f, 0.0f));
	Test->TestEqual(TEXT("The Command Vehicle rejects movement outside its starting zone"), IllegalMove.GetAcceptedCount(), 0);
	if (!IllegalMove.Outcomes.IsEmpty())
	{
		Test->TestEqual(TEXT("The movement refusal names the zone boundary"), IllegalMove.Outcomes[0].Failure, ERTSOrderFailure::OutsideStartingZone);
	}

	const FVector2D HeadquartersHalfFootprint(
		Configuration.Headquarters.FootprintCells.X * Configuration.World.PlacementCellSize * 0.5f,
		Configuration.Headquarters.FootprintCells.Y * Configuration.World.PlacementCellSize * 0.5f);
	const float HeadquartersFootprintRadius = HeadquartersHalfFootprint.Size();
	const FVector EdgeLocation = PlayerZone->GetActorLocation()
		+ FVector(PlayerZone->GetRadius() - HeadquartersFootprintRadius * 0.5f, 0.0f, 80.0f);
	CommandVehicle->SetActorLocation(
		EdgeLocation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	const FRTSHeadquartersDeploymentPreview InvalidPreview =
		Structures->EvaluateHeadquartersDeployment(*CommandVehicle);
	Test->TestFalse(TEXT("A vehicle center inside the zone cannot deploy when the full HQ footprint crosses it"), InvalidPreview.bValid);
	Test->TestEqual(TEXT("The edge preview reports the complete-footprint zone refusal"), InvalidPreview.Refusal, ERTSDeploymentRefusal::OutsideStartingZone);
	Test->TestTrue(
		TEXT("A rejected edge preview remains anchored to the Command Vehicle"),
		InvalidPreview.GroundLocation.Equals(FVector(EdgeLocation.X, EdgeLocation.Y, 0.0f), 5.0f));
	const FRTSHeadquartersDeploymentResult Invalid = Structures->TryDeployHeadquarters(*CommandVehicle);
	Test->TestFalse(TEXT("Deployment outside the complete starting-zone footprint is rejected"), Invalid.bAccepted);
	Test->TestEqual(TEXT("The invalid location has a typed refusal"), Invalid.Refusal, ERTSDeploymentRefusal::OutsideStartingZone);
	Test->TestEqual(TEXT("Invalid deployment changes no Power"), Economy->GetSnapshot(RTSTeams::Player).PowerGeneration, 0);
	Test->TestNull(TEXT("Invalid deployment creates no HQ identity"), Match->GetHeadquarters(RTSTeams::Player));

	CommandVehicle->SetActorLocation(ValidLocation, false, nullptr, ETeleportType::TeleportPhysics);
	const FRTSHeadquartersDeploymentPreview Preview = Structures->EvaluateHeadquartersDeployment(*CommandVehicle);
	Test->TestTrue(TEXT("The authored Command Vehicle location is valid HQ ground"), Preview.bValid);
	const FRTSHeadquartersDeploymentResult Deployed = Structures->TryDeployHeadquarters(*CommandVehicle);
	Test->TestTrue(TEXT("Valid deployment commits"), Deployed.bAccepted);
	ARTSStructure* Headquarters = Match->GetHeadquarters(RTSTeams::Player);
	if (Test->TestNotNull(TEXT("Deployment establishes the HQ command identity"), Headquarters))
	{
		Test->TestEqual(TEXT("The structure has closed HQ identity"), Headquarters->GetStructureType(), ERTSStructureType::Headquarters);
		Test->TestEqual(TEXT("The HQ owns the configured 12 by 10 footprint"), Headquarters->GetFootprintCells(), Configuration.Headquarters.FootprintCells);
		Test->TestEqual(TEXT("The HQ establishes the configured build area"), Headquarters->GetBuildAreaRadius(), Configuration.Headquarters.BuildAreaRadius);
		Test->TestTrue(TEXT("The HQ replaces the vehicle at its chosen location"), Headquarters->GetActorLocation().Equals(FVector(ValidLocation.X, ValidLocation.Y, 0.0f), 5.0f));
		const TArray<FRTSWorldOverlaySnapshot> OverlaySnapshots = Overlays->GetSnapshots();
		const FRTSWorldOverlaySnapshot* BuildArea = OverlaySnapshots.FindByPredicate(
			[Headquarters](const FRTSWorldOverlaySnapshot& Snapshot)
			{
				return Snapshot.Descriptor.SourceKind == ERTSWorldOverlaySourceKind::Structure
					&& Snapshot.Descriptor.Mode == ERTSWorldOverlayMode::BuildArea
					&& Snapshot.Descriptor.StableSourceId == Headquarters->GetStableStructureId();
			});
		Test->TestNotNull(TEXT("The HQ submits a typed build-area overlay"), BuildArea);
		if (BuildArea != nullptr)
		{
			Test->TestEqual(TEXT("The build-area overlay uses the configured radius"), BuildArea->Descriptor.Radius, Configuration.Headquarters.BuildAreaRadius);
			Test->TestEqual(TEXT("The build-area overlay keeps player faction identity"), BuildArea->Descriptor.TeamId, RTSTeams::Player);
			Test->TestTrue(
				TEXT("The build-area overlay follows the deployed HQ"),
				BuildArea->Descriptor.WorldTransform.GetLocation().Equals(Headquarters->GetActorLocation(), 5.0f));
		}
	}
	Test->TestNull(TEXT("Deployment removes mobile command identity"), Match->GetCommandVehicle(RTSTeams::Player));
	const FRTSEconomySnapshot AfterDeployment = Economy->GetSnapshot(RTSTeams::Player);
	Test->TestEqual(TEXT("HQ Power applies exactly once"), AfterDeployment.PowerGeneration, Configuration.Headquarters.PowerGeneration);
	Test->TestEqual(TEXT("HQ Supply applies exactly once"), AfterDeployment.SupplyCapacity, Configuration.Headquarters.SupplyCapacity);
	Test->TestEqual(TEXT("Deployment does not spend starting Materials"), AfterDeployment.Materials, Configuration.Economy.StartingMaterials);

	const FRTSHeadquartersDeploymentResult Repeated = Structures->TryDeployHeadquarters(*CommandVehicle);
	Test->TestFalse(TEXT("The consumed Command Vehicle cannot deploy again"), Repeated.bAccepted);
	Test->TestEqual(TEXT("Repeated deployment does not duplicate Power"), Economy->GetSnapshot(RTSTeams::Player).PowerGeneration, Configuration.Headquarters.PowerGeneration);
	int32 HeadquartersCount = 0;
	for (TActorIterator<ARTSStructure> StructureIterator(World); StructureIterator; ++StructureIterator)
	{
		HeadquartersCount += StructureIterator->GetStructureType() == ERTSStructureType::Headquarters ? 1 : 0;
	}
	Test->TestEqual(TEXT("Exactly one HQ actor exists"), HeadquartersCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone2HeadquartersDeploymentTest,
	"Task0168.Headless.RTS.Milestone2.Deployment.Headquarters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone2HeadquartersDeploymentTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR PlayerLoopMapPackage[] = TEXT("/Game/RTS/Maps/M2_PlayerLoop");
	if (!TestTrue(TEXT("The battlefield opens before deployment verification"), AutomationOpenMap(PlayerLoopMapPackage, true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyHeadquartersDeploymentCommand(this, FPlatformTime::Seconds() + 10.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

class FRTSVerifyDeploymentEdgePreviewCommand final : public IAutomationLatentCommand
{
public:
	FRTSVerifyDeploymentEdgePreviewCommand(FAutomationTestBase* InTest, const double InDeadline)
		: Test(InTest), Deadline(InDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = FindDeploymentWorld();
		if (World == nullptr)
		{
			if (FPlatformTime::Seconds() >= Deadline)
			{
				Test->AddError(TEXT("PIE did not expose the deployment-preview world."));
				return true;
			}
			return false;
		}
		PinCamera();

		if (Phase == EPhase::Stage)
		{
			return Stage(*World);
		}
		if (Phase == EPhase::AwaitFrame)
		{
			return AwaitFrame();
		}
		return AwaitCapture();
	}

private:
	enum class EPhase : uint8
	{
		Stage,
		AwaitFrame,
		AwaitCapture
	};

	void PinCamera()
	{
		if (Camera.IsValid())
		{
			Camera->SetKeyboardPanInput(FVector2D::ZeroVector);
			Camera->SetEdgePanInput(FVector2D::ZeroVector);
			Camera->SetActorLocation(CameraLocation);
		}
	}

	bool Stage(UWorld& World)
	{
		ARTSCombatUnit* CommandVehicle = nullptr;
		ARTSStartingZone* PlayerZone = nullptr;
		for (TActorIterator<ARTSCombatUnit> UnitIterator(&World); UnitIterator; ++UnitIterator)
		{
			if (UnitIterator->GetGenericTeamId() == RTSTeams::Player
				&& UnitIterator->GetUnitType() == ERTSUnitType::CommandVehicle)
			{
				CommandVehicle = *UnitIterator;
				break;
			}
		}
		for (TActorIterator<ARTSStartingZone> ZoneIterator(&World); ZoneIterator; ++ZoneIterator)
		{
			if (ZoneIterator->GetGenericTeamId() == RTSTeams::Player)
			{
				PlayerZone = *ZoneIterator;
				break;
			}
		}

		APlayerController* Controller = World.GetFirstPlayerController();
		ULocalPlayer* LocalPlayer = Controller != nullptr ? Controller->GetLocalPlayer() : nullptr;
		URTSDeploymentViewSubsystem* View = LocalPlayer != nullptr
			? LocalPlayer->GetSubsystem<URTSDeploymentViewSubsystem>()
			: nullptr;
		ARTSCameraPawn* CameraPawn = Controller != nullptr ? Cast<ARTSCameraPawn>(Controller->GetPawn()) : nullptr;
		URTSStructureSubsystem* Structures = World.GetSubsystem<URTSStructureSubsystem>();
		if (!Test->TestNotNull(TEXT("The preview fixture has a Command Vehicle"), CommandVehicle)
			|| !Test->TestNotNull(TEXT("The preview fixture has a player starting zone"), PlayerZone)
			|| !Test->TestNotNull(TEXT("The preview fixture has a deployment view"), View)
			|| !Test->TestNotNull(TEXT("The preview fixture has an RTS camera"), CameraPawn)
			|| !Test->TestNotNull(TEXT("The preview fixture has structure authority"), Structures))
		{
			return true;
		}

		const FRTSMilestone2Configuration Configuration = FRTSMilestone2Configuration::Load();
		const FVector2D HalfFootprint(
			Configuration.Headquarters.FootprintCells.X * Configuration.World.PlacementCellSize * 0.5f,
			Configuration.Headquarters.FootprintCells.Y * Configuration.World.PlacementCellSize * 0.5f);
		const FVector EdgeLocation = PlayerZone->GetActorLocation()
			+ FVector(PlayerZone->GetRadius() - HalfFootprint.Size() * 0.5f, 0.0f, 80.0f);
		CommandVehicle->SetActorLocation(EdgeLocation, false, nullptr, ETeleportType::TeleportPhysics);
		const FRTSHeadquartersDeploymentPreview Preview =
			Structures->EvaluateHeadquartersDeployment(*CommandVehicle);
		Test->TestFalse(TEXT("The rendered edge position is outside the complete-footprint limit"), Preview.bValid);
		Test->TestEqual(TEXT("The rendered edge position reports the starting-zone refusal"),
			Preview.Refusal,
			ERTSDeploymentRefusal::OutsideStartingZone);
		Test->TestTrue(TEXT("The rendered refusal is anchored at the Command Vehicle"),
			Preview.GroundLocation.Equals(FVector(EdgeLocation.X, EdgeLocation.Y, 0.0f), 5.0f));

		View->BeginHeadquartersPreview(*CommandVehicle);
		Camera = CameraPawn;
		CameraLocation = FVector(EdgeLocation.X, EdgeLocation.Y, 100.0f);
		PinCamera();
		CameraPawn->AddZoomInput(-4.0f);
		if (FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
		{
			return true;
		}

		Deadline = FPlatformTime::Seconds() + 1.0;
		Phase = EPhase::AwaitFrame;
		return false;
	}

	bool AwaitFrame()
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		ScreenshotPath = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("Screenshots/RTS/M2DeploymentEdgePreview.png"));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ScreenshotPath), true);
		IFileManager::Get().Delete(*ScreenshotPath, false, true);
		FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
		Deadline = FPlatformTime::Seconds() + 5.0;
		Phase = EPhase::AwaitCapture;
		return false;
	}

	bool AwaitCapture()
	{
		if (IFileManager::Get().FileSize(*ScreenshotPath) > 0)
		{
			Test->AddInfo(FString::Printf(TEXT("Rendered deployment-edge capture: %s"), *ScreenshotPath));
			return true;
		}
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		Test->AddError(TEXT("The rendered deployment-edge screenshot was not written."));
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	EPhase Phase = EPhase::Stage;
	double Deadline = 0.0;
	FString ScreenshotPath;
	TWeakObjectPtr<ARTSCameraPawn> Camera;
	FVector CameraLocation = FVector::ZeroVector;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone2DeploymentEdgePreviewTest,
	"Task0168.Headless.RTS.Milestone2.Deployment.EdgePreviewFeedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone2DeploymentEdgePreviewTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("The battlefield opens before deployment-edge feedback verification"),
		AutomationOpenMap(TEXT("/Game/RTS/Maps/M2_PlayerLoop"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyDeploymentEdgePreviewCommand(this, FPlatformTime::Seconds() + 10.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

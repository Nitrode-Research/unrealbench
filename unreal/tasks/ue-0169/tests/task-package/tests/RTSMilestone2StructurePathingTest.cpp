// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/RTSCameraPawn.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Match/RTSMatchSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
UWorld* FindStructurePathingWorld()
{
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE)
		{
			return Context.World();
		}
	}
	return nullptr;
}

ARTSCombatUnit* FindPlayerCommandVehicle(UWorld& World)
{
	if (URTSMatchSubsystem* Match = World.GetSubsystem<URTSMatchSubsystem>())
	{
		return Match->GetCommandVehicle(RTSTeams::Player);
	}
	return nullptr;
}
}

class FRTSVerifyStructurePathingCommand final : public IAutomationLatentCommand
{
public:
	FRTSVerifyStructurePathingCommand(FAutomationTestBase* InTest, const double InWorldDeadline)
		: Test(InTest), Deadline(InWorldDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = FindStructurePathingWorld();
		if (World == nullptr)
		{
			if (FPlatformTime::Seconds() >= Deadline)
			{
				Test->AddError(TEXT("PIE did not expose the structure-pathing world."));
				return true;
			}
			return false;
		}
		PinCamera(*World);

		switch (Phase)
		{
		case EPhase::Stage: return Stage(*World);
		case EPhase::IssueMove: return IssueMove(*World);
		case EPhase::AwaitArrival: return AwaitArrival(*World);
		case EPhase::AwaitCapture: return AwaitCapture();
		}
		return true;
	}

private:
	enum class EPhase : uint8
	{
		Stage,
		IssueMove,
		AwaitArrival,
		AwaitCapture
	};

	void PinCamera(UWorld& World)
	{
		const APlayerController* Controller = World.GetFirstPlayerController();
		ARTSCameraPawn* Camera = Controller != nullptr ? Cast<ARTSCameraPawn>(Controller->GetPawn()) : nullptr;
		if (Camera == nullptr)
		{
			return;
		}
		Camera->SetEdgePanInput(FVector2D::ZeroVector);
		Camera->SetActorLocation(FVector(0.0f, -7200.0f, 100.0f));
		if (!bCameraZoomedOut)
		{
			Camera->AddZoomInput(-4.0f);
			bCameraZoomedOut = true;
		}
	}

	bool Stage(UWorld& World)
	{
		URTSStructureSubsystem* Structures = World.GetSubsystem<URTSStructureSubsystem>();
		ARTSCombatUnit* CommandVehicle = FindPlayerCommandVehicle(World);
		if (Structures == nullptr || CommandVehicle == nullptr)
		{
			Test->AddError(TEXT("The structure-pathing fixture did not find deployment state."));
			return true;
		}
		if (!Test->TestTrue(TEXT("The pathing fixture deploys one blocking Headquarters"),
			Structures->TryDeployHeadquarters(*CommandVehicle).bAccepted))
		{
			return true;
		}

		constexpr int32 UnitCount = 12;
		constexpr int32 ColumnCount = 4;
		for (int32 UnitIndex = 0; UnitIndex < UnitCount; ++UnitIndex)
		{
			const FVector SpawnLocation(
				-750.0f + (UnitIndex % ColumnCount) * 500.0f,
				-13000.0f + (UnitIndex / ColumnCount) * 500.0f,
				80.0f);
			ARTSCombatUnit* Unit = World.SpawnActorDeferred<ARTSCombatUnit>(
				ARTSCombatUnit::StaticClass(),
				FTransform(FRotator::ZeroRotator, SpawnLocation),
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Test->TestNotNull(TEXT("Every structure-pathing unit spawns"), Unit))
			{
				return true;
			}
			Unit->ConfigureMilestone2Unit(
				ERTSUnitType::InfantrySquad,
				1000 + UnitIndex,
				RTSTeams::Player);
			Unit->FinishSpawning(FTransform(FRotator::ZeroRotator, SpawnLocation));
			Units.Add(Unit);
		}

		Deadline = FPlatformTime::Seconds() + 0.75;
		Phase = EPhase::IssueMove;
		return false;
	}

	bool IssueMove(UWorld& World)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		TArray<ARTSCombatUnit*> LiveUnits;
		for (const TWeakObjectPtr<ARTSCombatUnit>& Unit : Units)
		{
			if (Unit.IsValid())
			{
				LiveUnits.Add(Unit.Get());
			}
		}
		const FRTSCommandResult Move = World.GetSubsystem<URTSCommandSubsystem>()->IssueMove(
			LiveUnits,
			FVector(0.0f, -5000.0f, 0.0f));
		if (!Test->TestEqual(TEXT("All twelve units accept the move across the HQ footprint"),
			Move.GetAcceptedCount(), 12))
		{
			return true;
		}
		for (const FRTSUnitCommandOutcome& Outcome : Move.Outcomes)
		{
			if (Outcome.bAccepted && Outcome.Unit != nullptr)
			{
				AssignedDestinations.Add(Outcome.Unit->GetStableUnitId(), Outcome.AssignedDestination);
			}
		}
		// Crowd avoidance is frame-step sensitive; accelerate the long crossing without turning
		// the rendered trial into a large-delta stress test that players never experience.
		World.GetWorldSettings()->SetTimeDilation(4.0f);
		Deadline = FPlatformTime::Seconds() + 8.0;
		Phase = EPhase::AwaitArrival;
		return false;
	}

	bool AwaitArrival(UWorld& World)
	{
		int32 ArrivedCount = 0;
		for (const TWeakObjectPtr<ARTSCombatUnit>& UnitPointer : Units)
		{
			ARTSCombatUnit* Unit = UnitPointer.Get();
			if (Unit == nullptr)
			{
				continue;
			}
			const FVector* Destination = AssignedDestinations.Find(Unit->GetStableUnitId());
			if (Destination == nullptr)
			{
				continue;
			}
			const FRTSOrderSnapshot Snapshot = Unit->GetOrderComponent()->GetSnapshot();
			if (Snapshot.Phase == ERTSOrderPhase::Idle
				&& Snapshot.LastFailure == ERTSOrderFailure::None
				&& FVector::DistSquared2D(Unit->GetActorLocation(), *Destination) <= FMath::Square(140.0f))
			{
				++ArrivedCount;
			}
		}

		if (ArrivedCount == Units.Num())
		{
			World.GetWorldSettings()->SetTimeDilation(1.0f);
			if (!FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
			{
				ScreenshotPath = FPaths::Combine(
					FPaths::ProjectSavedDir(),
					TEXT("Screenshots/RTS/M2StructurePathing.png"));
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(ScreenshotPath), true);
				IFileManager::Get().Delete(*ScreenshotPath, false, true);
				FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
				Deadline = FPlatformTime::Seconds() + 5.0;
				Phase = EPhase::AwaitCapture;
				return false;
			}
			return true;
		}
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}

		for (const TWeakObjectPtr<ARTSCombatUnit>& UnitPointer : Units)
		{
			const ARTSCombatUnit* Unit = UnitPointer.Get();
			if (Unit == nullptr)
			{
				continue;
			}
			const FVector* Destination = AssignedDestinations.Find(Unit->GetStableUnitId());
			const FRTSOrderSnapshot Snapshot = Unit->GetOrderComponent()->GetSnapshot();
			Test->AddInfo(FString::Printf(
				TEXT("Structure pathing unit=%d phase=%d failure=%d location=%s destination=%s distance=%.1f."),
				Unit->GetStableUnitId(),
				static_cast<int32>(Snapshot.Phase),
				static_cast<int32>(Snapshot.LastFailure),
				*Unit->GetActorLocation().ToCompactString(),
				Destination != nullptr ? *Destination->ToCompactString() : TEXT("missing"),
				Destination != nullptr ? FVector::Dist2D(Unit->GetActorLocation(), *Destination) : -1.0f));
		}
		Test->AddError(FString::Printf(
			TEXT("Only %d of 12 units reached their assigned slots after crossing the completed HQ footprint."),
			ArrivedCount));
		World.GetWorldSettings()->SetTimeDilation(1.0f);
		return true;
	}

	bool AwaitCapture()
	{
		if (IFileManager::Get().FileSize(*ScreenshotPath) > 0)
		{
			Test->AddInfo(FString::Printf(TEXT("Rendered structure-pathing capture: %s"), *ScreenshotPath));
			return true;
		}
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		Test->AddError(TEXT("The rendered structure-pathing screenshot was not written."));
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	EPhase Phase = EPhase::Stage;
	double Deadline = 0.0;
	TArray<TWeakObjectPtr<ARTSCombatUnit>> Units;
	TMap<int32, FVector> AssignedDestinations;
	FString ScreenshotPath;
	bool bCameraZoomedOut = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone2StructurePathingTest,
	"Task0169.Headless.RTS.Milestone2.Movement.GroupPathsAroundHeadquarters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone2StructurePathingTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("The battlefield opens before structure pathing verification"),
		AutomationOpenMap(TEXT("/Game/RTS/Maps/M2_PlayerLoop"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyStructurePathingCommand(this, FPlatformTime::Seconds() + 10.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

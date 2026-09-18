// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/RTSAIStrategySubsystem.h"
#include "Camera/RTSCameraPawn.h"
#include "Components/BoxComponent.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Configuration/RTSMilestone3Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Presentation/RTSWorldOverlaySubsystem.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSUnitRegistrySubsystem.h"
#include "Units/RTSTeams.h"
#include "UnrealClient.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RTSMilestone3AIDeploymentTestPrivate
{
UWorld* FindPIEWorld()
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

ARTSCombatUnit* FindEnemyCommandVehicle(UWorld& World)
{
	URTSUnitRegistrySubsystem* Units = World.GetSubsystem<URTSUnitRegistrySubsystem>();
	const TArray<FRTSUnitSnapshot> CommandVehicles = Units != nullptr
		? Units->Query(RTSTeams::Enemy, ERTSUnitType::CommandVehicle)
		: TArray<FRTSUnitSnapshot>();
	return !CommandVehicles.IsEmpty() && Units != nullptr
		? Units->FindActor(CommandVehicles[0].StableUnitId)
		: nullptr;
}

AActor* SpawnQueryOnlyHeadquartersBlocker(UWorld& World, const FVector& GroundLocation)
{
	AActor* Blocker = World.SpawnActor<AActor>(
		AActor::StaticClass(),
		GroundLocation + FVector(0.0f, 0.0f, 225.0f),
		FRotator::ZeroRotator);
	if (Blocker == nullptr)
	{
		return nullptr;
	}
	UBoxComponent* Box = NewObject<UBoxComponent>(Blocker, TEXT("HQCandidateBlocker"));
	Blocker->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(600.0f, 500.0f, 225.0f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Overlap);
	Box->RegisterComponent();
	Blocker->SetActorLocation(GroundLocation + FVector(0.0f, 0.0f, 225.0f));
	return Blocker;
}

void RemoveBlocker(TWeakObjectPtr<AActor>& Blocker)
{
	if (Blocker.IsValid())
	{
		Blocker->SetActorEnableCollision(false);
		Blocker->Destroy();
	}
	Blocker.Reset();
}
}

class FRTSMilestone3HQCandidateCommand final : public IAutomationLatentCommand
{
public:
	FRTSMilestone3HQCandidateCommand(FAutomationTestBase* InTest, const double InDeadline)
		: Test(InTest), Deadline(InDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = RTSMilestone3AIDeploymentTestPrivate::FindPIEWorld();
		if (World == nullptr)
		{
			return FinishIfTimedOut(TEXT("PIE did not expose the M3 candidate world."));
		}
		URTSAIStrategySubsystem* Strategy = World->GetSubsystem<URTSAIStrategySubsystem>();
		ARTSCombatUnit* CommandVehicle = RTSMilestone3AIDeploymentTestPrivate::FindEnemyCommandVehicle(*World);
		if (Strategy == nullptr || CommandVehicle == nullptr)
		{
			return FinishIfTimedOut(TEXT("The candidate fixture is missing AI or its Command Vehicle."));
		}
		Strategy->StopFaction(RTSTeams::Enemy);
		CommandVehicle->GetOrderComponent()->Cancel();

		const FRTSAIProfile Profile = FRTSMilestone3Configuration::Load().FastTest;
		const FRTSHQCandidateSearchResult Baseline = Strategy->EvaluateHeadquartersCandidates(
			*CommandVehicle,
			Profile);
		if (!Test->TestTrue(TEXT("The real battlefield has a legal HQ candidate"), Baseline.bFound))
		{
			return true;
		}
		const bool bHasEdgeRefusal = Baseline.Candidates.ContainsByPredicate(
			[](const FRTSHQCandidateReceipt& Candidate)
			{
				return Candidate.DeploymentRefusal == ERTSDeploymentRefusal::OutsideStartingZone;
			});
		Test->TestTrue(TEXT("Complete-footprint legality rejects edge candidates"), bHasEdgeRefusal);

		FRTSAIProfile TiedProfile = Profile;
		TiedProfile.MaterialProximityWeight = 0.0f;
		TiedProfile.BuildSpaceWeight = 0.0f;
		TiedProfile.ApproachStandoffWeight = 0.0f;
		TiedProfile.RouteAccessWeight = 0.0f;
		const FRTSHQCandidateSearchResult Tied = Strategy->EvaluateHeadquartersCandidates(
			*CommandVehicle,
			TiedProfile);
		int32 FirstSelectableIndex = INDEX_NONE;
		for (const FRTSHQCandidateReceipt& Candidate : Tied.Candidates)
		{
			if (Candidate.bSelectable)
			{
				FirstSelectableIndex = Candidate.CandidateIndex;
				break;
			}
		}
		Test->TestTrue(TEXT("A tied candidate set remains selectable"), Tied.bFound);
		Test->TestEqual(
			TEXT("Equal scores break by stable candidate index"),
			Tied.Selected.CandidateIndex,
			FirstSelectableIndex);

		TWeakObjectPtr<AActor> Blocker =
			RTSMilestone3AIDeploymentTestPrivate::SpawnQueryOnlyHeadquartersBlocker(
				*World,
				Baseline.Selected.GroundLocation);
		if (!Test->TestTrue(TEXT("A query-only blocked-site fixture spawns"), Blocker.IsValid()))
		{
			return true;
		}
		const FRTSHQCandidateSearchResult Blocked = Strategy->EvaluateHeadquartersCandidates(
			*CommandVehicle,
			Profile);
		const FRTSHQCandidateReceipt* RejectedPreferred = Blocked.Candidates.FindByPredicate(
			[&Baseline](const FRTSHQCandidateReceipt& Candidate)
			{
				return Candidate.CandidateIndex == Baseline.Selected.CandidateIndex;
			});
		Test->TestNotNull(TEXT("The preferred candidate keeps its stable receipt"), RejectedPreferred);
		if (RejectedPreferred != nullptr)
		{
			Test->TestEqual(
				TEXT("The real deployment evaluator reports the blocking refusal"),
				RejectedPreferred->DeploymentRefusal,
				ERTSDeploymentRefusal::FootprintBlocked);
		}
		Test->TestTrue(TEXT("A blocked preferred candidate selects another legal site"), Blocked.bFound);
		Test->TestNotEqual(
			TEXT("The next selection has a different stable candidate index"),
			Blocked.Selected.CandidateIndex,
			Baseline.Selected.CandidateIndex);
		RTSMilestone3AIDeploymentTestPrivate::RemoveBlocker(Blocker);
		return true;
	}

private:
	bool FinishIfTimedOut(const TCHAR* Failure)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		Test->AddError(Failure);
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	double Deadline = 0.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3HQCandidateTest,
	"Task0168.Headless.RTS.Milestone3.AI.HQCandidate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone3HQCandidateTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(
		TEXT("The skirmish opens for candidate evaluation"),
		AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSMilestone3HQCandidateCommand(
		this,
		FPlatformTime::Seconds() + 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

class FRTSMilestone3AIDeploymentCommand final : public IAutomationLatentCommand
{
public:
	FRTSMilestone3AIDeploymentCommand(FAutomationTestBase* InTest, const double InDeadline)
		: Test(InTest), Deadline(InDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = RTSMilestone3AIDeploymentTestPrivate::FindPIEWorld();
		if (World == nullptr)
		{
			return FinishIfTimedOut(TEXT("PIE did not expose the M3 deployment world."));
		}
		URTSAIStrategySubsystem* Strategy = World->GetSubsystem<URTSAIStrategySubsystem>();
		URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>();
		URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>();
		if (Strategy == nullptr || Structures == nullptr || Economy == nullptr)
		{
			return FinishIfTimedOut(TEXT("The deployment fixture is missing an authoritative subsystem."));
		}

		if (!bStarted)
		{
			ARTSCombatUnit* CommandVehicle = RTSMilestone3AIDeploymentTestPrivate::FindEnemyCommandVehicle(*World);
			if (CommandVehicle == nullptr)
			{
				return FinishIfTimedOut(TEXT("The enemy Command Vehicle did not register."));
			}
			Strategy->StopFaction(RTSTeams::Enemy);
			CommandVehicle->GetOrderComponent()->Cancel();
			InitialCommandVehicleLocation = CommandVehicle->GetActorLocation();
			InitialEconomy = Economy->GetSnapshot(RTSTeams::Enemy);
			FRTSAIProfile Profile = FRTSMilestone3Configuration::Load().FastTest;
			Profile.RetryCooldownSeconds = 0.5f;
			const FRTSAIStartResult Start = Strategy->StartFaction(RTSTeams::Enemy, Profile);
			if (!Test->TestTrue(TEXT("The enemy strategy starts through its public seam"), Start.bAccepted)
				|| !Test->TestEqual(
					TEXT("The first commitment begins real travel"),
					Start.Snapshot.HeadquartersPhase,
					ERTSAIHeadquartersPhase::Traveling))
			{
				return true;
			}
			FirstCandidateIndex = Start.Snapshot.CandidateIndex;
			CurrentObjective = Start.Snapshot.ObjectiveLocation;
			Blocker = RTSMilestone3AIDeploymentTestPrivate::SpawnQueryOnlyHeadquartersBlocker(
				*World,
				CurrentObjective);
			if (!Test->TestTrue(TEXT("The post-command blocker spawns"), Blocker.IsValid()))
			{
				return true;
			}
			bStarted = true;
			return false;
		}

		const FRTSAISnapshot Snapshot = Strategy->GetSnapshot(RTSTeams::Enemy);
		ARTSCombatUnit* CommandVehicle = RTSMilestone3AIDeploymentTestPrivate::FindEnemyCommandVehicle(*World);
		if (CommandVehicle != nullptr)
		{
			const float DistanceMoved = FVector::Dist2D(
				InitialCommandVehicleLocation,
				CommandVehicle->GetActorLocation());
			const float RemainingDistance = FVector::Dist2D(
				CommandVehicle->GetActorLocation(),
				CurrentObjective);
			bSawIntermediateTravel |= DistanceMoved > 75.0f
				&& RemainingDistance > FRTSMilestone3Configuration::Load().FastTest.HeadquartersArrivalTolerance;
		}

		if (!bSawRejectedDeployment
			&& Snapshot.HeadquartersPhase == ERTSAIHeadquartersPhase::Retrying)
		{
			bSawRejectedDeployment = true;
			Test->TestEqual(
				TEXT("The newly blocked site publishes the authoritative refusal"),
				Snapshot.LastDeploymentRefusal,
				ERTSDeploymentRefusal::FootprintBlocked);
			RTSMilestone3AIDeploymentTestPrivate::RemoveBlocker(Blocker);
		}
		if (Snapshot.HeadquartersPhase == ERTSAIHeadquartersPhase::Traveling
			&& Snapshot.CandidateIndex != FirstCandidateIndex)
		{
			bSawSecondCandidate = true;
			CurrentObjective = Snapshot.ObjectiveLocation;
		}

		const TArray<FRTSStructureSnapshot> Headquarters = Structures->Query(
			RTSTeams::Enemy,
			ERTSStructureType::Headquarters);
		if (!Headquarters.IsEmpty())
		{
			RTSMilestone3AIDeploymentTestPrivate::RemoveBlocker(Blocker);
			Test->TestEqual(TEXT("Exactly one enemy HQ exists"), Headquarters.Num(), 1);
			Test->TestTrue(TEXT("The Command Vehicle visibly occupied an intermediate location"), bSawIntermediateTravel);
			Test->TestTrue(TEXT("The newly blocked first site was rejected"), bSawRejectedDeployment);
			Test->TestTrue(TEXT("A stable second candidate received a normal move"), bSawSecondCandidate);
			Test->TestEqual(
				TEXT("The strategy records one accepted deployment"),
				Snapshot.AcceptedDeploymentCount,
				1);
			Test->TestEqual(
				TEXT("One rejected and one accepted deployment were attempted"),
				Snapshot.DeploymentAttemptCount,
				2);
			Test->TestEqual(
				TEXT("The successful transition reaches the deployed phase"),
				Snapshot.HeadquartersPhase,
				ERTSAIHeadquartersPhase::Deployed);
			const FRTSEconomySnapshot FinalEconomy = Economy->GetSnapshot(RTSTeams::Enemy);
			Test->TestEqual(
				TEXT("Zero-cost HQ deployment does not inject or spend Materials"),
				FinalEconomy.Materials,
				InitialEconomy.Materials);
			Test->TestEqual(
				TEXT("The HQ contributes Power through the normal structure transaction"),
				FinalEconomy.PowerGeneration,
				FRTSMilestone2Configuration::Load().Headquarters.PowerGeneration);
			return true;
		}
		return FinishIfTimedOut(TEXT("The enemy did not retry and deploy its HQ before the deadline."));
	}

private:
	bool FinishIfTimedOut(const TCHAR* Failure)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		RTSMilestone3AIDeploymentTestPrivate::RemoveBlocker(Blocker);
		Test->AddError(Failure);
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	double Deadline = 0.0;
	bool bStarted = false;
	bool bSawIntermediateTravel = false;
	bool bSawRejectedDeployment = false;
	bool bSawSecondCandidate = false;
	int32 FirstCandidateIndex = INDEX_NONE;
	FVector InitialCommandVehicleLocation = FVector::ZeroVector;
	FVector CurrentObjective = FVector::ZeroVector;
	FRTSEconomySnapshot InitialEconomy;
	TWeakObjectPtr<AActor> Blocker;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3AIDeploymentTest,
	"Task0168.Headless.RTS.Milestone3.AI.Deployment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone3AIDeploymentTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(
		TEXT("The skirmish opens for enemy HQ deployment"),
		AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSMilestone3AIDeploymentCommand(
		this,
		FPlatformTime::Seconds() + 45.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

class FRTSMilestone3RenderedDeploymentCommand final : public IAutomationLatentCommand
{
public:
	FRTSMilestone3RenderedDeploymentCommand(FAutomationTestBase* InTest, const double InDeadline)
		: Test(InTest), Deadline(InDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = RTSMilestone3AIDeploymentTestPrivate::FindPIEWorld();
		if (World == nullptr)
		{
			return FinishIfTimedOut(TEXT("PIE did not expose the rendered deployment world."));
		}
		if (!Initialize(*World))
		{
			return FinishIfTimedOut(TEXT("The rendered deployment fixture did not initialize."));
		}
		PinCamera();
		if (bCapturePending)
		{
			if (IFileManager::Get().FileSize(*PendingCapturePath) > 0)
			{
				bCapturePending = false;
				Stage = StageAfterCapture;
			}
			return false;
		}

		URTSAIStrategySubsystem* Strategy = World->GetSubsystem<URTSAIStrategySubsystem>();
		URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>();
		ARTSCombatUnit* CommandVehicle = RTSMilestone3AIDeploymentTestPrivate::FindEnemyCommandVehicle(*World);
		const TArray<FRTSStructureSnapshot> Headquarters = Structures != nullptr
			? Structures->Query(RTSTeams::Enemy, ERTSStructureType::Headquarters)
			: TArray<FRTSStructureSnapshot>();
		const FRTSAISnapshot Snapshot = Strategy != nullptr
			? Strategy->GetSnapshot(RTSTeams::Enemy)
			: FRTSAISnapshot();
		const float DistanceMoved = CommandVehicle != nullptr
			? FVector::Dist2D(InitialCommandVehicleLocation, CommandVehicle->GetActorLocation())
			: 0.0f;
		const float RemainingDistance = CommandVehicle != nullptr
			? FVector::Dist2D(CommandVehicle->GetActorLocation(), ObjectiveLocation)
			: 0.0f;

		switch (Stage)
		{
		case ECaptureStage::Candidate:
			if (FPlatformTime::Seconds() >= CandidateCaptureTime)
			{
				RequestCapture(TEXT("M3Slice1-Candidate.png"), ECaptureStage::Travel);
			}
			break;
		case ECaptureStage::Travel:
			if (DistanceMoved > 1000.0f && RemainingDistance > 1000.0f)
			{
				RequestCapture(TEXT("M3Slice1-Travel.png"), ECaptureStage::Arrival);
			}
			break;
		case ECaptureStage::Arrival:
			if (CommandVehicle != nullptr && RemainingDistance < 450.0f && Headquarters.IsEmpty())
			{
				RequestCapture(TEXT("M3Slice1-Arrival.png"), ECaptureStage::Deployment);
			}
			break;
		case ECaptureStage::Deployment:
			if (!Headquarters.IsEmpty()
				&& Snapshot.HeadquartersPhase == ERTSAIHeadquartersPhase::Deployed
				&& Snapshot.AcceptedDeploymentCount == 1)
			{
				RequestCapture(TEXT("M3Slice1-Deployed.png"), ECaptureStage::Complete);
			}
			break;
		case ECaptureStage::Complete:
			Test->AddInfo(TEXT("Rendered enemy HQ sequence: M3Slice1-Candidate.png, M3Slice1-Travel.png, M3Slice1-Arrival.png, M3Slice1-Deployed.png"));
			return true;
		}
		return FinishIfTimedOut(TEXT("The rendered enemy deployment sequence did not complete."));
	}

private:
	enum class ECaptureStage : uint8
	{
		Candidate,
		Travel,
		Arrival,
		Deployment,
		Complete
	};

	bool Initialize(UWorld& World)
	{
		if (bInitialized)
		{
			return true;
		}
		URTSAIStrategySubsystem* Strategy = World.GetSubsystem<URTSAIStrategySubsystem>();
		ARTSCombatUnit* CommandVehicle = RTSMilestone3AIDeploymentTestPrivate::FindEnemyCommandVehicle(World);
		APlayerController* Controller = World.GetFirstPlayerController();
		Camera = Controller != nullptr ? Cast<ARTSCameraPawn>(Controller->GetPawn()) : nullptr;
		URTSWorldOverlaySubsystem* Overlays = World.GetSubsystem<URTSWorldOverlaySubsystem>();
		if (Strategy == nullptr || CommandVehicle == nullptr || !Camera.IsValid() || Overlays == nullptr)
		{
			return false;
		}
		Strategy->StopFaction(RTSTeams::Enemy);
		CommandVehicle->GetOrderComponent()->Cancel();
		InitialCommandVehicleLocation = CommandVehicle->GetActorLocation();
		FRTSAIProfile Profile = FRTSMilestone3Configuration::Load().FastTest;
		Profile.DecisionIntervalSeconds = 0.5f;
		Profile.HeadquartersCandidateGridSpacing = 2000.0f;
		Profile.MaterialProximityWeight = 0.0f;
		Profile.BuildSpaceWeight = 0.0f;
		Profile.ApproachStandoffWeight = 0.0f;
		Profile.RouteAccessWeight = 0.0f;
		const FRTSAIStartResult Start = Strategy->StartFaction(RTSTeams::Enemy, Profile);
		if (!Start.bAccepted || Start.Snapshot.HeadquartersPhase != ERTSAIHeadquartersPhase::Traveling)
		{
			Test->AddError(TEXT("The rendered test could not start a real HQ travel commitment."));
			return false;
		}
		ObjectiveLocation = Start.Snapshot.ObjectiveLocation;
		FRTSWorldOverlayDescriptor Objective;
		Objective.Mode = ERTSWorldOverlayMode::PlacementFootprint;
		Objective.SourceKind = ERTSWorldOverlaySourceKind::Showcase;
		Objective.StableSourceId = 91;
		Objective.WorldTransform.SetLocation(ObjectiveLocation);
		Objective.HalfExtent = FVector2D(500.0f, 500.0f);
		Objective.Validity = ERTSWorldOverlayValidity::Valid;
		Objective.TeamId = RTSTeams::Enemy;
		if (!Overlays->Submit(Objective).bAccepted)
		{
			Test->AddError(TEXT("The rendered test could not submit its typed objective overlay."));
			return false;
		}
		CameraLocation = (InitialCommandVehicleLocation + ObjectiveLocation) * 0.5f;
		CameraLocation.Z = 100.0f;
		Camera->AddZoomInput(-100.0f);
		CandidateCaptureTime = FPlatformTime::Seconds() + 0.25;
		bInitialized = true;
		return true;
	}

	void PinCamera() const
	{
		if (Camera.IsValid())
		{
			Camera->SetKeyboardPanInput(FVector2D::ZeroVector);
			Camera->SetEdgePanInput(FVector2D::ZeroVector);
			Camera->SetActorLocation(CameraLocation);
		}
	}

	void RequestCapture(const TCHAR* Filename, const ECaptureStage NextStage)
	{
		PendingCapturePath = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("Screenshots/RTS"),
			Filename);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PendingCapturePath), true);
		IFileManager::Get().Delete(*PendingCapturePath, false, true);
		FScreenshotRequest::RequestScreenshot(PendingCapturePath, true, false);
		StageAfterCapture = NextStage;
		bCapturePending = true;
	}

	bool FinishIfTimedOut(const TCHAR* Failure)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		Test->AddError(Failure);
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	double Deadline = 0.0;
	double CandidateCaptureTime = 0.0;
	bool bInitialized = false;
	bool bCapturePending = false;
	ECaptureStage Stage = ECaptureStage::Candidate;
	ECaptureStage StageAfterCapture = ECaptureStage::Candidate;
	FString PendingCapturePath;
	FVector InitialCommandVehicleLocation = FVector::ZeroVector;
	FVector ObjectiveLocation = FVector::ZeroVector;
	FVector CameraLocation = FVector::ZeroVector;
	TWeakObjectPtr<ARTSCameraPawn> Camera;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3RenderedDeploymentTest,
	"Task0168.Rendered.RTS.Milestone3.AI.DeploymentRendered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone3RenderedDeploymentTest::RunTest(const FString& Parameters)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
	{
		AddInfo(TEXT("NullRHI skips the rendered deployment sequence; deterministic deployment remains covered separately."));
		return true;
	}
	if (!TestTrue(
		TEXT("The skirmish opens for rendered enemy deployment"),
		AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSMilestone3RenderedDeploymentCommand(
		this,
		FPlatformTime::Seconds() + 45.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/RTSHealthComponent.h"
#include "Algo/Reverse.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Structures/RTSStructure.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/RTSCameraPawn.h"
#include "Game/RTSPlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Orders/RTSOrderTypes.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Selection/RTSSelectionSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

#if WITH_DEV_AUTOMATION_TESTS

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FRTSVerifyMoveDispatchCommand,
	FAutomationTestBase*, Test,
	double, DeadlineSeconds);

bool FRTSVerifyMoveDispatchCommand::Update()
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (WorldContext.WorldType != EWorldType::PIE || World == nullptr)
		{
			continue;
		}

		ARTSPlayerController* Controller = Cast<ARTSPlayerController>(World->GetFirstPlayerController());
		ULocalPlayer* LocalPlayer = Controller != nullptr ? Controller->GetLocalPlayer() : nullptr;
		URTSSelectionSubsystem* Selection = LocalPlayer != nullptr
			? LocalPlayer->GetSubsystem<URTSSelectionSubsystem>()
			: nullptr;
		URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>();
		if (Selection == nullptr || Commands == nullptr)
		{
			break;
		}

		TArray<ARTSCombatUnit*> PlayerUnits;
		for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
		{
			if (UnitIterator->GetGenericTeamId() == RTSTeams::Player)
			{
				PlayerUnits.Add(*UnitIterator);
			}
		}
		PlayerUnits.Sort([](const ARTSCombatUnit& Left, const ARTSCombatUnit& Right)
		{
			return Left.GetStableUnitId() < Right.GetStableUnitId();
		});
		if (!Test->TestEqual(TEXT("PIE exposes the twelve-unit movement trial group"), PlayerUnits.Num(), 12))
		{
			return true;
		}
		PlayerUnits.SetNum(5);

		Selection->ReplaceWith(PlayerUnits);
		const FRTSCommandResult Result = Commands->IssueMove(
			Selection->GetLivingUnits(),
			FVector(0.0f, 2500.0f, 0.0f));
		Test->TestTrue(TEXT("A reachable group move receives a command ID"), Result.GroupCommandId > 0);
		Test->TestEqual(TEXT("The group command returns one outcome per selected unit"), Result.Outcomes.Num(), 5);
		Test->TestEqual(TEXT("Every selected unit accepts the reachable move"), Result.GetAcceptedCount(), 5);

		TSet<FVector> AssignedDestinations;
		for (const FRTSUnitCommandOutcome& Outcome : Result.Outcomes)
		{
			Test->TestTrue(TEXT("Every movement outcome is accepted"), Outcome.bAccepted);
			AssignedDestinations.Add(Outcome.AssignedDestination);

			const ARTSCombatUnit* Unit = Outcome.Unit;
			const URTSUnitOrderComponent* OrderComponent = Unit != nullptr ? Unit->GetOrderComponent() : nullptr;
			if (!Test->TestNotNull(TEXT("Every assigned unit owns an order component"), OrderComponent))
			{
				continue;
			}

			const FRTSOrderSnapshot Snapshot = OrderComponent->GetSnapshot();
			Test->TestEqual(TEXT("Every unit reports the shared group command ID"), Snapshot.GroupCommandId, Result.GroupCommandId);
			Test->TestEqual(TEXT("Every unit reports a move order"), Snapshot.Kind, ERTSOrderKind::Move);
			Test->TestTrue(
				TEXT("A dispatched move is active or already complete"),
				Snapshot.Phase == ERTSOrderPhase::Moving || Snapshot.Phase == ERTSOrderPhase::Idle);
		}
		Test->TestEqual(TEXT("Every selected unit receives a unique destination slot"), AssignedDestinations.Num(), 5);
		return true;
	}

	if (FPlatformTime::Seconds() >= DeadlineSeconds)
	{
		Test->AddError(TEXT("PIE did not expose the movement command seam within ten seconds."));
		return true;
	}
	return false;
}

// REQUIRED: Observe production interfaces in the real navigable sandbox.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1MoveDispatchTest,
	"Task0168.Headless.GameEngineBench.UE0159.Orders.Dispatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone1MoveDispatchTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR SandboxMapPackage[] = TEXT("/Game/RTS/Maps/M1_CombatSandbox");
	if (!TestTrue(TEXT("The sandbox opens before move dispatch starts"), AutomationOpenMap(SandboxMapPackage, true)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyMoveDispatchCommand(this, FPlatformTime::Seconds() + 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FRTSVerifyMoveReplacementCommand,
	FAutomationTestBase*, Test,
	double, DeadlineSeconds);

bool FRTSVerifyMoveReplacementCommand::Update()
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (WorldContext.WorldType != EWorldType::PIE || World == nullptr)
		{
			continue;
		}

		URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>();
		ARTSCombatUnit* PlayerUnit = nullptr;
		for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
		{
			if (UnitIterator->GetGenericTeamId() == RTSTeams::Player
				&& (PlayerUnit == nullptr || UnitIterator->GetStableUnitId() < PlayerUnit->GetStableUnitId()))
			{
				PlayerUnit = *UnitIterator;
			}
		}
		if (Commands == nullptr || PlayerUnit == nullptr)
		{
			break;
		}

		ARTSCombatUnit* Units[] = {PlayerUnit};
		const FRTSCommandResult FirstMove = Commands->IssueMove(Units, FVector(-1000.0f, 1500.0f, 0.0f));
		const FRTSCommandResult ReplacementMove = Commands->IssueMove(Units, FVector(1000.0f, 2500.0f, 0.0f));
		Test->TestEqual(TEXT("The initial move is accepted"), FirstMove.GetAcceptedCount(), 1);
		Test->TestEqual(TEXT("The replacement move is accepted"), ReplacementMove.GetAcceptedCount(), 1);
		Test->TestTrue(
			TEXT("A replacement receives a newer world command ID"),
			ReplacementMove.GroupCommandId > FirstMove.GroupCommandId);

		const FRTSOrderSnapshot Snapshot = PlayerUnit->GetOrderComponent()->GetSnapshot();
		Test->TestEqual(
			TEXT("The unit reports only the replacement command ID"),
			Snapshot.GroupCommandId,
			ReplacementMove.GroupCommandId);
		if (!ReplacementMove.Outcomes.IsEmpty())
		{
			Test->TestTrue(
				TEXT("The unit reports only the replacement destination"),
				Snapshot.Destination.Equals(ReplacementMove.Outcomes[0].AssignedDestination, 1.0f));
		}
		return true;
	}

	if (FPlatformTime::Seconds() >= DeadlineSeconds)
	{
		Test->AddError(TEXT("PIE did not expose the move replacement seam within ten seconds."));
		return true;
	}
	return false;
}

// REQUIRED: Observe production interfaces in the real navigable sandbox.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1MoveReplacementTest,
	"Task0168.Headless.GameEngineBench.UE0159.Orders.Replace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone1MoveReplacementTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR SandboxMapPackage[] = TEXT("/Game/RTS/Maps/M1_CombatSandbox");
	if (!TestTrue(TEXT("The sandbox opens before move replacement starts"), AutomationOpenMap(SandboxMapPackage, true)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyMoveReplacementCommand(this, FPlatformTime::Seconds() + 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

class FRTSVerifyMoveArrivalCommand final : public IAutomationLatentCommand
{
public:
	FRTSVerifyMoveArrivalCommand(FAutomationTestBase* InTest, const double InDeadlineSeconds)
		: Test(InTest)
		, DeadlineSeconds(InDeadlineSeconds)
	{
	}

	virtual bool Update() override
	{
		if (!MovingUnit.IsValid())
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
				return FinishIfTimedOut(TEXT("PIE did not expose an arrival-test world within ten seconds."));
			}

			ARTSCombatUnit* FirstPlayerUnit = nullptr;
			for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
			{
				if (UnitIterator->GetGenericTeamId() == RTSTeams::Player
					&& (FirstPlayerUnit == nullptr || UnitIterator->GetStableUnitId() < FirstPlayerUnit->GetStableUnitId()))
				{
					FirstPlayerUnit = *UnitIterator;
				}
			}
			URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>();
			if (FirstPlayerUnit == nullptr || Commands == nullptr || FirstPlayerUnit->GetController() == nullptr)
			{
				return FinishIfTimedOut(TEXT("The arrival fixture did not receive its native unit controller."));
			}

			MovingUnit = FirstPlayerUnit;
			InitialLocation = FirstPlayerUnit->GetActorLocation();
			ARTSCombatUnit* Units[] = {FirstPlayerUnit};
			const FRTSCommandResult Result = Commands->IssueMove(
				Units,
				FVector(InitialLocation.X, InitialLocation.Y + 2000.0f, 0.0f));
			if (!Test->TestEqual(TEXT("The open-field arrival move is accepted"), Result.GetAcceptedCount(), 1)
				|| Result.Outcomes.IsEmpty())
			{
				return true;
			}
			AssignedDestination = Result.Outcomes[0].AssignedDestination;
			return false;
		}

		const FVector CurrentLocation = MovingUnit->GetActorLocation();
		bObservedProgress |= FVector::DistSquared2D(CurrentLocation, InitialLocation) >= FMath::Square(300.0f);
		const FRTSOrderSnapshot Snapshot = MovingUnit->GetOrderComponent()->GetSnapshot();
		if (Snapshot.Phase == ERTSOrderPhase::Idle)
		{
			Test->TestTrue(TEXT("The moving unit makes measurable progress"), bObservedProgress);
			Test->TestTrue(
				TEXT("The moving unit stops within arrival tolerance"),
				FVector::DistSquared2D(CurrentLocation, AssignedDestination) <= FMath::Square(120.0f));
			Test->TestEqual(TEXT("A completed move has no failure"), Snapshot.LastFailure, ERTSOrderFailure::None);
			return true;
		}

		return FinishIfTimedOut(TEXT("The open-field move did not arrive within ten seconds."));
	}

private:
	bool FinishIfTimedOut(const TCHAR* Message)
	{
		if (FPlatformTime::Seconds() < DeadlineSeconds)
		{
			return false;
		}
		Test->AddError(Message);
		return true;
	}

	FAutomationTestBase* Test;
	double DeadlineSeconds;
	TWeakObjectPtr<ARTSCombatUnit> MovingUnit;
	FVector InitialLocation = FVector::ZeroVector;
	FVector AssignedDestination = FVector::ZeroVector;
	bool bObservedProgress = false;
};

// REQUIRED: Observe production interfaces in the real navigable sandbox.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1MoveArrivalTest,
	"Task0168.Headless.GameEngineBench.UE0159.Movement.Arrival",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone1MoveArrivalTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR SandboxMapPackage[] = TEXT("/Game/RTS/Maps/M1_CombatSandbox");
	if (!TestTrue(TEXT("The sandbox opens before movement arrival starts"), AutomationOpenMap(SandboxMapPackage, true)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyMoveArrivalCommand(this, FPlatformTime::Seconds() + 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

class FRTSVerifyBlockedRouteCommand final : public IAutomationLatentCommand
{
public:
	FRTSVerifyBlockedRouteCommand(FAutomationTestBase* InTest, const double InDeadlineSeconds)
		: Test(InTest)
		, DeadlineSeconds(InDeadlineSeconds)
	{
	}

	virtual bool Update() override
	{
		if (!MovingUnit.IsValid())
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
				return FinishIfTimedOut(TEXT("PIE did not expose a blocker-route world within twenty-five seconds."));
			}

			ARTSCombatUnit* FirstPlayerUnit = nullptr;
			for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
			{
				if (UnitIterator->GetGenericTeamId() == RTSTeams::Player
					&& (FirstPlayerUnit == nullptr || UnitIterator->GetStableUnitId() < FirstPlayerUnit->GetStableUnitId()))
				{
					FirstPlayerUnit = *UnitIterator;
				}
			}
			URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>();
			if (FirstPlayerUnit == nullptr || Commands == nullptr || FirstPlayerUnit->GetController() == nullptr)
			{
				return FinishIfTimedOut(TEXT("The blocker-route fixture did not receive its native unit controller."));
			}

			const FVector RouteStart(-5000.0f, -3000.0f, 80.0f);
			if (!Test->TestTrue(TEXT("The blocker-route unit teleports to its deterministic start"), FirstPlayerUnit->TeleportTo(RouteStart, FRotator::ZeroRotator)))
			{
				return true;
			}
			MovingUnit = FirstPlayerUnit;
			InitialLocation = RouteStart;
			ARTSCombatUnit* Units[] = {FirstPlayerUnit};
			const FRTSCommandResult Result = Commands->IssueMove(Units, FVector(-5000.0f, 3000.0f, 0.0f));
			if (!Test->TestEqual(TEXT("The route-around-blocker move is accepted"), Result.GetAcceptedCount(), 1)
				|| Result.Outcomes.IsEmpty())
			{
				return true;
			}
			AssignedDestination = Result.Outcomes[0].AssignedDestination;
			return false;
		}

		const FVector CurrentLocation = MovingUnit->GetActorLocation();
		bObservedProgress |= FVector::DistSquared2D(CurrentLocation, InitialLocation) >= FMath::Square(300.0f);
		const bool bInsideBlockedRectangle = CurrentLocation.X >= -8000.0f
			&& CurrentLocation.X <= -1500.0f
			&& FMath::Abs(CurrentLocation.Y) <= 600.0f;
		if (bInsideBlockedRectangle)
		{
			Test->AddError(FString::Printf(
				TEXT("The unit entered blocked navigation at %s."),
				*CurrentLocation.ToCompactString()));
			return true;
		}

		const FRTSOrderSnapshot Snapshot = MovingUnit->GetOrderComponent()->GetSnapshot();
		if (Snapshot.Phase == ERTSOrderPhase::Idle)
		{
			Test->TestTrue(TEXT("The blocker-route unit makes measurable progress"), bObservedProgress);
			Test->TestTrue(
				TEXT("The blocker-route unit reaches the far side within tolerance"),
				FVector::DistSquared2D(CurrentLocation, AssignedDestination) <= FMath::Square(120.0f));
			Test->TestEqual(TEXT("The completed blocker route has no failure"), Snapshot.LastFailure, ERTSOrderFailure::None);
			return true;
		}

		return FinishIfTimedOut(TEXT("The route around the blocker did not complete within twenty-five seconds."));
	}

private:
	bool FinishIfTimedOut(const TCHAR* Message)
	{
		if (FPlatformTime::Seconds() < DeadlineSeconds)
		{
			return false;
		}
		Test->AddError(Message);
		return true;
	}

	FAutomationTestBase* Test;
	double DeadlineSeconds;
	TWeakObjectPtr<ARTSCombatUnit> MovingUnit;
	FVector InitialLocation = FVector::ZeroVector;
	FVector AssignedDestination = FVector::ZeroVector;
	bool bObservedProgress = false;
};

// REQUIRED: Observe production interfaces in the real navigable sandbox.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1MoveBlockedTest,
	"Task0168.Headless.GameEngineBench.UE0159.Movement.Blocked",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone1MoveBlockedTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR SandboxMapPackage[] = TEXT("/Game/RTS/Maps/M1_CombatSandbox");
	if (!TestTrue(TEXT("The sandbox opens before blocker routing starts"), AutomationOpenMap(SandboxMapPackage, true)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyBlockedRouteCommand(this, FPlatformTime::Seconds() + 60.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FRTSVerifyMoveFailureCommand,
	FAutomationTestBase*, Test,
	double, DeadlineSeconds);

bool FRTSVerifyMoveFailureCommand::Update()
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (WorldContext.WorldType != EWorldType::PIE || World == nullptr)
		{
			continue;
		}

		URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>();
		ARTSCombatUnit* PlayerUnit = nullptr;
		for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
		{
			if (UnitIterator->GetGenericTeamId() == RTSTeams::Player
				&& (PlayerUnit == nullptr || UnitIterator->GetStableUnitId() < PlayerUnit->GetStableUnitId()))
			{
				PlayerUnit = *UnitIterator;
			}
		}
		if (Commands == nullptr || PlayerUnit == nullptr)
		{
			break;
		}

		ARTSCombatUnit* Units[] = {PlayerUnit};
		const FRTSCommandResult InitialMove = Commands->IssueMove(Units, FVector(0.0f, -2500.0f, 0.0f));
		Test->TestEqual(TEXT("The setup move is accepted before failure replacement"), InitialMove.GetAcceptedCount(), 1);
		const FRTSCommandResult FailedMove = Commands->IssueMove(Units, FVector(30000.0f, 30000.0f, 0.0f));
		Test->TestEqual(TEXT("An off-arena destination accepts no units"), FailedMove.GetAcceptedCount(), 0);
		Test->TestEqual(
			TEXT("An off-arena destination reports navigation projection failure"),
			FailedMove.Failure,
			ERTSOrderFailure::DestinationNotNavigable);
		Test->TestEqual(TEXT("The failed request still reports one unit outcome"), FailedMove.Outcomes.Num(), 1);

		const FRTSOrderSnapshot Snapshot = PlayerUnit->GetOrderComponent()->GetSnapshot();
		Test->TestEqual(TEXT("A failed replacement does not preserve the prior move kind"), Snapshot.Kind, ERTSOrderKind::None);
		Test->TestEqual(TEXT("A failed replacement leaves the unit idle"), Snapshot.Phase, ERTSOrderPhase::Idle);
		Test->TestEqual(TEXT("A failed replacement clears the prior command ID"), Snapshot.GroupCommandId, int64{0});
		return true;
	}

	if (FPlatformTime::Seconds() >= DeadlineSeconds)
	{
		Test->AddError(TEXT("PIE did not expose the move-failure seam within ten seconds."));
		return true;
	}
	return false;
}

// REQUIRED: Observe production interfaces in the real navigable sandbox.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1MoveFailureTest,
	"Task0168.Headless.GameEngineBench.UE0159.Movement.Failure",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone1MoveFailureTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR SandboxMapPackage[] = TEXT("/Game/RTS/Maps/M1_CombatSandbox");
	if (!TestTrue(TEXT("The sandbox opens before movement failure starts"), AutomationOpenMap(SandboxMapPackage, true)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyMoveFailureCommand(this, FPlatformTime::Seconds() + 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FRTSVerifyFormationAssignmentCommand,
	FAutomationTestBase*, Test,
	double, DeadlineSeconds);

bool FRTSVerifyFormationAssignmentCommand::Update()
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (WorldContext.WorldType != EWorldType::PIE || World == nullptr)
		{
			continue;
		}

		URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>();
		TArray<ARTSCombatUnit*> PlayerUnits;
		for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
		{
			if (UnitIterator->GetGenericTeamId() == RTSTeams::Player)
			{
				PlayerUnits.Add(*UnitIterator);
			}
		}
		PlayerUnits.Sort([](const ARTSCombatUnit& Left, const ARTSCombatUnit& Right)
		{
			return Left.GetStableUnitId() < Right.GetStableUnitId();
		});
		if (Commands == nullptr || PlayerUnits.Num() != 12)
		{
			break;
		}

		TMap<int32, FVector> InitialLocations;
		for (ARTSCombatUnit* Unit : PlayerUnits)
		{
			InitialLocations.Add(Unit->GetStableUnitId(), Unit->GetActorLocation());
		}
		const FVector RequestedDestination(0.0f, 2500.0f, 0.0f);
		const FRTSCommandResult FirstAssignment = Commands->IssueMove(PlayerUnits, RequestedDestination);
		if (!Test->TestEqual(TEXT("The first formation assigns all twelve units"), FirstAssignment.GetAcceptedCount(), 12))
		{
			return true;
		}

		for (int32 LeftIndex = 0; LeftIndex < FirstAssignment.Outcomes.Num(); ++LeftIndex)
		{
			for (int32 RightIndex = LeftIndex + 1; RightIndex < FirstAssignment.Outcomes.Num(); ++RightIndex)
			{
				Test->TestTrue(
					TEXT("Formation slots preserve collision-radius spacing"),
					FVector::DistSquared2D(
						FirstAssignment.Outcomes[LeftIndex].AssignedDestination,
						FirstAssignment.Outcomes[RightIndex].AssignedDestination)
					>= FMath::Square(190.0f));
			}
		}

		TMap<int32, FVector> FirstDestinations;
		for (const FRTSUnitCommandOutcome& Outcome : FirstAssignment.Outcomes)
		{
			FirstDestinations.Add(Outcome.Unit->GetStableUnitId(), Outcome.AssignedDestination);
			Outcome.Unit->GetOrderComponent()->Cancel();
			Outcome.Unit->TeleportTo(InitialLocations.FindChecked(Outcome.Unit->GetStableUnitId()), FRotator::ZeroRotator);
		}

		Algo::Reverse(PlayerUnits);
		const FRTSCommandResult RepeatedAssignment = Commands->IssueMove(PlayerUnits, RequestedDestination);
		Test->TestEqual(TEXT("The repeated formation assigns all twelve units"), RepeatedAssignment.GetAcceptedCount(), 12);
		for (const FRTSUnitCommandOutcome& Outcome : RepeatedAssignment.Outcomes)
		{
			const FVector* FirstDestination = FirstDestinations.Find(Outcome.Unit->GetStableUnitId());
			Test->TestTrue(
				TEXT("A fixed setup repeats each stable unit's destination"),
				FirstDestination != nullptr && FirstDestination->Equals(Outcome.AssignedDestination, 1.0f));
		}
		return true;
	}

	if (FPlatformTime::Seconds() >= DeadlineSeconds)
	{
		Test->AddError(TEXT("PIE did not expose the formation assignment seam within ten seconds."));
		return true;
	}
	return false;
}

// REQUIRED: Observe production interfaces in the real navigable sandbox.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1FormationAssignmentTest,
	"Task0168.Headless.GameEngineBench.UE0159.Formation.Assign",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone1FormationAssignmentTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR SandboxMapPackage[] = TEXT("/Game/RTS/Maps/M1_CombatSandbox");
	if (!TestTrue(TEXT("The sandbox opens before formation assignment starts"), AutomationOpenMap(SandboxMapPackage, true)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyFormationAssignmentCommand(this, FPlatformTime::Seconds() + 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}


DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FRTSVerifyAttackDispatchCommand,
	FAutomationTestBase*, Test,
	double, DeadlineSeconds);

bool FRTSVerifyAttackDispatchCommand::Update()
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (WorldContext.WorldType != EWorldType::PIE || World == nullptr)
		{
			continue;
		}

		ARTSCombatUnit* Attacker = nullptr;
		ARTSCombatUnit* FriendlyTarget = nullptr;
		ARTSCombatUnit* Target = nullptr;
		for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
		{
			if (UnitIterator->GetGenericTeamId() == RTSTeams::Player
				&& (Attacker == nullptr || UnitIterator->GetStableUnitId() < Attacker->GetStableUnitId()))
			{
				FriendlyTarget = Attacker;
				Attacker = *UnitIterator;
			}
			else if (UnitIterator->GetGenericTeamId() == RTSTeams::Player && FriendlyTarget == nullptr)
			{
				FriendlyTarget = *UnitIterator;
			}
			else if (UnitIterator->GetGenericTeamId() == RTSTeams::Enemy
				&& (Target == nullptr || UnitIterator->GetStableUnitId() < Target->GetStableUnitId()))
			{
				Target = *UnitIterator;
			}
		}

		URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>();
		if (Attacker == nullptr || Target == nullptr || Commands == nullptr)
		{
			break;
		}

		ARTSCombatUnit* Attackers[] = {Attacker, Attacker, nullptr};
		const FRTSCommandResult Result = Commands->IssueAttack(Attackers, *Target);
		Test->TestTrue(TEXT("An attack receives a command ID"), Result.GroupCommandId > 0);
		Test->TestEqual(TEXT("The eligible attacker accepts the hostile target"), Result.GetAcceptedCount(), 1);
		Test->TestEqual(TEXT("A duplicate attacker is deduplicated and null gets an explicit refusal"), Result.Outcomes.Num(), 2);
		Test->TestTrue(TEXT("Null-unit refusal is typed"), Result.Outcomes.ContainsByPredicate([](const FRTSUnitCommandOutcome& O)
		{
			return O.Unit == nullptr && O.Failure == ERTSOrderFailure::InvalidUnit && !O.bAccepted;
		}));

		const FRTSOrderSnapshot Snapshot = Attacker->GetOrderComponent()->GetSnapshot();
		Test->TestEqual(TEXT("The attacker reports the group command ID"), Snapshot.GroupCommandId, Result.GroupCommandId);
		Test->TestEqual(TEXT("The attacker reports an Attack order"), Snapshot.Kind, ERTSOrderKind::Attack);
		Test->TestTrue(
			TEXT("Attack dispatch starts chasing or attacking"),
			Snapshot.Phase == ERTSOrderPhase::Chasing || Snapshot.Phase == ERTSOrderPhase::Attacking);

		if (Test->TestNotNull(TEXT("The attack-validation fixture finds another friendly"), FriendlyTarget))
		{
			ARTSCombatUnit* SingleAttacker[] = {Attacker};
			const FRTSCommandResult FriendlyAttack = Commands->IssueAttack(SingleAttacker, *FriendlyTarget);
			Test->TestEqual(TEXT("A friendly target accepts no attackers"), FriendlyAttack.GetAcceptedCount(), 0);
			Test->TestEqual(TEXT("The group refusal identifies a friendly target"), FriendlyAttack.Failure, ERTSOrderFailure::FriendlyTarget);
			if (!FriendlyAttack.Outcomes.IsEmpty())
			{
				Test->TestEqual(TEXT("The unit refusal identifies a friendly target"), FriendlyAttack.Outcomes[0].Failure, ERTSOrderFailure::FriendlyTarget);
			}
		}
		// A target dying during execution and a new request against an already-dead target
		// are distinct public cases. Keep a real live order as the cancellation prerequisite.
		ARTSCombatUnit* SingleAttacker[] = {Attacker};
		const FRTSCommandResult LiveReattack = Commands->IssueAttack(SingleAttacker, *Target);
		if (!Test->TestEqual(TEXT("A live target accepts the death-refusal setup order"), LiveReattack.GetAcceptedCount(), 1)) { return true; }
		Target->MarkDead();
		Test->TestFalse(TEXT("The refused target is actually dead"), Target->IsAlive());
		if (!Test->TestTrue(TEXT("The dead unit fixture remains addressable before its delayed cleanup"), IsValid(Target))) { return true; }
		const FRTSCommandResult DeadAttack = Commands->IssueAttack(SingleAttacker, *Target);
		Test->TestEqual(TEXT("Already-dead target accepts no new attackers"), DeadAttack.GetAcceptedCount(), 0);
		Test->TestEqual(TEXT("Already-dead target has a TargetUnavailable group refusal"), DeadAttack.Failure, ERTSOrderFailure::TargetUnavailable);
		if (Test->TestEqual(TEXT("Already-dead target returns one refused unit outcome"), DeadAttack.Outcomes.Num(), 1))
		{
			Test->TestEqual(TEXT("Already-dead target has a TargetUnavailable unit refusal"), DeadAttack.Outcomes[0].Failure, ERTSOrderFailure::TargetUnavailable);
		}
		const FRTSOrderSnapshot RefusedSnapshot = Attacker->GetOrderComponent()->GetSnapshot();
		Test->TestEqual(TEXT("Dead-target refusal cancels the previous unit order"), RefusedSnapshot.Kind, ERTSOrderKind::None);
		Test->TestEqual(TEXT("Dead-target refusal leaves the attacker idle"), RefusedSnapshot.Phase, ERTSOrderPhase::Idle);
		return true;
	}

	if (FPlatformTime::Seconds() >= DeadlineSeconds)
	{
		Test->AddError(TEXT("PIE did not expose the attack command seam within ten seconds."));
		return true;
	}
	return false;
}

// REQUIRED: Observe production interfaces in the real navigable sandbox.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1AttackDispatchTest,
	"Task0168.Headless.GameEngineBench.UE0159.Orders.AttackDispatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone1AttackDispatchTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR SandboxMapPackage[] = TEXT("/Game/RTS/Maps/M1_CombatSandbox");
	if (!TestTrue(TEXT("The sandbox opens before attack dispatch starts"), AutomationOpenMap(SandboxMapPackage, true)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyAttackDispatchCommand(this, FPlatformTime::Seconds() + 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

class FRTSVerifyChaseAndFireCommand final : public IAutomationLatentCommand
{
public:
	FRTSVerifyChaseAndFireCommand(FAutomationTestBase* InTest, const double InDeadlineSeconds)
		: Test(InTest)
		, DeadlineSeconds(InDeadlineSeconds)
	{
	}

	virtual bool Update() override
	{
		if (!Attacker.IsValid())
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
				return FinishIfTimedOut(TEXT("PIE did not expose the chase-and-fire world."));
			}

			ARTSCombatUnit* SelectedAttacker = nullptr;
			ARTSCombatUnit* SelectedTarget = nullptr;
			TArray<ARTSCombatUnit*> OtherUnits;
			for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
			{
				ARTSCombatUnit* Unit = *UnitIterator;
				if (Unit->GetGenericTeamId() == RTSTeams::Player
					&& (SelectedAttacker == nullptr || Unit->GetStableUnitId() < SelectedAttacker->GetStableUnitId()))
				{
					if (SelectedAttacker != nullptr)
					{
						OtherUnits.Add(SelectedAttacker);
					}
					SelectedAttacker = Unit;
				}
				else if (Unit->GetGenericTeamId() == RTSTeams::Enemy
					&& (SelectedTarget == nullptr || Unit->GetStableUnitId() < SelectedTarget->GetStableUnitId()))
				{
					if (SelectedTarget != nullptr)
					{
						OtherUnits.Add(SelectedTarget);
					}
					SelectedTarget = Unit;
				}
				else
				{
					OtherUnits.Add(Unit);
				}
			}

			URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>();
			if (SelectedAttacker == nullptr || SelectedTarget == nullptr || Commands == nullptr)
			{
				return FinishIfTimedOut(TEXT("The chase-and-fire fixture is incomplete."));
			}
			for (ARTSCombatUnit* OtherUnit : OtherUnits)
			{
				if (IsValid(OtherUnit) && OtherUnit != SelectedAttacker && OtherUnit != SelectedTarget)
				{
					OtherUnit->MarkDead();
				}
			}

			Attacker = SelectedAttacker;
			Target = SelectedTarget;
			InitialAttackerLocation = SelectedAttacker->GetActorLocation();
			SelectedTarget->SetActorLocation(
				InitialAttackerLocation + FVector(0.0f, 1800.0f, 0.0f),
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
			SelectedTarget->GetCharacterMovement()->MaxWalkSpeed = 0.0f;
			InitialTargetHealth = SelectedTarget->GetHealthComponent()->GetSnapshot().CurrentHealth;

			ARTSCombatUnit* Attackers[] = {SelectedAttacker};
			const FRTSCommandResult Result = Commands->IssueAttack(Attackers, *SelectedTarget);
			if (!Test->TestEqual(TEXT("The chase-and-fire order is accepted"), Result.GetAcceptedCount(), 1))
			{
				return true;
			}
			return false;
		}

		const FRTSOrderSnapshot Snapshot = Attacker->GetOrderComponent()->GetSnapshot();
		bObservedChasing |= Snapshot.Phase == ERTSOrderPhase::Chasing;
		bObservedProgress |= FVector::DistSquared2D(
			Attacker->GetActorLocation(),
			InitialAttackerLocation) >= FMath::Square(250.0f);
		const float CurrentTargetHealth = Target->GetHealthComponent()->GetSnapshot().CurrentHealth;
		if (CurrentTargetHealth < InitialTargetHealth)
		{
			Test->TestTrue(TEXT("The attacker visibly enters Chasing"), bObservedChasing);
			Test->TestTrue(TEXT("The attacker makes measurable chase progress"), bObservedProgress);
			Test->TestEqual(TEXT("The first accepted shot applies configured damage"), CurrentTargetHealth, InitialTargetHealth - 20.0f);
			Test->TestEqual(TEXT("The attacker is in Attacking when it fires"), Snapshot.Phase, ERTSOrderPhase::Attacking);
			Test->TestTrue(
				TEXT("The attacker stops inside configured range"),
				FVector::DistSquared2D(Attacker->GetActorLocation(), Target->GetActorLocation())
					<= FMath::Square(700.0f));
			return true;
		}

		return FinishIfTimedOut(TEXT("The attacker did not chase into range and fire within eight seconds."));
	}

private:
	bool FinishIfTimedOut(const TCHAR* Message) const
	{
		if (FPlatformTime::Seconds() < DeadlineSeconds)
		{
			return false;
		}
		Test->AddError(Message);
		return true;
	}

	FAutomationTestBase* Test;
	double DeadlineSeconds;
	TWeakObjectPtr<ARTSCombatUnit> Attacker;
	TWeakObjectPtr<ARTSCombatUnit> Target;
	FVector InitialAttackerLocation = FVector::ZeroVector;
	float InitialTargetHealth = 0.0f;
	bool bObservedChasing = false;
	bool bObservedProgress = false;
};

// REQUIRED: Observe production interfaces in the real navigable sandbox.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1ChaseAndFireTest,
	"Task0168.Headless.GameEngineBench.UE0159.Combat.ChaseAndFire",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone1ChaseAndFireTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR SandboxMapPackage[] = TEXT("/Game/RTS/Maps/M1_CombatSandbox");
	if (!TestTrue(TEXT("The sandbox opens before chase-and-fire starts"), AutomationOpenMap(SandboxMapPackage, true)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyChaseAndFireCommand(this, FPlatformTime::Seconds() + 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

class FRTSVerifyAutomaticAttackPriorityCommand final : public IAutomationLatentCommand
{
public:
	FRTSVerifyAutomaticAttackPriorityCommand(FAutomationTestBase* InTest, const double InDeadlineSeconds)
		: Test(InTest)
		, DeadlineSeconds(InDeadlineSeconds)
	{
	}

	virtual bool Update() override
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
			return FinishIfTimedOut(TEXT("PIE did not expose the automatic-priority world."));
		}

		if (bExplicitMoveIssued)
		{
			if (!Test->TestTrue(TEXT("Priority fixture retains its moving unit"), IdleUnit.IsValid())
				|| !Test->TestTrue(TEXT("Nearby hostile remains a live acquisition candidate"), Target.IsValid() && Target->IsAlive())) { return true; }
			const FRTSOrderSnapshot LaterSnapshot = IdleUnit->GetOrderComponent()->GetSnapshot();
			const bool bMoveRetained = Test->TestEqual(TEXT("Explicit Move survives later automatic-acquisition ticks"), LaterSnapshot.Kind, ERTSOrderKind::Move);
			const bool bIdentityRetained = Test->TestEqual(TEXT("Later ticks retain the explicit command ID"), LaterSnapshot.GroupCommandId, ExplicitCommandId);
			Test->TestFalse(TEXT("Later ticks do not mark the explicit Move automatic"), LaterSnapshot.bAutomatic);
			Test->TestNull(TEXT("Later ticks do not reacquire a target during Move"), LaterSnapshot.Target.Get());
			if (!bMoveRetained || !bIdentityRetained || LaterSnapshot.bAutomatic || LaterSnapshot.Target != nullptr) { return true; }
			if (World->GetTimeSeconds() - ExplicitMoveTime >= 0.35)
			{
				Test->TestTrue(TEXT("The retained explicit Move makes real progress"),
					FVector::DistSquared2D(IdleUnit->GetActorLocation(), ExplicitMoveStart) > FMath::Square(25.0f));
				return true;
			}
			return FinishIfTimedOut(TEXT("Explicit Move did not complete its post-acquisition observation window."));
		}

		if (!IdleUnit.IsValid())
		{
			ARTSCombatUnit* SelectedIdleUnit = nullptr;
			ARTSCombatUnit* SelectedTarget = nullptr;
			TArray<ARTSCombatUnit*> OtherUnits;
			for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
			{
				ARTSCombatUnit* Unit = *UnitIterator;
				if (Unit->GetGenericTeamId() == RTSTeams::Player && SelectedIdleUnit == nullptr)
				{
					SelectedIdleUnit = Unit;
				}
				else if (Unit->GetGenericTeamId() == RTSTeams::Enemy && SelectedTarget == nullptr)
				{
					SelectedTarget = Unit;
				}
				else
				{
					OtherUnits.Add(Unit);
				}
			}
			if (SelectedIdleUnit == nullptr || SelectedTarget == nullptr)
			{
				return FinishIfTimedOut(TEXT("The automatic-priority fixture is incomplete."));
			}
			for (ARTSCombatUnit* OtherUnit : OtherUnits)
			{
				OtherUnit->MarkDead();
			}

			IdleUnit = SelectedIdleUnit;
			Target = SelectedTarget;
			SelectedIdleUnit->TeleportTo(FVector(-1000.0f, -3500.0f, 80.0f), FRotator::ZeroRotator);
			SelectedTarget->TeleportTo(FVector(-1000.0f, -2500.0f, 80.0f), FRotator::ZeroRotator);
			SelectedTarget->GetCharacterMovement()->MaxWalkSpeed = 0.0f;
			return false;
		}

		const FRTSOrderSnapshot AutomaticSnapshot = IdleUnit->GetOrderComponent()->GetSnapshot();
		if (AutomaticSnapshot.Kind == ERTSOrderKind::Attack && AutomaticSnapshot.bAutomatic)
		{
			Test->TestEqual(TEXT("Idle acquisition chooses the nearby hostile"), AutomaticSnapshot.Target.Get(), Target.Get());
			URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>();
			if (!Test->TestNotNull(TEXT("The priority fixture finds the command subsystem"), Commands))
			{
				return true;
			}

			ARTSCombatUnit* Units[] = {IdleUnit.Get()};
			const FRTSCommandResult MoveResult = Commands->IssueMove(Units, FVector(1000.0f, -3500.0f, 0.0f));
			Test->TestEqual(TEXT("The explicit Move is accepted"), MoveResult.GetAcceptedCount(), 1);
			const FRTSOrderSnapshot MoveSnapshot = IdleUnit->GetOrderComponent()->GetSnapshot();
			Test->TestEqual(TEXT("Move replaces the automatic Attack immediately"), MoveSnapshot.Kind, ERTSOrderKind::Move);
			Test->TestFalse(TEXT("The player Move is not automatic"), MoveSnapshot.bAutomatic);
			Test->TestNull(TEXT("The replacement Move releases the attack target"), MoveSnapshot.Target.Get());
			Test->TestEqual(TEXT("The replacement reports the player command ID"), MoveSnapshot.GroupCommandId, MoveResult.GroupCommandId);
			ExplicitCommandId = MoveResult.GroupCommandId;
			ExplicitMoveTime = World->GetTimeSeconds();
			ExplicitMoveStart = IdleUnit->GetActorLocation();
			bExplicitMoveIssued = true;
			return false;
		}

		return FinishIfTimedOut(TEXT("The idle unit did not acquire the nearby hostile within five seconds."));
	}

private:
	bool FinishIfTimedOut(const TCHAR* Message) const
	{
		if (FPlatformTime::Seconds() < DeadlineSeconds)
		{
			return false;
		}
		Test->AddError(Message);
		return true;
	}

	FAutomationTestBase* Test;
	double DeadlineSeconds;
	TWeakObjectPtr<ARTSCombatUnit> IdleUnit;
	TWeakObjectPtr<ARTSCombatUnit> Target;
	bool bExplicitMoveIssued = false;
	int64 ExplicitCommandId = 0;
	double ExplicitMoveTime = 0.0;
	FVector ExplicitMoveStart = FVector::ZeroVector;
};

// REQUIRED: Observe production interfaces in the real navigable sandbox.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1AutomaticAttackPriorityTest,
	"Task0168.Headless.GameEngineBench.UE0159.Combat.OrderPriority",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone1AutomaticAttackPriorityTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR SandboxMapPackage[] = TEXT("/Game/RTS/Maps/M1_CombatSandbox");
	if (!TestTrue(TEXT("The sandbox opens before automatic-priority starts"), AutomationOpenMap(SandboxMapPackage, true)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyAutomaticAttackPriorityCommand(this, FPlatformTime::Seconds() + 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

class FRTSVerifyCombatCooldownCommand final : public IAutomationLatentCommand
{
public:
	FRTSVerifyCombatCooldownCommand(FAutomationTestBase* InTest, const double InDeadlineSeconds)
		: Test(InTest)
		, DeadlineSeconds(InDeadlineSeconds)
	{
	}

	virtual bool Update() override
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
			return FinishIfTimedOut(TEXT("PIE did not expose the cooldown world."));
		}

		if (!Attacker.IsValid())
		{
			TArray<ARTSCombatUnit*> OtherUnits;
			for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
			{
				ARTSCombatUnit* Unit = *UnitIterator;
				if (Unit->GetGenericTeamId() == RTSTeams::Player && !Attacker.IsValid())
				{
					Attacker = Unit;
				}
				else if (Unit->GetGenericTeamId() == RTSTeams::Enemy && !Target.IsValid())
				{
					Target = Unit;
				}
				else
				{
					OtherUnits.Add(Unit);
				}
			}
			if (!Attacker.IsValid() || !Target.IsValid())
			{
				return FinishIfTimedOut(TEXT("The cooldown fixture is incomplete."));
			}
			for (ARTSCombatUnit* OtherUnit : OtherUnits)
			{
				OtherUnit->MarkDead();
			}

			Attacker->TeleportTo(FVector(-500.0f, -3500.0f, 80.0f), FRotator::ZeroRotator);
			Target->TeleportTo(FVector(-500.0f, -3000.0f, 80.0f), FRotator::ZeroRotator);
			Target->GetCharacterMovement()->MaxWalkSpeed = 0.0f;
			Target->GetHealthComponent()->ConfigureMaximumHealth(100.0f, true);
			Commands = World->GetSubsystem<URTSCommandSubsystem>();
			ARTSCombatUnit* Attackers[] = {Attacker.Get()};
			const FRTSCommandResult InitialAttack = Commands->IssueAttack(Attackers, *Target);
			Test->TestEqual(TEXT("The cooldown fixture accepts its first attack"), InitialAttack.GetAcceptedCount(), 1);
			return false;
		}

		const float Health = Target->GetHealthComponent()->GetSnapshot().CurrentHealth;
		if (!bReplacementIssued && Health == 80.0f)
		{
			FirstHitWallTimeSeconds = World->GetTimeSeconds();
			ARTSCombatUnit* Attackers[] = {Attacker.Get()};
			const FRTSCommandResult Replacement = Commands->IssueAttack(Attackers, *Target);
			Test->TestEqual(TEXT("The replacement Attack is accepted"), Replacement.GetAcceptedCount(), 1);
			ReplacementCommandId = Replacement.GroupCommandId;
			bReplacementIssued = true;
			return false;
		}

		if (bReplacementIssued)
		{
			const double ElapsedSinceFirstHit = World->GetTimeSeconds() - FirstHitWallTimeSeconds;
			if (ElapsedSinceFirstHit < 0.6)
			{
				Test->TestEqual(TEXT("Replacing Attack does not grant an early shot"), Health, 80.0f);
			}
			if (Health == 60.0f)
			{
				Test->TestTrue(TEXT("The second hit waits for the weapon cooldown"), ElapsedSinceFirstHit >= 0.65);
				Test->TestEqual(
					TEXT("The weapon continues under the replacement command"),
					Attacker->GetOrderComponent()->GetSnapshot().GroupCommandId,
					ReplacementCommandId);
				return true;
			}
		}

		return FinishIfTimedOut(TEXT("The cooldown fixture did not observe two timed hits."));
	}

private:
	bool FinishIfTimedOut(const TCHAR* Message) const
	{
		if (FPlatformTime::Seconds() < DeadlineSeconds)
		{
			return false;
		}
		Test->AddError(Message);
		return true;
	}

	FAutomationTestBase* Test;
	double DeadlineSeconds;
	TWeakObjectPtr<ARTSCombatUnit> Attacker;
	TWeakObjectPtr<ARTSCombatUnit> Target;
	TWeakObjectPtr<URTSCommandSubsystem> Commands;
	double FirstHitWallTimeSeconds = 0.0;
	int64 ReplacementCommandId = 0;
	bool bReplacementIssued = false;
};

// REQUIRED: Observe production interfaces in the real navigable sandbox.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1CombatCooldownTest,
	"Task0168.Headless.GameEngineBench.UE0159.Combat.Cooldown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone1CombatCooldownTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR SandboxMapPackage[] = TEXT("/Game/RTS/Maps/M1_CombatSandbox");
	if (!TestTrue(TEXT("The sandbox opens before cooldown timing starts"), AutomationOpenMap(SandboxMapPackage, true)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyCombatCooldownCommand(this, FPlatformTime::Seconds() + 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

class FRTSVerifyTargetLostCommand final : public IAutomationLatentCommand
{
public:
	FRTSVerifyTargetLostCommand(FAutomationTestBase* InTest, const double InDeadlineSeconds)
		: Test(InTest)
		, DeadlineSeconds(InDeadlineSeconds)
	{
	}

	virtual bool Update() override
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
			return FinishIfTimedOut(TEXT("PIE did not expose the target-loss world."));
		}

		if (!Attacker.IsValid())
		{
			TArray<ARTSCombatUnit*> OtherUnits;
			for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
			{
				ARTSCombatUnit* Unit = *UnitIterator;
				if (Unit->GetGenericTeamId() == RTSTeams::Player && !Attacker.IsValid())
				{
					Attacker = Unit;
				}
				else if (Unit->GetGenericTeamId() == RTSTeams::Enemy && !Target.IsValid())
				{
					Target = Unit;
				}
				else
				{
					OtherUnits.Add(Unit);
				}
			}
			if (!Attacker.IsValid() || !Target.IsValid())
			{
				return FinishIfTimedOut(TEXT("The target-loss fixture is incomplete."));
			}
			for (ARTSCombatUnit* OtherUnit : OtherUnits)
			{
				OtherUnit->MarkDead();
			}

			Attacker->TeleportTo(FVector(500.0f, -3500.0f, 80.0f), FRotator::ZeroRotator);
			Target->TeleportTo(FVector(500.0f, -3000.0f, 80.0f), FRotator::ZeroRotator);
			Target->GetCharacterMovement()->MaxWalkSpeed = 0.0f;
			URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>();
			ARTSCombatUnit* Attackers[] = {Attacker.Get()};
			const FRTSCommandResult Attack = Commands->IssueAttack(Attackers, *Target);
			if (!Test->TestEqual(TEXT("The target-loss attack is accepted"), Attack.GetAcceptedCount(), 1))
			{
				return true;
			}
			CommandId = Attack.GroupCommandId;
			Target->TeleportTo(FVector(500.0f, -1200.0f, 80.0f), FRotator::ZeroRotator);
			return false;
		}

		const FRTSOrderSnapshot Snapshot = Attacker->GetOrderComponent()->GetSnapshot();
		if (!bObservedReturnToChasing && Snapshot.Phase == ERTSOrderPhase::Chasing)
		{
			Test->TestEqual(TEXT("Leaving range preserves the attack command ID"), Snapshot.GroupCommandId, CommandId);
			Test->TestEqual(TEXT("Leaving range preserves the attack target"), Snapshot.Target.Get(), Target.Get());
			bObservedReturnToChasing = true;
			Target->MarkDead();
			return false;
		}

		if (bObservedReturnToChasing && Snapshot.Phase == ERTSOrderPhase::Idle)
		{
			Test->TestEqual(TEXT("Target loss does not manufacture a replacement sequence"), Snapshot.GroupCommandId, CommandId);
			Test->TestNull(TEXT("Target loss releases the dead target"), Snapshot.Target.Get());
			return true;
		}

		return FinishIfTimedOut(TEXT("The attack did not chase after range loss and idle after death."));
	}

private:
	bool FinishIfTimedOut(const TCHAR* Message) const
	{
		if (FPlatformTime::Seconds() < DeadlineSeconds)
		{
			return false;
		}
		Test->AddError(Message);
		return true;
	}

	FAutomationTestBase* Test;
	double DeadlineSeconds;
	TWeakObjectPtr<ARTSCombatUnit> Attacker;
	TWeakObjectPtr<ARTSCombatUnit> Target;
	int64 CommandId = 0;
	bool bObservedReturnToChasing = false;
};

// REQUIRED: Observe production interfaces in the real navigable sandbox.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1TargetLostTest,
	"Task0168.Headless.GameEngineBench.UE0159.Combat.TargetLost",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone1TargetLostTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR SandboxMapPackage[] = TEXT("/Game/RTS/Maps/M1_CombatSandbox");
	if (!TestTrue(TEXT("The sandbox opens before target-loss starts"), AutomationOpenMap(SandboxMapPackage, true)))
	{
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyTargetLostCommand(this, FPlatformTime::Seconds() + 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}


// REQUIRED: Configured unit statistics, structure-target dispatch and real shot receipts.
class FUE0159StructureCombat final : public IAutomationLatentCommand
{
public:
	explicit FUE0159StructureCombat(FAutomationTestBase* InTest)
		: Test(InTest), Deadline(FPlatformTime::Seconds() + 30.0) {}
	bool Update() override
	{
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE) { World = Context.World(); break; }
		}
		if (FPlatformTime::Seconds() >= Deadline)
		{
			Test->AddError(TEXT("Configured structure combat did not complete in its bounded PIE window."));
			return true;
		}
		if (!World) { return false; }
		if (!bStarted)
		{
			TArray<ARTSCombatUnit*> Units;
			for (TActorIterator<ARTSCombatUnit> It(World); It; ++It) { Units.Add(*It); }
			if (!Test->TestTrue(TEXT("The map contains a nonempty native unit fixture"), Units.Num() >= 12)) { return true; }
			Attacker = Units[0];
			for (int32 Index = 1; Index < Units.Num(); ++Index) { Units[Index]->Destroy(); }
			Attacker->GetOrderComponent()->Cancel();
			Attacker->GetOrderComponent()->SetComponentTickEnabled(false);
			const FRTSCombatTuning Legacy = FRTSCombatTuning::Load();
			Test->TestEqual(TEXT("Legacy damage comes from combat tuning"), Attacker->GetAttackDamage(), Legacy.AttackDamage);
			Test->TestEqual(TEXT("Legacy range comes from combat tuning"), Attacker->GetAttackRange(), Legacy.AttackRange);
			Test->TestEqual(TEXT("Legacy acquisition comes from combat tuning"), Attacker->GetAcquisitionRange(), Legacy.AcquisitionRange);
			Test->TestEqual(TEXT("Legacy cooldown comes from combat tuning"), Attacker->GetWeaponCooldownSeconds(), Legacy.WeaponCooldownSeconds);
			const FRTSMilestone2Configuration& Config = FRTSMilestone2Configuration::Load();
			int32 Id = 9000;
			for (ERTSUnitType Type : {ERTSUnitType::InfantrySquad, ERTSUnitType::HeavyVehicle, ERTSUnitType::LightVehicle})
			{
				const FRTSUnitDefinition* Definition = Config.FindUnit(Type);
				if (!Test->TestNotNull(TEXT("Typed combat definition exists"), Definition)) { return true; }
				Attacker->ConfigureMilestone2Unit(Type, ++Id, RTSTeams::Player);
				Test->TestEqual(TEXT("Typed damage"), Attacker->GetAttackDamage(), Definition->AttackDamage);
				Test->TestEqual(TEXT("Typed range"), Attacker->GetAttackRange(), Definition->AttackRange);
				Test->TestEqual(TEXT("Typed acquisition"), Attacker->GetAcquisitionRange(), Definition->AcquisitionRange);
				Test->TestEqual(TEXT("Typed cooldown"), Attacker->GetWeaponCooldownSeconds(), Definition->WeaponCooldownSeconds);
			}
			if (!Test->TestTrue(TEXT("Attacker reaches the combat fixture"), Attacker->TeleportTo(FVector(0, -3500, 80), FRotator::ZeroRotator))) { return true; }
			const FRTSStructureDefinition* Definition = Config.FindStructure(ERTSStructureType::PowerGenerator);
			if (!Test->TestNotNull(TEXT("Structure definition exists"), Definition)) { return true; }
			const FTransform StructureTransform(FRotator::ZeroRotator, FVector(0, -3000, 0));
			Structure = World->SpawnActorDeferred<ARTSStructure>(ARTSStructure::StaticClass(), StructureTransform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Test->TestTrue(TEXT("Native structure spawns"), Structure.IsValid())) { return true; }
			Structure->Configure(ERTSStructureType::PowerGenerator, 9100, RTSTeams::Player, *Definition, Config.World.PlacementCellSize);
			Structure->FinishSpawning(StructureTransform);
			URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>();
			if (!Test->TestNotNull(TEXT("Command subsystem exists"), Commands)) { return true; }
			ARTSCombatUnit* Group[] = {Attacker.Get()};
			const FRTSCommandResult Friendly = Commands->IssueAttack(Group, *Structure);
			Test->TestEqual(TEXT("Friendly structures reject attack"), Friendly.GetAcceptedCount(), 0);
			Test->TestEqual(TEXT("Friendly structure refusal is typed"), Friendly.Failure, ERTSOrderFailure::FriendlyTarget);
			Structure->SetGenericTeamId(RTSTeams::Enemy);
			InitialHealth = Structure->GetHealthComponent()->GetSnapshot().CurrentHealth;
			Shots = MakeShared<TArray<FRTSWeaponEvent>>();
			Attacker->GetOrderComponent()->OnWeaponFired().AddLambda([Receipt = Shots](const FRTSWeaponEvent& Event) { Receipt->Add(Event); });
			const FRTSCommandResult Attack = Commands->IssueAttack(Group, *Structure);
			if (!Test->TestEqual(TEXT("Hostile structure accepts the attacker"), Attack.GetAcceptedCount(), 1)) { return true; }
			CommandId = Attack.GroupCommandId;
			Test->TestEqual(TEXT("Snapshot records structure target"), Attacker->GetOrderComponent()->GetSnapshot().TargetStructure.Get(), Structure.Get());
			Test->TestNull(TEXT("Structure order clears unit target"), Attacker->GetOrderComponent()->GetSnapshot().Target.Get());
			Attacker->GetOrderComponent()->SetComponentTickEnabled(true);
			bStarted = true;
			return false;
		}
		if (!Test->TestTrue(TEXT("Attacker survives the fixture"), Attacker.IsValid())) { return true; }
		if (!bTargetKilled && !Test->TestTrue(TEXT("Structure remains addressable before lethal damage"), Structure.IsValid())) { return true; }
		if (!bTargetKilled && !Shots->IsEmpty())
		{
			float TotalDamage = 0.0f;
			int32 PreviousSequence = 0;
			for (const FRTSWeaponEvent& Shot : *Shots)
			{
				Test->TestEqual(TEXT("Receipt identifies unit source"), Shot.SourceKind, ERTSWeaponSourceKind::Unit);
				Test->TestEqual(TEXT("Receipt identifies structure target"), Shot.TargetKind, ERTSWeaponTargetKind::Structure);
				Test->TestEqual(TEXT("Receipt target stable ID"), Shot.StableTargetId, 9100);
				Test->TestEqual(TEXT("Receipt source stable ID"), Shot.StableSourceId, Attacker->GetStableUnitId());
				Test->TestTrue(TEXT("Shot sequence increases"), Shot.SourceShotSequence > PreviousSequence);
				PreviousSequence = Shot.SourceShotSequence;
				TotalDamage += Shot.AppliedDamage;
			}
			Test->TestEqual(TEXT("Reported shots match actual structure damage"), Structure->GetHealthComponent()->GetSnapshot().CurrentHealth, InitialHealth - TotalDamage);
			Test->TestTrue(TEXT("Accepted shots apply positive damage"), TotalDamage > 0.0f);
			const FRTSDamageResult LethalHit = Structure->ApplyDamage(1000000.0f);
			Test->TestTrue(TEXT("Deliberate lethal damage reports a kill"), LethalHit.bKilled);
			// Native structures destroy synchronously on death, unlike the delayed unit corpse.
			Test->TestFalse(TEXT("Lethal structure damage invalidates its weak handle"), Structure.IsValid());
			ShotCountAtDeath = Shots->Num();
			bTargetKilled = true;
			return false;
		}
		if (bTargetKilled && Attacker->GetOrderComponent()->GetSnapshot().Phase == ERTSOrderPhase::Idle)
		{
			const FRTSOrderSnapshot Snapshot = Attacker->GetOrderComponent()->GetSnapshot();
			Test->TestNull(TEXT("Dead structure target is released"), Snapshot.TargetStructure.Get());
			Test->TestEqual(TEXT("Target death does not fabricate a new order"), Snapshot.GroupCommandId, CommandId);
			Test->TestEqual(TEXT("No accepted shot after target death"), Shots->Num(), ShotCountAtDeath);
			return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	double Deadline;
	TWeakObjectPtr<ARTSCombatUnit> Attacker;
	TWeakObjectPtr<ARTSStructure> Structure;
	TSharedPtr<TArray<FRTSWeaponEvent>> Shots;
	float InitialHealth = 0.0f;
	int64 CommandId = 0;
	int32 ShotCountAtDeath = 0;
	bool bStarted = false;
	bool bTargetKilled = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE0159StructureCombatTest,
	"Task0168.Headless.GameEngineBench.UE0159.Combat.TypedStructureExecution",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FUE0159StructureCombatTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Navigable sandbox opens"), AutomationOpenMap(TEXT("/Game/RTS/Maps/M1_CombatSandbox"), true))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FUE0159StructureCombat(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif

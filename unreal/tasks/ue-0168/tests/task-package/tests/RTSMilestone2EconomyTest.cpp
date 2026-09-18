// Copyright Epic Games, Inc. All Rights Reserved.

#include "Configuration/RTSMilestone2Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Units/RTSTeams.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone2AtomicEconomyTest,
	"Task0168.Headless.RTS.Milestone2.Economy.AtomicAccounting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone2AtomicEconomyTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR PlayerLoopMapPackage[] = TEXT("/Game/RTS/Maps/M2_PlayerLoop");
	if (!TestTrue(TEXT("The economy fixture opens"), AutomationOpenMap(PlayerLoopMapPackage, true)))
	{
		return false;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	URTSEconomySubsystem* Economy = World != nullptr ? World->GetSubsystem<URTSEconomySubsystem>() : nullptr;
	if (!TestNotNull(TEXT("The world owns one economy subsystem"), Economy))
	{
		return false;
	}

	const int32 StartingMaterials = FRTSMilestone2Configuration::Load().Economy.StartingMaterials;
	TestEqual(TEXT("The player ledger starts with configured Materials"), Economy->GetSnapshot(RTSTeams::Player).Materials, StartingMaterials);
	TestEqual(TEXT("The enemy ledger starts independently"), Economy->GetSnapshot(RTSTeams::Enemy).Materials, StartingMaterials);

	FRTSEconomyDelta Delta;
	Delta.Materials = -200;
	Delta.PowerGeneration = 100;
	Delta.SupplyCapacity = 20;
	const int64 TransactionId = Economy->AllocateTransactionId();
	const FRTSEconomyTransactionResult Accepted = Economy->TryCommit(TransactionId, RTSTeams::Player, Delta);
	TestTrue(TEXT("A fully affordable transaction commits"), Accepted.bAccepted);
	TestEqual(TEXT("Materials, Power, and Supply change together"), Accepted.Snapshot.Materials, StartingMaterials - 200);
	TestEqual(TEXT("Power generation is part of the same receipt"), Accepted.Snapshot.PowerGeneration, 100);
	TestEqual(TEXT("Supply capacity is part of the same receipt"), Accepted.Snapshot.SupplyCapacity, 20);

	const FRTSEconomyTransactionResult Duplicate = Economy->TryCommit(TransactionId, RTSTeams::Player, Delta);
	TestFalse(TEXT("The same transaction cannot apply twice"), Duplicate.bAccepted);
	TestEqual(TEXT("The duplicate refusal is explicit"), Duplicate.Refusal, ERTSEconomyRefusal::AlreadyApplied);
	TestEqual(TEXT("A duplicate changes no Materials"), Economy->GetSnapshot(RTSTeams::Player).Materials, StartingMaterials - 200);

	TestTrue(TEXT("A committed transaction can roll back exactly once"), Economy->TryRollback(TransactionId));
	const FRTSEconomySnapshot Restored = Economy->GetSnapshot(RTSTeams::Player);
	TestEqual(TEXT("Rollback restores Materials"), Restored.Materials, StartingMaterials);
	TestEqual(TEXT("Rollback restores Power"), Restored.PowerGeneration, 0);
	TestEqual(TEXT("Rollback restores Supply"), Restored.SupplyCapacity, 0);
	TestFalse(TEXT("Rollback cannot repeat"), Economy->TryRollback(TransactionId));

	FRTSEconomyDelta Unaffordable;
	Unaffordable.Materials = -(StartingMaterials + 1);
	const FRTSEconomyTransactionResult Rejected = Economy->TryCommit(
		Economy->AllocateTransactionId(), RTSTeams::Player, Unaffordable);
	TestFalse(TEXT("An unaffordable transaction is rejected atomically"), Rejected.bAccepted);
	TestEqual(TEXT("The refusal identifies insufficient Materials"), Rejected.Refusal, ERTSEconomyRefusal::InsufficientMaterials);
	TestEqual(TEXT("The rejected transaction changes nothing"), Economy->GetSnapshot(RTSTeams::Player).Materials, StartingMaterials);
	return !HasAnyErrors();
}

#endif

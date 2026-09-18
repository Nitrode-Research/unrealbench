// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Selection/RTSSelectionSubsystem.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
ARTSCombatUnit* SpawnUnit(
	UWorld& World,
	const int32 StableUnitId,
	const FGenericTeamId TeamId)
{
	ARTSCombatUnit* Unit = World.SpawnActor<ARTSCombatUnit>();
	if (Unit != nullptr)
	{
		Unit->SetStableUnitId(StableUnitId);
		Unit->SetGenericTeamId(FGenericTeamId(TeamId));
	}
	return Unit;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1SelectionReplaceTest,
	"Task0168.Headless.RTS.Milestone1.Selection.Replace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone1SelectionReplaceTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	World->AddToRoot();
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	LocalPlayer->AddToRoot();
	URTSSelectionSubsystem* Selection = NewObject<URTSSelectionSubsystem>(LocalPlayer);

	ARTSCombatUnit* FriendlyTwo = SpawnUnit(*World, 2, RTSTeams::Player);
	ARTSCombatUnit* Hostile = SpawnUnit(*World, 3, RTSTeams::Enemy);
	ARTSCombatUnit* FriendlyOne = SpawnUnit(*World, 1, RTSTeams::Player);
	if (!TestNotNull(TEXT("The first friendly fixture spawns"), FriendlyOne)
		|| !TestNotNull(TEXT("The second friendly fixture spawns"), FriendlyTwo)
		|| !TestNotNull(TEXT("The hostile fixture spawns"), Hostile))
	{
		LocalPlayer->RemoveFromRoot();
		World->DestroyWorld(false);
		World->RemoveFromRoot();
		return false;
	}

	int32 SelectionChangedCount = 0;
	Selection->OnSelectionChanged().AddLambda([&SelectionChangedCount]()
	{
		++SelectionChangedCount;
	});

	ARTSCombatUnit* Candidates[] = {FriendlyTwo, Hostile, FriendlyOne, FriendlyOne};
	Selection->ReplaceWith(Candidates);
	const TConstArrayView<ARTSCombatUnit*> SelectedUnits = Selection->GetLivingUnits();

	TestEqual(TEXT("Only unique living friendlies are selected"), SelectedUnits.Num(), 2);
	if (SelectedUnits.Num() == 2)
	{
		TestEqual(TEXT("Selection has stable unit ordering at index zero"), SelectedUnits[0], FriendlyOne);
		TestEqual(TEXT("Selection has stable unit ordering at index one"), SelectedUnits[1], FriendlyTwo);
	}
	TestEqual(TEXT("Replacement broadcasts one selection change"), SelectionChangedCount, 1);

	LocalPlayer->RemoveFromRoot();
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1SelectionToggleTest,
	"Task0168.Headless.RTS.Milestone1.Selection.Toggle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone1SelectionToggleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	World->AddToRoot();
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	LocalPlayer->AddToRoot();
	URTSSelectionSubsystem* Selection = NewObject<URTSSelectionSubsystem>(LocalPlayer);

	ARTSCombatUnit* FriendlyOne = SpawnUnit(*World, 1, RTSTeams::Player);
	ARTSCombatUnit* FriendlyTwo = SpawnUnit(*World, 2, RTSTeams::Player);
	ARTSCombatUnit* FriendlyThree = SpawnUnit(*World, 3, RTSTeams::Player);
	ARTSCombatUnit* Hostile = SpawnUnit(*World, 4, RTSTeams::Enemy);

	ARTSCombatUnit* InitialUnits[] = {FriendlyOne, FriendlyTwo};
	Selection->ReplaceWith(InitialUnits);
	int32 SelectionChangedCount = 0;
	Selection->OnSelectionChanged().AddLambda([&SelectionChangedCount]()
	{
		++SelectionChangedCount;
	});

	ARTSCombatUnit* ToggleCandidates[] = {FriendlyTwo, FriendlyThree, FriendlyThree, Hostile};
	Selection->Toggle(ToggleCandidates);
	const TConstArrayView<ARTSCombatUnit*> SelectedUnits = Selection->GetLivingUnits();

	TestEqual(TEXT("Toggle removes selected and adds unselected friendlies once"), SelectedUnits.Num(), 2);
	if (SelectedUnits.Num() == 2)
	{
		TestEqual(TEXT("The unaffected friendly remains selected"), SelectedUnits[0], FriendlyOne);
		TestEqual(TEXT("The new friendly is selected once"), SelectedUnits[1], FriendlyThree);
	}
	TestEqual(TEXT("A multi-unit toggle broadcasts one selection change"), SelectionChangedCount, 1);

	LocalPlayer->RemoveFromRoot();
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1SelectionClearTest,
	"Task0168.Headless.RTS.Milestone1.Selection.Clear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone1SelectionClearTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	World->AddToRoot();
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	LocalPlayer->AddToRoot();
	URTSSelectionSubsystem* Selection = NewObject<URTSSelectionSubsystem>(LocalPlayer);
	ARTSCombatUnit* Friendly = SpawnUnit(*World, 1, RTSTeams::Player);

	ARTSCombatUnit* InitialUnits[] = {Friendly};
	Selection->ReplaceWith(InitialUnits);
	int32 SelectionChangedCount = 0;
	Selection->OnSelectionChanged().AddLambda([&SelectionChangedCount]()
	{
		++SelectionChangedCount;
	});

	Selection->Clear();
	Selection->Clear();

	TestTrue(TEXT("Empty non-additive selection clears the set"), Selection->GetLivingUnits().IsEmpty());
	TestEqual(TEXT("Clearing an already empty set does not rebroadcast"), SelectionChangedCount, 1);

	LocalPlayer->RemoveFromRoot();
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1SelectionDeathTest,
	"Task0168.Headless.RTS.Milestone1.Selection.Death",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone1SelectionDeathTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	World->AddToRoot();
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	LocalPlayer->AddToRoot();
	URTSSelectionSubsystem* Selection = NewObject<URTSSelectionSubsystem>(LocalPlayer);
	ARTSCombatUnit* DyingUnit = SpawnUnit(*World, 1, RTSTeams::Player);
	ARTSCombatUnit* DestroyedUnit = SpawnUnit(*World, 2, RTSTeams::Player);

	ARTSCombatUnit* InitialUnits[] = {DyingUnit, DestroyedUnit};
	Selection->ReplaceWith(InitialUnits);
	int32 SelectionChangedCount = 0;
	Selection->OnSelectionChanged().AddLambda([&SelectionChangedCount]()
	{
		++SelectionChangedCount;
	});

	DyingUnit->MarkDead();
	DyingUnit->MarkDead();
	TestEqual(TEXT("Death removes one selected unit"), Selection->GetLivingUnits().Num(), 1);
	TestEqual(TEXT("Repeated death reports one selection change"), SelectionChangedCount, 1);

	DestroyedUnit->Destroy();
	TestTrue(TEXT("Destroyed units are pruned from selection"), Selection->GetLivingUnits().IsEmpty());
	TestEqual(TEXT("Destruction broadcasts one additional selection change"), SelectionChangedCount, 2);

	LocalPlayer->RemoveFromRoot();
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return !HasAnyErrors();
}

#endif

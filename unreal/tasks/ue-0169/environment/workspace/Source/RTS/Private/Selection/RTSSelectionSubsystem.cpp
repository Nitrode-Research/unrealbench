// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/RTSSelectionSubsystem.h"

#include "Structures/RTSStructure.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

void URTSSelectionSubsystem::Deinitialize()
{
	// Restore this contractor-owned implementation.
	Super::Deinitialize();
}

void URTSSelectionSubsystem::ReplaceWith(TConstArrayView<ARTSCombatUnit*> Candidates)
{
	// Restore this contractor-owned implementation.
}

void URTSSelectionSubsystem::ReplaceWithStructure(ARTSStructure* Candidate)
{
	// Restore this contractor-owned implementation.
}

void URTSSelectionSubsystem::ToggleStructure(ARTSStructure* Candidate)
{
	// Restore this contractor-owned implementation.
}

void URTSSelectionSubsystem::Toggle(TConstArrayView<ARTSCombatUnit*> Candidates)
{
	// Restore this contractor-owned implementation.
}

void URTSSelectionSubsystem::Clear()
{
	// Restore this contractor-owned implementation.
}

TConstArrayView<ARTSStructure*> URTSSelectionSubsystem::GetLivingStructures() const
{
	return SelectedStructures;
}

ARTSStructure* URTSSelectionSubsystem::GetFocusedStructure() const
{
	return SelectedStructures.IsEmpty() ? nullptr : SelectedStructures[0];
}

TConstArrayView<ARTSCombatUnit*> URTSSelectionSubsystem::GetLivingUnits() const
{
	return SelectedUnits;
}

FRTSSelectionChanged& URTSSelectionSubsystem::OnSelectionChanged()
{
	return SelectionChanged;
}

TArray<ARTSCombatUnit*> URTSSelectionSubsystem::FilterAndSortCandidates(
	TConstArrayView<ARTSCombatUnit*> Candidates) const
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSSelectionSubsystem::ApplySelection(TArray<ARTSCombatUnit*>&& Replacement)
{
	// Restore this contractor-owned implementation.
}

void URTSSelectionSubsystem::ClearStructures()
{
	// Restore this contractor-owned implementation.
}

void URTSSelectionSubsystem::RemoveUnavailableUnit(ARTSCombatUnit* Unit)
{
	// Restore this contractor-owned implementation.
}

void URTSSelectionSubsystem::SubscribeToUnit(ARTSCombatUnit& Unit)
{
	// Restore this contractor-owned implementation.
}

void URTSSelectionSubsystem::UnsubscribeFromUnit(ARTSCombatUnit& Unit)
{
	// Restore this contractor-owned implementation.
}

void URTSSelectionSubsystem::HandleUnitEligibilityChanged(ARTSCombatUnit* Unit)
{
	// Restore this contractor-owned implementation.
}

void URTSSelectionSubsystem::HandleSelectedUnitDestroyed(AActor* DestroyedActor)
{
	// Restore this contractor-owned implementation.
}

void URTSSelectionSubsystem::HandleSelectedStructureDestroyed(AActor* DestroyedActor)
{
	// Restore this contractor-owned implementation.
}

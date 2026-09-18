// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/RTSSelectionSubsystem.h"

#include "Structures/RTSStructure.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

void URTSSelectionSubsystem::Deinitialize()
{
	for (ARTSCombatUnit* SelectedUnit : SelectedUnits)
	{
		if (IsValid(SelectedUnit))
		{
			UnsubscribeFromUnit(*SelectedUnit);
		}
	}
	SelectedUnits.Reset();
	for (ARTSStructure* Structure : SelectedStructures)
	{
		if (IsValid(Structure))
		{
			Structure->OnDestroyed.RemoveDynamic(this, &URTSSelectionSubsystem::HandleSelectedStructureDestroyed);
		}
	}
	SelectedStructures.Reset();
	Super::Deinitialize();
}

void URTSSelectionSubsystem::ReplaceWith(TConstArrayView<ARTSCombatUnit*> Candidates)
{
	ClearStructures();
	TArray<ARTSCombatUnit*> Replacement = FilterAndSortCandidates(Candidates);
	ApplySelection(MoveTemp(Replacement));
}

void URTSSelectionSubsystem::ReplaceWithStructure(ARTSStructure* Candidate)
{
	ApplySelection({});
	ClearStructures();
	if (IsValid(Candidate) && Candidate->IsAlive() && Candidate->GetGenericTeamId() == RTSTeams::Player)
	{
		SelectedStructures.Add(Candidate);
		Candidate->OnDestroyed.AddUniqueDynamic(this, &URTSSelectionSubsystem::HandleSelectedStructureDestroyed);
		SelectionChanged.Broadcast();
	}
}

void URTSSelectionSubsystem::ToggleStructure(ARTSStructure* Candidate)
{
	if (!IsValid(Candidate) || !Candidate->IsAlive() || Candidate->GetGenericTeamId() != RTSTeams::Player)
	{
		return;
	}
	if (SelectedStructures.RemoveSingle(Candidate) > 0)
	{
		Candidate->OnDestroyed.RemoveDynamic(this, &URTSSelectionSubsystem::HandleSelectedStructureDestroyed);
	}
	else
	{
		SelectedStructures.Add(Candidate);
		SelectedStructures.Sort([](const ARTSStructure& Left, const ARTSStructure& Right)
		{
			return Left.GetStableStructureId() < Right.GetStableStructureId();
		});
		Candidate->OnDestroyed.AddUniqueDynamic(this, &URTSSelectionSubsystem::HandleSelectedStructureDestroyed);
	}
	SelectionChanged.Broadcast();
}

void URTSSelectionSubsystem::Toggle(TConstArrayView<ARTSCombatUnit*> Candidates)
{
	TArray<ARTSCombatUnit*> Replacement = SelectedUnits;
	for (ARTSCombatUnit* Candidate : FilterAndSortCandidates(Candidates))
	{
		if (Replacement.Contains(Candidate))
		{
			Replacement.RemoveSingle(Candidate);
		}
		else
		{
			Replacement.Add(Candidate);
		}
	}

	Replacement.Sort([](const ARTSCombatUnit& Left, const ARTSCombatUnit& Right)
	{
		return Left.GetStableUnitId() < Right.GetStableUnitId();
	});
	ApplySelection(MoveTemp(Replacement));
}

void URTSSelectionSubsystem::Clear()
{
	if (SelectedUnits.IsEmpty() && SelectedStructures.IsEmpty())
	{
		return;
	}

	ApplySelection({});
	ClearStructures();
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
	TArray<ARTSCombatUnit*> AcceptedUnits;
	for (ARTSCombatUnit* Candidate : Candidates)
	{
		if (IsValid(Candidate)
			&& Candidate->IsAlive()
			&& Candidate->GetGenericTeamId() == RTSTeams::Player)
		{
			AcceptedUnits.AddUnique(Candidate);
		}
	}

	AcceptedUnits.Sort([](const ARTSCombatUnit& Left, const ARTSCombatUnit& Right)
	{
		return Left.GetStableUnitId() < Right.GetStableUnitId();
	});
	return AcceptedUnits;
}

void URTSSelectionSubsystem::ApplySelection(TArray<ARTSCombatUnit*>&& Replacement)
{
	if (SelectedUnits == Replacement)
	{
		return;
	}

	for (ARTSCombatUnit* PreviouslySelected : SelectedUnits)
	{
		if (!Replacement.Contains(PreviouslySelected) && IsValid(PreviouslySelected))
		{
			UnsubscribeFromUnit(*PreviouslySelected);
		}
	}
	for (ARTSCombatUnit* NewlySelected : Replacement)
	{
		if (!SelectedUnits.Contains(NewlySelected))
		{
			SubscribeToUnit(*NewlySelected);
		}
	}

	SelectedUnits = MoveTemp(Replacement);
	SelectionChanged.Broadcast();
}

void URTSSelectionSubsystem::ClearStructures()
{
	if (SelectedStructures.IsEmpty())
	{
		return;
	}
	for (ARTSStructure* Structure : SelectedStructures)
	{
		if (IsValid(Structure))
		{
			Structure->OnDestroyed.RemoveDynamic(this, &URTSSelectionSubsystem::HandleSelectedStructureDestroyed);
		}
	}
	SelectedStructures.Reset();
	SelectionChanged.Broadcast();
}

void URTSSelectionSubsystem::RemoveUnavailableUnit(ARTSCombatUnit* Unit)
{
	if (SelectedUnits.RemoveSingle(Unit) == 0)
	{
		return;
	}

	if (IsValid(Unit))
	{
		UnsubscribeFromUnit(*Unit);
	}
	SelectionChanged.Broadcast();
}

void URTSSelectionSubsystem::SubscribeToUnit(ARTSCombatUnit& Unit)
{
	Unit.OnSelectionEligibilityChanged().AddUObject(
		this,
		&URTSSelectionSubsystem::HandleUnitEligibilityChanged);
	Unit.OnDestroyed.AddUniqueDynamic(this, &URTSSelectionSubsystem::HandleSelectedUnitDestroyed);
}

void URTSSelectionSubsystem::UnsubscribeFromUnit(ARTSCombatUnit& Unit)
{
	Unit.OnSelectionEligibilityChanged().RemoveAll(this);
	Unit.OnDestroyed.RemoveDynamic(this, &URTSSelectionSubsystem::HandleSelectedUnitDestroyed);
}

void URTSSelectionSubsystem::HandleUnitEligibilityChanged(ARTSCombatUnit* Unit)
{
	if (Unit == nullptr || !Unit->IsAlive())
	{
		RemoveUnavailableUnit(Unit);
	}
}

void URTSSelectionSubsystem::HandleSelectedUnitDestroyed(AActor* DestroyedActor)
{
	RemoveUnavailableUnit(Cast<ARTSCombatUnit>(DestroyedActor));
}

void URTSSelectionSubsystem::HandleSelectedStructureDestroyed(AActor* DestroyedActor)
{
	ARTSStructure* Structure = Cast<ARTSStructure>(DestroyedActor);
	if (SelectedStructures.RemoveSingle(Structure) > 0)
	{
		SelectionChanged.Broadcast();
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "RTSSelectionSubsystem.generated.h"

class ARTSCombatUnit;
class ARTSStructure;

DECLARE_MULTICAST_DELEGATE(FRTSSelectionChanged);

/** Local-player ownership seam for Slice 1 selection behavior. */
UCLASS()
class RTS_API URTSSelectionSubsystem final : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	void ReplaceWith(TConstArrayView<ARTSCombatUnit*> Candidates);
	void Toggle(TConstArrayView<ARTSCombatUnit*> Candidates);
	void ReplaceWithStructure(ARTSStructure* Candidate);
	void ToggleStructure(ARTSStructure* Candidate);
	void Clear();
	TConstArrayView<ARTSCombatUnit*> GetLivingUnits() const;
	TConstArrayView<ARTSStructure*> GetLivingStructures() const;
	ARTSStructure* GetFocusedStructure() const;

	FRTSSelectionChanged& OnSelectionChanged();

private:
	TArray<ARTSCombatUnit*> FilterAndSortCandidates(TConstArrayView<ARTSCombatUnit*> Candidates) const;
	void ApplySelection(TArray<ARTSCombatUnit*>&& Replacement);
	void ClearStructures();
	void RemoveUnavailableUnit(ARTSCombatUnit* Unit);
	void SubscribeToUnit(ARTSCombatUnit& Unit);
	void UnsubscribeFromUnit(ARTSCombatUnit& Unit);
	void HandleUnitEligibilityChanged(ARTSCombatUnit* Unit);

	UFUNCTION()
	void HandleSelectedUnitDestroyed(AActor* DestroyedActor);
	UFUNCTION()
	void HandleSelectedStructureDestroyed(AActor* DestroyedActor);

	UPROPERTY(Transient)
	TArray<ARTSCombatUnit*> SelectedUnits;
	UPROPERTY(Transient)
	TArray<ARTSStructure*> SelectedStructures;

	FRTSSelectionChanged SelectionChanged;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/RTSPresentationSubsystem.h"

#include "Combat/RTSTurretCombatComponent.h"
#include "Configuration/RTSMilestone3Configuration.h"
#include "Engine/World.h"
#include "Match/RTSMatchSubsystem.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Presentation/RTSTransientEffect.h"
#include "Reclaim/RTSWreckage.h"
#include "Reclaim/RTSWreckageSubsystem.h"
#include "Selection/RTSSelectionSubsystem.h"
#include "Sound/SoundBase.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSUnitRegistrySubsystem.h"
#include "Units/RTSTeams.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
template <typename SoundType>
void AssignSound(TObjectPtr<USoundBase>& Destination, const TCHAR* AssetPath)
{
	ConstructorHelpers::FObjectFinder<SoundType> Finder(AssetPath);
	if (Finder.Succeeded())
	{
		Destination = Finder.Object;
	}
}
}

URTSPresentationSubsystem::URTSPresentationSubsystem()
{
	AssignSound<USoundBase>(SelectionSound, TEXT("/Game/RTS/Audio/S_Selection.S_Selection"));
	AssignSound<USoundBase>(MoveSound, TEXT("/Game/RTS/Audio/S_Move.S_Move"));
	AssignSound<USoundBase>(AttackSound, TEXT("/Game/RTS/Audio/S_Attack.S_Attack"));
	AssignSound<USoundBase>(WeaponSound, TEXT("/Game/RTS/Audio/S_WeaponImpact.S_WeaponImpact"));
	AssignSound<USoundBase>(DestructionSound, TEXT("/Game/RTS/Audio/S_Destruction.S_Destruction"));
	AssignSound<USoundBase>(ConstructionSound, TEXT("/Game/RTS/Audio/S_ConstructionComplete.S_ConstructionComplete"));
	AssignSound<USoundBase>(ProductionSound, TEXT("/Game/RTS/Audio/S_ProductionComplete.S_ProductionComplete"));
	AssignSound<USoundBase>(ReclaimSound, TEXT("/Game/RTS/Audio/S_ReclaimComplete.S_ReclaimComplete"));
	AssignSound<USoundBase>(VictorySound, TEXT("/Game/RTS/Audio/S_Victory.S_Victory"));
	AssignSound<USoundBase>(DefeatSound, TEXT("/Game/RTS/Audio/S_Defeat.S_Defeat"));
}

void URTSPresentationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Restore this contractor-owned implementation.
	Super::Initialize(Collection);
}

void URTSPresentationSubsystem::Deinitialize()
{
	// Restore this contractor-owned implementation.
	Super::Deinitialize();
}

void URTSPresentationSubsystem::ObserveSelection(URTSSelectionSubsystem& Selection)
{
	// Restore this contractor-owned implementation.
}

void URTSPresentationSubsystem::ObserveCombatUnit(ARTSCombatUnit& Unit)
{
	// Restore this contractor-owned implementation.
}

void URTSPresentationSubsystem::ObserveStructure(ARTSStructure& Structure)
{
	// Restore this contractor-owned implementation.
}

FRTSPresentationReceipt URTSPresentationSubsystem::Present(const FRTSPresentationRequest& InRequest)
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSPresentationSnapshot URTSPresentationSubsystem::GetSnapshot()
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSPresentationRequested& URTSPresentationSubsystem::OnPresentationRequested()
{
	return PresentationRequested;
}

void URTSPresentationSubsystem::HandleSelectionChanged()
{
	// Restore this contractor-owned implementation.
}

void URTSPresentationSubsystem::HandleCommandAccepted(const FRTSCommandResult& Result)
{
	// Restore this contractor-owned implementation.
}

void URTSPresentationSubsystem::HandleConstructionCompleted(const FRTSStructureSnapshot& Snapshot)
{
	// Restore this contractor-owned implementation.
}

void URTSPresentationSubsystem::HandleStructureDestroyed(const FRTSStructureSnapshot& Snapshot)
{
	// Restore this contractor-owned implementation.
}

void URTSPresentationSubsystem::HandleMatchEventRecorded(const FRTSMatchEventReceipt& Receipt)
{
	// Restore this contractor-owned implementation.
}

void URTSPresentationSubsystem::HandleWeaponFired(const FRTSWeaponEvent& Event)
{
	// Restore this contractor-owned implementation.
}

void URTSPresentationSubsystem::PruneActiveEffects()
{
	// Restore this contractor-owned implementation.
}

void URTSPresentationSubsystem::RetainReceipt(const FRTSPresentationReceipt& Receipt)
{
	// Restore this contractor-owned implementation.
}

float URTSPresentationSubsystem::LifetimeFor(const ERTSPresentationEventKind Kind) const
{
	// Restore this contractor-owned implementation.
	return {};
}

USoundBase* URTSPresentationSubsystem::SoundFor(const ERTSPresentationEventKind Kind) const
{
	// Restore this contractor-owned implementation.
	return {};
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/RTSWorldOverlaySubsystem.h"

#include "Combat/RTSTurretCombatComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Match/RTSMatchSubsystem.h"
#include "Presentation/RTSWorldOverlayActor.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"

bool URTSWorldOverlaySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr
		&& (World->WorldType == EWorldType::Game
			|| World->WorldType == EWorldType::PIE
			|| World->WorldType == EWorldType::GamePreview);
}

void URTSWorldOverlaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Restore this contractor-owned implementation.
	Super::Initialize(Collection);
}

void URTSWorldOverlaySubsystem::Deinitialize()
{
	// Restore this contractor-owned implementation.
	Super::Deinitialize();
}

FRTSWorldOverlaySubmission URTSWorldOverlaySubsystem::Submit(
	const FRTSWorldOverlayDescriptor& Descriptor)
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSWorldOverlaySubsystem::Remove(const FRTSWorldOverlayHandle Handle)
{
	// Restore this contractor-owned implementation.
	return {};
}

int32 URTSWorldOverlaySubsystem::RemoveSource(
	const ERTSWorldOverlaySourceKind SourceKind,
	const int32 StableSourceId)
{
	// Restore this contractor-owned implementation.
	return {};
}

TArray<FRTSWorldOverlaySnapshot> URTSWorldOverlaySubsystem::GetSnapshots()
{
	// Restore this contractor-owned implementation.
	return {};
}

uint64 URTSWorldOverlaySubsystem::MakeSourceKey(const FRTSWorldOverlayDescriptor& Descriptor)
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSWorldOverlaySubsystem::PruneExpired()
{
	// Restore this contractor-owned implementation.
}

void URTSWorldOverlaySubsystem::HandleStructureChanged(const FRTSStructureSnapshot& Snapshot)
{
	// Restore this contractor-owned implementation.
}

void URTSWorldOverlaySubsystem::HandleStructureDestroyed(const FRTSStructureSnapshot& Snapshot)
{
	// Restore this contractor-owned implementation.
}

void URTSWorldOverlaySubsystem::HandleMatchResolved(const FRTSMatchSnapshot& Snapshot)
{
	// Restore this contractor-owned implementation.
}

void URTSWorldOverlaySubsystem::SubmitStructureOverlays(ARTSStructure& Structure)
{
	// Restore this contractor-owned implementation.
}

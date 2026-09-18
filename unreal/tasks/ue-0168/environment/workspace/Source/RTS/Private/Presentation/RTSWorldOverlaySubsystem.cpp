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
	Super::Initialize(Collection);
	Collection.InitializeDependency<URTSStructureSubsystem>();
	Collection.InitializeDependency<URTSMatchSubsystem>();
	if (UWorld* World = GetWorld())
	{
		if (URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>())
		{
			Structures->OnStructureChanged().AddUObject(this, &URTSWorldOverlaySubsystem::HandleStructureChanged);
			Structures->OnStructureDestroyed().AddUObject(this, &URTSWorldOverlaySubsystem::HandleStructureDestroyed);
		}
		if (URTSMatchSubsystem* Match = World->GetSubsystem<URTSMatchSubsystem>())
		{
			Match->OnMatchResolved().AddUObject(this, &URTSWorldOverlaySubsystem::HandleMatchResolved);
		}
		for (TActorIterator<ARTSStructure> Iterator(World); Iterator; ++Iterator)
		{
			SubmitStructureOverlays(**Iterator);
		}
	}
}

void URTSWorldOverlaySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>())
		{
			Structures->OnStructureChanged().RemoveAll(this);
			Structures->OnStructureDestroyed().RemoveAll(this);
		}
		if (URTSMatchSubsystem* Match = World->GetSubsystem<URTSMatchSubsystem>())
		{
			Match->OnMatchResolved().RemoveAll(this);
		}
	}
	for (TPair<int32, FOverlayEntry>& Entry : EntriesByHandle)
	{
		if (ARTSWorldOverlayActor* Actor = Entry.Value.Actor.Get())
		{
			Actor->Destroy();
		}
	}
	EntriesByHandle.Reset();
	HandleBySource.Reset();
	Super::Deinitialize();
}

FRTSWorldOverlaySubmission URTSWorldOverlaySubsystem::Submit(
	const FRTSWorldOverlayDescriptor& Descriptor)
{
	FRTSWorldOverlaySubmission Result;
	const bool bHasValidShape = Descriptor.Mode == ERTSWorldOverlayMode::PlacementFootprint
		? Descriptor.HalfExtent.X > 0.0f && Descriptor.HalfExtent.Y > 0.0f
		: Descriptor.Radius > 0.0f;
	if (GetWorld() == nullptr
		|| Descriptor.StableSourceId == INDEX_NONE
		|| Descriptor.TeamId == FGenericTeamId::NoTeam
		|| Descriptor.LifetimeSeconds < 0.0f
		|| !bHasValidShape)
	{
		return Result;
	}

	PruneExpired();
	const uint64 SourceKey = MakeSourceKey(Descriptor);
	int32 HandleValue = HandleBySource.FindRef(SourceKey);
	FOverlayEntry* Existing = EntriesByHandle.Find(HandleValue);
	ARTSWorldOverlayActor* Actor = Existing != nullptr ? Existing->Actor.Get() : nullptr;
	if (Actor == nullptr)
	{
		HandleValue = NextHandle++;
		Actor = GetWorld()->SpawnActor<ARTSWorldOverlayActor>();
		if (Actor == nullptr)
		{
			return Result;
		}
		FOverlayEntry& NewEntry = EntriesByHandle.Add(HandleValue);
		NewEntry.Actor = Actor;
		NewEntry.SourceKey = SourceKey;
		Existing = &NewEntry;
		HandleBySource.Add(SourceKey, HandleValue);
	}
	if (!Actor->ApplyDescriptor(Descriptor))
	{
		Remove(FRTSWorldOverlayHandle{HandleValue});
		return Result;
	}

	Existing->Descriptor = Descriptor;
	Actor->SetLifeSpan(Descriptor.LifetimeSeconds);
	Result.bAccepted = true;
	Result.Handle.Value = HandleValue;
	return Result;
}

bool URTSWorldOverlaySubsystem::Remove(const FRTSWorldOverlayHandle Handle)
{
	FOverlayEntry Entry;
	if (!EntriesByHandle.RemoveAndCopyValue(Handle.Value, Entry))
	{
		return false;
	}
	HandleBySource.Remove(Entry.SourceKey);
	if (ARTSWorldOverlayActor* Actor = Entry.Actor.Get())
	{
		Actor->Destroy();
	}
	return true;
}

int32 URTSWorldOverlaySubsystem::RemoveSource(
	const ERTSWorldOverlaySourceKind SourceKind,
	const int32 StableSourceId)
{
	TArray<FRTSWorldOverlayHandle> Handles;
	for (const TPair<int32, FOverlayEntry>& Entry : EntriesByHandle)
	{
		if (Entry.Value.Descriptor.SourceKind == SourceKind
			&& Entry.Value.Descriptor.StableSourceId == StableSourceId)
		{
			Handles.Add(FRTSWorldOverlayHandle{Entry.Key});
		}
	}
	for (const FRTSWorldOverlayHandle Handle : Handles)
	{
		Remove(Handle);
	}
	return Handles.Num();
}

TArray<FRTSWorldOverlaySnapshot> URTSWorldOverlaySubsystem::GetSnapshots()
{
	PruneExpired();
	TArray<FRTSWorldOverlaySnapshot> Snapshots;
	for (const TPair<int32, FOverlayEntry>& Entry : EntriesByHandle)
	{
		FRTSWorldOverlaySnapshot& Snapshot = Snapshots.AddDefaulted_GetRef();
		Snapshot.Handle.Value = Entry.Key;
		Snapshot.Descriptor = Entry.Value.Descriptor;
	}
	Snapshots.Sort([](const FRTSWorldOverlaySnapshot& Left, const FRTSWorldOverlaySnapshot& Right)
	{
		return Left.Handle.Value < Right.Handle.Value;
	});
	return Snapshots;
}

uint64 URTSWorldOverlaySubsystem::MakeSourceKey(const FRTSWorldOverlayDescriptor& Descriptor)
{
	return (static_cast<uint64>(Descriptor.SourceKind) << 56)
		| (static_cast<uint64>(Descriptor.Mode) << 48)
		| static_cast<uint32>(Descriptor.StableSourceId);
}

void URTSWorldOverlaySubsystem::PruneExpired()
{
	TArray<FRTSWorldOverlayHandle> Expired;
	for (const TPair<int32, FOverlayEntry>& Entry : EntriesByHandle)
	{
		if (!Entry.Value.Actor.IsValid())
		{
			Expired.Add(FRTSWorldOverlayHandle{Entry.Key});
		}
	}
	for (const FRTSWorldOverlayHandle Handle : Expired)
	{
		Remove(Handle);
	}
}

void URTSWorldOverlaySubsystem::HandleStructureChanged(const FRTSStructureSnapshot& Snapshot)
{
	URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	ARTSStructure* Structure = Structures != nullptr
		? Structures->FindActor(Snapshot.StableStructureId)
		: nullptr;
	if (Structure == nullptr || !Snapshot.bAlive || !Snapshot.bConstructed)
	{
		RemoveSource(ERTSWorldOverlaySourceKind::Structure, Snapshot.StableStructureId);
		return;
	}
	SubmitStructureOverlays(*Structure);
}

void URTSWorldOverlaySubsystem::HandleStructureDestroyed(const FRTSStructureSnapshot& Snapshot)
{
	RemoveSource(ERTSWorldOverlaySourceKind::Structure, Snapshot.StableStructureId);
}

void URTSWorldOverlaySubsystem::HandleMatchResolved(const FRTSMatchSnapshot& Snapshot)
{
	TArray<FRTSWorldOverlayHandle> PreviewHandles;
	for (const TPair<int32, FOverlayEntry>& Entry : EntriesByHandle)
	{
		if (Entry.Value.Descriptor.SourceKind != ERTSWorldOverlaySourceKind::Showcase)
		{
			PreviewHandles.Add(FRTSWorldOverlayHandle{Entry.Key});
		}
	}
	for (const FRTSWorldOverlayHandle Handle : PreviewHandles)
	{
		Remove(Handle);
	}
}

void URTSWorldOverlaySubsystem::SubmitStructureOverlays(ARTSStructure& Structure)
{
	if (!Structure.IsAlive() || !Structure.IsConstructed())
	{
		return;
	}
	if (Structure.GetBuildAreaRadius() > 0.0f)
	{
		FRTSWorldOverlayDescriptor Descriptor;
		Descriptor.Mode = ERTSWorldOverlayMode::BuildArea;
		Descriptor.SourceKind = ERTSWorldOverlaySourceKind::Structure;
		Descriptor.StableSourceId = Structure.GetStableStructureId();
		Descriptor.WorldTransform = Structure.GetActorTransform();
		Descriptor.Radius = Structure.GetBuildAreaRadius();
		Descriptor.TeamId = Structure.GetGenericTeamId();
		Submit(Descriptor);
	}
	if (const URTSTurretCombatComponent* Turret = Structure.GetTurretCombatComponent())
	{
		FRTSWorldOverlayDescriptor Descriptor;
		Descriptor.Mode = ERTSWorldOverlayMode::TurretRange;
		Descriptor.SourceKind = ERTSWorldOverlaySourceKind::Structure;
		Descriptor.StableSourceId = Structure.GetStableStructureId();
		Descriptor.WorldTransform = Structure.GetActorTransform();
		Descriptor.Radius = Turret->GetSnapshot().AttackRange;
		Descriptor.TeamId = Structure.GetGenericTeamId();
		Submit(Descriptor);
	}
}

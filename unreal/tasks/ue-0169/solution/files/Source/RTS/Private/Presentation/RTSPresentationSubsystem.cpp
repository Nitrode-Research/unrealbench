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
	Super::Initialize(Collection);
	Collection.InitializeDependency<URTSCommandSubsystem>();
	Collection.InitializeDependency<URTSMatchSubsystem>();
	Collection.InitializeDependency<URTSStructureSubsystem>();
	Collection.InitializeDependency<URTSUnitRegistrySubsystem>();
	Collection.InitializeDependency<URTSWreckageSubsystem>();

	const FRTSPresentationTuning& Tuning = FRTSMilestone3Configuration::Load().Presentation;
	MaximumActiveEffects = FMath::Max(1, Tuning.MaximumActiveEffects);
	MaximumRetainedReceipts = FMath::Max(1, Tuning.MaximumRetainedReceipts);
	MaximumTrackedEventKeys = FMath::Max(MaximumRetainedReceipts, Tuning.MaximumTrackedEventKeys);
	VolumeMultiplier = FMath::Clamp(Tuning.VolumeMultiplier, 0.0f, 1.0f);
	AcknowledgementLifetimeSeconds = FMath::Max(0.05f, Tuning.AcknowledgementLifetimeSeconds);
	WeaponLifetimeSeconds = FMath::Max(0.05f, Tuning.WeaponLifetimeSeconds);
	DestructionLifetimeSeconds = FMath::Max(0.05f, Tuning.DestructionLifetimeSeconds);
	CompletionLifetimeSeconds = FMath::Max(0.05f, Tuning.CompletionLifetimeSeconds);
	MatchLifetimeSeconds = FMath::Max(0.05f, Tuning.MatchLifetimeSeconds);

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	if (URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>())
	{
		Commands->OnCommandAccepted().AddUObject(this, &URTSPresentationSubsystem::HandleCommandAccepted);
	}
	if (URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>())
	{
		Structures->OnConstructionCompleted().AddUObject(this, &URTSPresentationSubsystem::HandleConstructionCompleted);
		Structures->OnStructureDestroyed().AddUObject(this, &URTSPresentationSubsystem::HandleStructureDestroyed);
		for (const FGenericTeamId TeamId : {RTSTeams::Player, RTSTeams::Enemy})
		{
			for (const FRTSStructureSnapshot& Snapshot : Structures->Query(TeamId))
			{
				if (ARTSStructure* Structure = Structures->FindActor(Snapshot.StableStructureId))
				{
					ObserveStructure(*Structure);
				}
			}
		}
	}
	if (URTSMatchSubsystem* Match = World->GetSubsystem<URTSMatchSubsystem>())
	{
		Match->OnMatchEventRecorded().AddUObject(this, &URTSPresentationSubsystem::HandleMatchEventRecorded);
	}
	if (URTSUnitRegistrySubsystem* Units = World->GetSubsystem<URTSUnitRegistrySubsystem>())
	{
		for (const FGenericTeamId TeamId : {RTSTeams::Player, RTSTeams::Enemy})
		{
			for (const FRTSUnitSnapshot& Snapshot : Units->Query(TeamId))
			{
				if (ARTSCombatUnit* Unit = Units->FindActor(Snapshot.StableUnitId))
				{
					ObserveCombatUnit(*Unit);
				}
			}
		}
	}
}

void URTSPresentationSubsystem::Deinitialize()
{
	if (IsValid(PlayerSelection))
	{
		PlayerSelection->OnSelectionChanged().RemoveAll(this);
	}
	if (UWorld* World = GetWorld())
	{
		if (URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>())
		{
			Commands->OnCommandAccepted().RemoveAll(this);
		}
		if (URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>())
		{
			Structures->OnConstructionCompleted().RemoveAll(this);
			Structures->OnStructureDestroyed().RemoveAll(this);
		}
		if (URTSMatchSubsystem* Match = World->GetSubsystem<URTSMatchSubsystem>())
		{
			Match->OnMatchEventRecorded().RemoveAll(this);
		}
	}
	PlayerSelection = nullptr;
	ActiveEffects.Reset();
	PresentedEvents.Reset();
	PresentedEventOrder.Reset();
	RecentReceipts.Reset();
	Super::Deinitialize();
}

void URTSPresentationSubsystem::ObserveSelection(URTSSelectionSubsystem& Selection)
{
	if (IsValid(PlayerSelection) && PlayerSelection != &Selection)
	{
		PlayerSelection->OnSelectionChanged().RemoveAll(this);
	}
	PlayerSelection = &Selection;
	Selection.OnSelectionChanged().RemoveAll(this);
	Selection.OnSelectionChanged().AddUObject(this, &URTSPresentationSubsystem::HandleSelectionChanged);
}

void URTSPresentationSubsystem::ObserveCombatUnit(ARTSCombatUnit& Unit)
{
	if (URTSUnitOrderComponent* Orders = Unit.GetOrderComponent())
	{
		Orders->OnWeaponFired().RemoveAll(this);
		Orders->OnWeaponFired().AddUObject(this, &URTSPresentationSubsystem::HandleWeaponFired);
	}
}

void URTSPresentationSubsystem::ObserveStructure(ARTSStructure& Structure)
{
	if (URTSTurretCombatComponent* Turret = Structure.GetTurretCombatComponent())
	{
		Turret->OnWeaponFired().RemoveAll(this);
		Turret->OnWeaponFired().AddUObject(this, &URTSPresentationSubsystem::HandleWeaponFired);
	}
}

FRTSPresentationReceipt URTSPresentationSubsystem::Present(const FRTSPresentationRequest& InRequest)
{
	FRTSPresentationReceipt Receipt;
	Receipt.Request = InRequest;
	if (InRequest.StableSourceId == INDEX_NONE
		|| InRequest.SourceEventSequence <= 0
		|| InRequest.SourceWorldLocation.ContainsNaN()
		|| InRequest.TargetWorldLocation.ContainsNaN()
		|| !FMath::IsFinite(InRequest.Magnitude))
	{
		RetainReceipt(Receipt);
		return Receipt;
	}

	const FEventKey EventKey{
		InRequest.Kind,
		InRequest.SourceKind,
		InRequest.TeamId.GetId(),
		InRequest.StableSourceId,
		InRequest.SourceEventSequence};
	if (PresentedEvents.Contains(EventKey))
	{
		Receipt.bDuplicate = true;
		++DuplicateRequestCount;
		RetainReceipt(Receipt);
		return Receipt;
	}

	while (PresentedEventOrder.Num() >= MaximumTrackedEventKeys)
	{
		PresentedEvents.Remove(PresentedEventOrder[0]);
		PresentedEventOrder.RemoveAt(0, 1, EAllowShrinking::No);
	}
	PresentedEvents.Add(EventKey);
	PresentedEventOrder.Add(EventKey);
	Receipt.Request.LifetimeSeconds = InRequest.LifetimeSeconds > 0.0f
		? FMath::Min(InRequest.LifetimeSeconds, MatchLifetimeSeconds)
		: LifetimeFor(InRequest.Kind);
	Receipt.bAccepted = true;
	Receipt.ReceiptSequence = NextReceiptSequence++;
	++AcceptedRequestCount;

	PruneActiveEffects();
	while (ActiveEffects.Num() >= MaximumActiveEffects)
	{
		if (ARTSTransientEffect* Oldest = ActiveEffects[0].Get(); IsValid(Oldest))
		{
			Oldest->Destroy();
		}
		ActiveEffects.RemoveAt(0, 1, EAllowShrinking::No);
		++EvictedEffectCount;
	}

	USoundBase* Sound = SoundFor(Receipt.Request.Kind);
	Receipt.bAudioAvailable = Sound != nullptr;
	if (UWorld* World = GetWorld())
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ARTSTransientEffect* Effect = World->SpawnActor<ARTSTransientEffect>(
			ARTSTransientEffect::StaticClass(),
			Receipt.Request.SourceWorldLocation,
			FRotator::ZeroRotator,
			Parameters);
		if (Effect != nullptr)
		{
			Effect->Configure(Receipt.Request, Sound, VolumeMultiplier);
			ActiveEffects.Add(Effect);
			Receipt.bVisualSpawned = true;
		}
	}
	Receipt.ActiveEffectsAfterRequest = ActiveEffects.Num();
	RetainReceipt(Receipt);
	PresentationRequested.Broadcast(Receipt);
	return Receipt;
}

FRTSPresentationSnapshot URTSPresentationSubsystem::GetSnapshot()
{
	PruneActiveEffects();
	FRTSPresentationSnapshot Snapshot;
	Snapshot.ActiveEffectCount = ActiveEffects.Num();
	Snapshot.MaximumActiveEffects = MaximumActiveEffects;
	Snapshot.TrackedEventCount = PresentedEvents.Num();
	Snapshot.MaximumTrackedEvents = MaximumTrackedEventKeys;
	Snapshot.MaximumRetainedReceipts = MaximumRetainedReceipts;
	Snapshot.EvictedEffectCount = EvictedEffectCount;
	Snapshot.AcceptedRequestCount = AcceptedRequestCount;
	Snapshot.DuplicateRequestCount = DuplicateRequestCount;
	Snapshot.RecentReceipts = RecentReceipts;
	return Snapshot;
}

FRTSPresentationRequested& URTSPresentationSubsystem::OnPresentationRequested()
{
	return PresentationRequested;
}

void URTSPresentationSubsystem::HandleSelectionChanged()
{
	if (!IsValid(PlayerSelection))
	{
		return;
	}
	FRTSPresentationRequest Request;
	Request.Kind = ERTSPresentationEventKind::SelectionAcknowledged;
	Request.SourceKind = ERTSPresentationSourceKind::Selection;
	Request.TeamId = RTSTeams::Player;
	Request.SourceEventSequence = NextSelectionEventSequence++;
	if (!PlayerSelection->GetLivingUnits().IsEmpty())
	{
		const ARTSCombatUnit* Unit = PlayerSelection->GetLivingUnits()[0];
		Request.StableSourceId = Unit->GetStableUnitId();
		Request.SourceWorldLocation = Unit->GetActorLocation();
		Request.TargetWorldLocation = Request.SourceWorldLocation;
	}
	else if (ARTSStructure* Structure = PlayerSelection->GetFocusedStructure())
	{
		Request.StableSourceId = Structure->GetStableStructureId();
		Request.SourceWorldLocation = Structure->GetActorLocation();
		Request.TargetWorldLocation = Request.SourceWorldLocation;
	}
	else
	{
		return;
	}
	Present(Request);
}

void URTSPresentationSubsystem::HandleCommandAccepted(const FRTSCommandResult& Result)
{
	const FRTSUnitCommandOutcome* SourceOutcome = Result.Outcomes.FindByPredicate(
		[](const FRTSUnitCommandOutcome& Outcome)
		{
			return Outcome.bAccepted && IsValid(Outcome.Unit);
		});
	if (SourceOutcome == nullptr || SourceOutcome->Unit->GetGenericTeamId() != RTSTeams::Player)
	{
		return;
	}

	FRTSPresentationRequest Request;
	Request.SourceKind = ERTSPresentationSourceKind::Command;
	Request.TeamId = SourceOutcome->Unit->GetGenericTeamId();
	Request.StableSourceId = SourceOutcome->Unit->GetStableUnitId();
	Request.SourceEventSequence = Result.GroupCommandId;
	Request.SourceWorldLocation = SourceOutcome->Unit->GetActorLocation();
	if (IsValid(Result.RequestedTarget))
	{
		Request.Kind = ERTSPresentationEventKind::AttackAcknowledged;
		Request.StableTargetId = Result.RequestedTarget->GetStableUnitId();
		Request.TargetWorldLocation = Result.RequestedTarget->GetActorLocation();
	}
	else if (IsValid(Result.RequestedStructureTarget))
	{
		Request.Kind = ERTSPresentationEventKind::AttackAcknowledged;
		Request.StableTargetId = Result.RequestedStructureTarget->GetStableStructureId();
		Request.TargetWorldLocation = Result.RequestedStructureTarget->GetActorLocation();
	}
	else if (!IsValid(Result.RequestedWreckageTarget))
	{
		Request.Kind = ERTSPresentationEventKind::MoveAcknowledged;
		Request.TargetWorldLocation = Result.ProjectedDestination;
	}
	else
	{
		return;
	}
	Present(Request);
}

void URTSPresentationSubsystem::HandleConstructionCompleted(const FRTSStructureSnapshot& Snapshot)
{
	FRTSPresentationRequest Request;
	Request.Kind = ERTSPresentationEventKind::ConstructionCompleted;
	Request.SourceKind = ERTSPresentationSourceKind::Structure;
	Request.TeamId = Snapshot.TeamId;
	Request.StableSourceId = Snapshot.StableStructureId;
	Request.SourceEventSequence = 1;
	Request.SourceWorldLocation = Snapshot.WorldLocation;
	Request.TargetWorldLocation = Snapshot.WorldLocation;
	Present(Request);
}

void URTSPresentationSubsystem::HandleStructureDestroyed(const FRTSStructureSnapshot& Snapshot)
{
	LastDestructionLocationByTeam.Add(Snapshot.TeamId.GetId(), Snapshot.WorldLocation);
	FRTSPresentationRequest Request;
	Request.Kind = ERTSPresentationEventKind::Destruction;
	Request.SourceKind = ERTSPresentationSourceKind::Structure;
	Request.TeamId = Snapshot.TeamId;
	Request.StableSourceId = Snapshot.StableStructureId;
	Request.SourceEventSequence = 1;
	Request.SourceWorldLocation = Snapshot.WorldLocation;
	Request.TargetWorldLocation = Snapshot.WorldLocation;
	Present(Request);
}

void URTSPresentationSubsystem::HandleMatchEventRecorded(const FRTSMatchEventReceipt& Receipt)
{
	if (!Receipt.bAccepted)
	{
		return;
	}
	FRTSPresentationRequest Request;
	Request.TeamId = Receipt.TeamId;
	Request.StableSourceId = Receipt.StableSourceId;
	Request.SourceEventSequence = Receipt.EventSequence;
	Request.Magnitude = Receipt.Amount;
	UWorld* World = GetWorld();
	switch (Receipt.Kind)
	{
	case ERTSMatchEventKind::UnitProduced:
		Request.Kind = ERTSPresentationEventKind::ProductionCompleted;
		Request.SourceKind = ERTSPresentationSourceKind::Unit;
		if (const URTSUnitRegistrySubsystem* Units = World != nullptr
			? World->GetSubsystem<URTSUnitRegistrySubsystem>()
			: nullptr)
		{
			if (const ARTSCombatUnit* Unit = Units->FindActor(Receipt.StableSourceId))
			{
				Request.SourceWorldLocation = Unit->GetActorLocation();
			}
		}
		break;
	case ERTSMatchEventKind::UnitLost:
		Request.Kind = ERTSPresentationEventKind::Destruction;
		Request.SourceKind = ERTSPresentationSourceKind::Unit;
		if (const URTSUnitRegistrySubsystem* Units = World != nullptr
			? World->GetSubsystem<URTSUnitRegistrySubsystem>()
			: nullptr)
		{
			if (const ARTSCombatUnit* Unit = Units->FindActor(Receipt.StableSourceId))
			{
				Request.SourceWorldLocation = Unit->GetActorLocation();
			}
		}
		LastDestructionLocationByTeam.Add(Receipt.TeamId.GetId(), Request.SourceWorldLocation);
		break;
	case ERTSMatchEventKind::MaterialsReclaimed:
		Request.Kind = ERTSPresentationEventKind::ReclaimCompleted;
		Request.SourceKind = ERTSPresentationSourceKind::Wreckage;
		if (const URTSWreckageSubsystem* Wreckage = World != nullptr
			? World->GetSubsystem<URTSWreckageSubsystem>()
			: nullptr)
		{
			if (const ARTSWreckage* Source = Wreckage->Find(Receipt.StableSourceId))
			{
				Request.SourceWorldLocation = Source->GetActorLocation();
			}
		}
		break;
	case ERTSMatchEventKind::MatchResolved:
		Request.Kind = Receipt.TeamId == RTSTeams::Player
			? ERTSPresentationEventKind::Defeat
			: ERTSPresentationEventKind::Victory;
		Request.SourceKind = ERTSPresentationSourceKind::Match;
		Request.SourceWorldLocation = LastDestructionLocationByTeam.FindRef(Receipt.TeamId.GetId());
		break;
	}
	Request.TargetWorldLocation = Request.SourceWorldLocation;
	Present(Request);
}

void URTSPresentationSubsystem::HandleWeaponFired(const FRTSWeaponEvent& Event)
{
	FRTSPresentationRequest Request;
	Request.Kind = ERTSPresentationEventKind::WeaponImpact;
	Request.SourceKind = Event.SourceKind == ERTSWeaponSourceKind::DefensiveTurret
		? ERTSPresentationSourceKind::TurretWeapon
		: ERTSPresentationSourceKind::UnitWeapon;
	Request.TeamId = Event.SourceTeamId;
	Request.StableSourceId = Event.StableSourceId;
	Request.StableTargetId = Event.StableTargetId;
	Request.SourceEventSequence = Event.SourceShotSequence;
	Request.SourceWorldLocation = Event.SourceWorldLocation;
	Request.TargetWorldLocation = Event.TargetWorldLocation;
	Request.Magnitude = Event.AppliedDamage;
	Present(Request);
}

void URTSPresentationSubsystem::PruneActiveEffects()
{
	ActiveEffects.RemoveAllSwap(
		[](const TWeakObjectPtr<ARTSTransientEffect>& Effect)
		{
			return !Effect.IsValid();
		},
		EAllowShrinking::No);
}

void URTSPresentationSubsystem::RetainReceipt(const FRTSPresentationReceipt& Receipt)
{
	while (RecentReceipts.Num() >= MaximumRetainedReceipts)
	{
		RecentReceipts.RemoveAt(0, 1, EAllowShrinking::No);
	}
	RecentReceipts.Add(Receipt);
}

float URTSPresentationSubsystem::LifetimeFor(const ERTSPresentationEventKind Kind) const
{
	switch (Kind)
	{
	case ERTSPresentationEventKind::SelectionAcknowledged:
	case ERTSPresentationEventKind::MoveAcknowledged:
	case ERTSPresentationEventKind::AttackAcknowledged:
		return AcknowledgementLifetimeSeconds;
	case ERTSPresentationEventKind::WeaponImpact:
		return WeaponLifetimeSeconds;
	case ERTSPresentationEventKind::Destruction:
		return DestructionLifetimeSeconds;
	case ERTSPresentationEventKind::ConstructionCompleted:
	case ERTSPresentationEventKind::ProductionCompleted:
	case ERTSPresentationEventKind::ReclaimCompleted:
		return CompletionLifetimeSeconds;
	case ERTSPresentationEventKind::Victory:
	case ERTSPresentationEventKind::Defeat:
		return MatchLifetimeSeconds;
	}
	return AcknowledgementLifetimeSeconds;
}

USoundBase* URTSPresentationSubsystem::SoundFor(const ERTSPresentationEventKind Kind) const
{
	switch (Kind)
	{
	case ERTSPresentationEventKind::SelectionAcknowledged: return SelectionSound;
	case ERTSPresentationEventKind::MoveAcknowledged: return MoveSound;
	case ERTSPresentationEventKind::AttackAcknowledged: return AttackSound;
	case ERTSPresentationEventKind::WeaponImpact: return WeaponSound;
	case ERTSPresentationEventKind::Destruction: return DestructionSound;
	case ERTSPresentationEventKind::ConstructionCompleted: return ConstructionSound;
	case ERTSPresentationEventKind::ProductionCompleted: return ProductionSound;
	case ERTSPresentationEventKind::ReclaimCompleted: return ReclaimSound;
	case ERTSPresentationEventKind::Victory: return VictorySound;
	case ERTSPresentationEventKind::Defeat: return DefeatSound;
	}
	return nullptr;
}

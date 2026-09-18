// Fill out your copyright notice in the Description page of Project Settings.


#include "ParticleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraSimCache.h"
#include "NiagaraSystemInstanceController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ParticleManager)

// Sets default values
AParticleManager::AParticleManager()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
}

// Called when the game starts or when spawned
void AParticleManager::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void AParticleManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AParticleManager::UpdateParticles()
{
	BattleParticles.RemoveAll([=](const FBattleParticle& Particle)
	{
		return !IsValid(Particle.NiagaraComponent);
	});
	for (const auto [NiagaraComponent, ParticleOwner] : BattleParticles)
	{
		if (IsValid(ParticleOwner) && ParticleOwner->IsStopped())
		{
			NiagaraComponent->SetPaused(false);
			NiagaraComponent->AdvanceSimulation(1, OneFrame / 1000);
			NiagaraComponent->SetDesiredAge(NiagaraComponent->GetDesiredAge());
			if (NiagaraComponent->IsComplete())
			{
				NiagaraComponent->Deactivate();
				NiagaraComponent->DestroyComponent();
			}
			continue;
		}
		NiagaraComponent->SetPaused(false);
		NiagaraComponent->AdvanceSimulation(1, OneFrame);
		NiagaraComponent->SetDesiredAge(NiagaraComponent->GetDesiredAge() + OneFrame);
		if (NiagaraComponent->IsComplete())
		{
			NiagaraComponent->Deactivate();
			NiagaraComponent->DestroyComponent();
		}
	}
}

void AParticleManager::PauseParticles()
{
	BattleParticles.RemoveAll([=](const FBattleParticle& Particle)
	{
		return !IsValid(Particle.NiagaraComponent);
	});
	for (const auto BattleParticle : BattleParticles)
	{
		BattleParticle.NiagaraComponent->SetPaused(true);
		if (ReplayManaged)
		{
			// Niagara's pause API declines draining systems. Keep all playback
			// effects on manually advanced instances, including those finishing.
			BattleParticle.NiagaraComponent->SetForceSolo(true);
			BattleParticle.NiagaraComponent->SetComponentTickEnabled(false);
		}
	}
}

void AParticleManager::RollbackParticles(int RollbackFrames)
{
	for (const auto BattleParticle : BattleParticles)
	{
		const auto NiagaraComponent = BattleParticle.NiagaraComponent;
		if (!IsValid(NiagaraComponent)) continue;
		const int32 RollbackTime = NiagaraComponent->GetDesiredAge() * (1 / OneFrame) - RollbackFrames;
		if (RollbackTime < 0)
		{
			NiagaraComponent->Deactivate();
			continue;
		}
		NiagaraComponent->ResetSystem();
		NiagaraComponent->AdvanceSimulation(RollbackTime, OneFrame);
	}
}


bool AParticleManager::CaptureReplayStart(ANightSkyGameState* Battle)
{
	auto DiscardConfiguration = [this]()
	{
		for (const FReplayStartupParticle& Initial : ReplayStartupParticles)
			if (IsValid(Initial.Configuration)) Initial.Configuration->DestroyComponent();
		ReplayStartupParticles.Empty();
	};
	DiscardConfiguration();
	bool Captured = false;
	ON_SCOPE_EXIT { if (!Captured) DiscardConfiguration(); };
	for (const FBattleParticle& Particle : BattleParticles)
	{
		UNiagaraComponent* Component = Particle.NiagaraComponent;
		if (!IsValid(Component)) continue;
		auto Controller = Component->GetSystemInstanceController();
		if (Controller.IsValid() && Controller->IsValid()) Controller->WaitForConcurrentTickAndFinalize();
		if (!IsValid(Component)) continue;
		Controller = Component->GetSystemInstanceController();
		const bool HasInstance = Controller.IsValid() && Controller->IsValid();

		FReplayStartupParticle& Initial = ReplayStartupParticles.AddDefaulted_GetRef();
		Initial.ComponentOuter = Component->GetOuter();
		Initial.AttachParent = Component->GetAttachParent();
		Initial.Socket = Component->GetAttachSocketName();
		Initial.WorldTransform = Component->GetComponentTransform();
		Initial.RelativeTransform = Component->GetRelativeTransform();
		if (Initial.AttachParent) Initial.ParentTransform = Initial.AttachParent->GetComponentTransform();
		Initial.ParticleOwner = Particle.ParticleOwner;
		for (ABattleObject* Object : Battle->SortedObjects)
			if (Object && Object->LinkedParticle == Component) Initial.LinkedObjects.AddUnique(Object);
		Initial.DesiredAge = Component->GetDesiredAge();
		Initial.AgeMode = uint8(Component->GetAgeUpdateMode());
		Initial.RequestedState = uint8(Component->GetRequestedExecutionState());
		Initial.ActualState = uint8(Component->GetExecutionState());
		Initial.Active = Component->IsActive();
		Initial.Complete = Component->IsComplete();
		Initial.Visible = Component->IsVisible();

		// Duplication retains authored overrides and instanced data interfaces, but
		// Niagara's native simulation buffers require their own full-attribute cache.
		Initial.Configuration = DuplicateObject<UNiagaraComponent>(Component, this,
			MakeUniqueObjectName(this, Component->GetClass(), TEXT("ReplayStartupConfiguration")));
		if (!Initial.Configuration) return false;
		Initial.Configuration->SetAutoActivate(false);
		Initial.Configuration->SetComponentTickEnabled(false);
		Initial.Configuration->SetupAttachment(nullptr);
		Initial.Configuration->SetAutoDestroy(false);
		Initial.Configuration->PoolingMethod = ENCPoolMethod::None;
		Initial.Configuration->bAutoManageAttachment = false;

		if (HasInstance && !Initial.Complete && !Controller->IsPendingSpawn())
		{
			// SimCache captures running instances. Unpause without advancing time,
			// then restore the caller's pause state even if capture fails.
			const bool WasPaused = Component->IsPaused();
			const bool WasInstancePaused = Controller->IsPaused();
			Component->SetPaused(false);
			Controller->SetPaused(false);
			ON_SCOPE_EXIT
			{
				if (IsValid(Component)) Component->SetPaused(WasPaused);
				if (Controller->IsValid()) Controller->SetPaused(WasInstancePaused);
			};
			Initial.Simulation = NewObject<UNiagaraSimCache>(this);
			FNiagaraSimCacheCreateParameters Parameters;
			Parameters.AttributeCaptureMode = ENiagaraSimCacheAttributeCaptureMode::All;
			Parameters.bAllowDataInterfaceCaching = true;
			Parameters.bAllowRebasing = false;
			if (!Initial.Simulation->BeginWrite(Parameters, Component)) return false;
			const bool Written = Initial.Simulation->WriteFrame(Component);
			const bool Finished = Initial.Simulation->EndWrite();
			if (!Written || !Finished || Initial.Simulation->GetNumFrames() != 1) return false;
		}
		else if (Initial.Active && !Initial.Complete && !HasInstance)
		{
			// An active effect still awaiting native initialization is not a captured
			// initialized state. Leave the current presentation intact and allow retry.
			return false;
		}
	}
	ReplayManaged = true;
	Captured = true;
	return true;
}

bool AParticleManager::PrepareReplayStart()
{
	if (!IsInGameThread()) return false;
	auto DiscardPrepared = [this]()
	{
		for (const FBattleParticle& Particle : PreparedReplayParticles)
			if (IsValid(Particle.NiagaraComponent)) Particle.NiagaraComponent->DestroyComponent();
		PreparedReplayParticles.Empty();
	};
	DiscardPrepared();
	for (const FReplayStartupParticle& Initial : ReplayStartupParticles)
	{
		if (!IsValid(Initial.Configuration) || !IsValid(Initial.ComponentOuter) ||
			(Initial.AttachParent && !IsValid(Initial.AttachParent)) ||
			Initial.LinkedObjects.ContainsByPredicate([](const TObjectPtr<ABattleObject>& Object) { return !IsValid(Object); }))
		{
			DiscardPrepared();
			return false;
		}
		UNiagaraComponent* Component = DuplicateObject<UNiagaraComponent>(
			Initial.Configuration, Initial.ComponentOuter, MakeUniqueObjectName(
				Initial.ComponentOuter, Initial.Configuration->GetClass(), TEXT("ReplayStartupParticle")));
		if (!Component)
		{
			DiscardPrepared();
			return false;
		}
		PreparedReplayParticles.Add(FBattleParticle(Component, Initial.ParticleOwner));
		Component->SetVisibility(false);
		Component->SetForceSolo(true);
		Component->RegisterComponentWithWorld(GetWorld());
		if (Initial.AttachParent && !Component->AttachToComponent(
			Initial.AttachParent, FAttachmentTransformRules::KeepRelativeTransform, Initial.Socket))
		{
			DiscardPrepared();
			return false;
		}
		Component->SetWorldTransform(Initial.WorldTransform);
		if (Initial.Active || Initial.Complete || Initial.Simulation)
		{
			Component->Activate(true);
			Component->SetPaused(false);
			auto Controller = Component->GetSystemInstanceController();
			if (!Controller.IsValid() || !Controller->IsValid())
			{
				DiscardPrepared();
				return false;
			}
			Controller->WaitForConcurrentTickAndFinalize();
			if (Initial.Simulation)
			{
				if (!Initial.Simulation->CanRead(Component->GetAsset()))
				{
					DiscardPrepared();
					return false;
				}
				// Niagara's async cache finalizer outlives the instance-level wait.
				// Run these two reads and their native finalization on this game-thread
				// stack before inspecting, continuing, or destroying the replacement.
				IConsoleVariable* AsyncCache = IConsoleManager::Get().FindConsoleVariable(
					TEXT("fx.Niagara.SystemSimulation.AllowASyncSimCache"));
				if (!AsyncCache)
				{
					DiscardPrepared();
					return false;
				}
				const FString PreviousAsyncValue = AsyncCache->GetString();
				const auto PreviousPriority = EConsoleVariableFlags(AsyncCache->GetFlags() & ECVF_SetByMask);
				IConsoleVariable::FSetContext CacheReadContext;
				CacheReadContext.Flags = EConsoleVariableFlags(PreviousPriority | ECVF_Set_ReplaceExistingTag);
				CacheReadContext.PriorityMode = IConsoleVariable::FSetContext::EPriorityMode::Set;
				CacheReadContext.TagMode = IConsoleVariable::FSetContext::ETagMode::ReplaceCurrent;
				CacheReadContext.MinPriority = PreviousPriority;
				CacheReadContext.MaxPriority = PreviousPriority;
				// Resolve once so both assignments replace the same priority/tag in
				// place, including an untouched constructor-priority setting.
				const auto ResolvedCacheReadContext = AsyncCache->ResolveContext(CacheReadContext);
				ON_SCOPE_EXIT { AsyncCache->Set(*PreviousAsyncValue, ResolvedCacheReadContext); };
				AsyncCache->Set(TEXT("0"), ResolvedCacheReadContext);
				if (AsyncCache->GetInt() != 0 ||
					(AsyncCache->GetFlags() & ECVF_SetByMask) != PreviousPriority)
				{
					DiscardPrepared();
					return false;
				}
				Component->SetSimCache(Initial.Simulation);
				Component->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
				// Cache ticks early-out when the instance already has the requested
				// age. Read once before the cache start, then at its exact captured age.
				// Both reads select the same stored frame and perform no simulation.
				auto ReadCacheAt = [&](float Age)
				{
					Component->SetDesiredAge(Age);
					Component->TickComponent(0.f, LEVELTICK_All, nullptr);
					if (Controller->IsValid()) Controller->WaitForConcurrentTickAndFinalize();
					return IsValid(Component) && Controller->IsValid() &&
						!Component->IsComplete() && !Controller->IsPendingSpawn();
				};
				if (!ReadCacheAt(-1.f))
				{
					DiscardPrepared();
					return false;
				}
				// Changing actual state to Active also activates every emitter. Do it
				// before the final cache read restores each emitter's captured state.
				Controller->GetSoloSystemInstance()->SetActualExecutionState(ENiagaraExecutionState(Initial.ActualState));
				if (!ReadCacheAt(Initial.Simulation->GetStartSeconds()))
				{
					DiscardPrepared();
					return false;
				}
				// All attributes were captured, so Niagara can continue this restored
				// simulation after the one-frame cache is detached without resetting.
				Component->ClearSimCache(false);
			}
			if (Initial.Complete) Component->DeactivateImmediate();
			else
			{
				if (!Initial.Simulation)
					Controller->GetSoloSystemInstance()->SetActualExecutionState(ENiagaraExecutionState(Initial.ActualState));
				Controller->GetSoloSystemInstance()->SetRequestedExecutionState(ENiagaraExecutionState(Initial.RequestedState));
			}
		}
		Component->SetActiveFlag(Initial.Active);
		Component->SetAgeUpdateMode(ENiagaraAgeUpdateMode(Initial.AgeMode));
		Component->SetDesiredAge(Initial.DesiredAge);
		Component->SetPaused(true);
		Component->SetComponentTickEnabled(false);
	}
	return true;
}

void AParticleManager::ClearReplayParticles(ANightSkyGameState* Battle)
{
	for (const FBattleParticle& Particle : BattleParticles)
	{
		for (ABattleObject* Object : Battle->SortedObjects)
			if (Object && Object->LinkedParticle == Particle.NiagaraComponent) Object->LinkedParticle = nullptr;
		if (IsValid(Particle.NiagaraComponent)) Particle.NiagaraComponent->DestroyComponent();
	}
	BattleParticles.Empty();
}

void AParticleManager::InstallReplayStart()
{
	check(PreparedReplayParticles.Num() == ReplayStartupParticles.Num());
	for (int32 Index = 0; Index < ReplayStartupParticles.Num(); ++Index)
	{
		const FReplayStartupParticle& Initial = ReplayStartupParticles[Index];
		UNiagaraComponent* Component = PreparedReplayParticles[Index].NiagaraComponent;
		if (IsValid(Initial.AttachParent))
		{
			// Gameplay rollback restores scalar positions, not scene-component
			// transforms. Restore the startup attachment before the first replay tick.
			Initial.AttachParent->SetWorldTransform(Initial.ParentTransform);
			Component->SetRelativeTransform(Initial.RelativeTransform);
		}
		for (ABattleObject* Object : Initial.LinkedObjects) Object->LinkedParticle = Component;
		Component->SetVisibility(Initial.Visible);
	}
	BattleParticles = MoveTemp(PreparedReplayParticles);
}

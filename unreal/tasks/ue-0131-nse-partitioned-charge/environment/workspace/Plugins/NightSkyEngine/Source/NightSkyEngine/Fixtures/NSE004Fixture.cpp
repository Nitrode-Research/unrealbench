#include "NSE004Fixture.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NativeGameplayTags.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Data/StateData.h"
#include "NightSkyEngine/Battle/Actors/AudioManager.h"
#include "NightSkyEngine/Battle/Actors/ParticleManager.h"
#include "NightSkyEngine/Battle/FighterRunners/FighterLocalRunner.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyBattleWidget.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE004Idle, "UnrealBench.NSE004.Idle");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE004Move0, "UnrealBench.NSE004.Move0");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE004Move1, "UnrealBench.NSE004.Move1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE004Move2, "UnrealBench.NSE004.Move2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE004Move3, "UnrealBench.NSE004.Move3");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE004Move4, "UnrealBench.NSE004.Move4");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE004Move5, "UnrealBench.NSE004.Move5");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE004Move6, "UnrealBench.NSE004.Move6");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE004Move7, "UnrealBench.NSE004.Move7");

static const FNativeGameplayTag* NSE004MoveTags[] = {
	&NSE004Move0, &NSE004Move1, &NSE004Move2, &NSE004Move3,
	&NSE004Move4, &NSE004Move5, &NSE004Move6, &NSE004Move7,
};

void UNSE004Script::Exec_Implementation()
{
	if (!Battle) return;
	APlayerObject* Owner = Parent->Player;
	FNSE004Side& Side = Battle->Sides[Owner->PlayerIndex];
	const int32 Frame = Owner->GameState->BattleState.FrameNumber;
	// ActionTime is zero on the entry call, advances once per unfrozen frame and is rolled back with the object.
	if (MoveIndex != INDEX_NONE && Parent->ActionTime == 0)
	{
		Side.Entries.Add({Frame, MoveIndex});
	}
	// A state change runs two Exec calls on one frame; count the frame once.
	if (Frame != Side.LastClockFrame)
	{
		Side.LastClockFrame = Frame;
		Side.ClockFrames++;
	}
	if (MoveIndex != INDEX_NONE && Parent->ActionTime >= RecoveryFrames)
	{
		Owner->JumpToStatePrimary(NSE004Idle);
		// Entering a state disables every custom state again; the moves are enabled once the player is free.
		for (const FGameplayTag& Tag : Side.MoveTags)
		{
			Owner->EnableCustomState(Tag, StateMachine_Primary);
		}
		return;
	}
}

bool UNSE004Script::CanEnterState_Implementation()
{
	if (!Battle || MoveIndex == INDEX_NONE) return true;
	return Battle->Sides[Parent->Player->PlayerIndex].MoveEnterable[MoveIndex];
}

FNSE004Battle::FNSE004Battle()
{
	CreateWorldAndServices();
	CreatePlayers();
	CreateObjectPool();
	ConfigureBattle();
}

FNSE004Battle::~FNSE004Battle()
{
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
}

void FNSE004Battle::CreateWorldAndServices()
{
	const auto Values = UWorld::InitializationValues()
							.AllowAudioPlayback(false)
							.CreatePhysicsScene(false)
							.RequiresHitProxies(false)
							.CreateNavigation(false)
							.CreateAISystem(false)
							.ShouldSimulatePhysics(false);
	World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num,
								&Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	Game = World->SpawnActor<ANightSkyGameState>();
	Game->GameInstance = NewObject<UNightSkyGameInstance>(World);
	Game->GameInstance->IsTraining = false;
	Game->ParticleManager = World->SpawnActor<AParticleManager>();
	Game->AudioManager = World->SpawnActor<AAudioManager>();
	Game->BattleHudActor = World->SpawnActor<ANightSkyBattleHudActor>();
	Game->BattleHudActor->TopWidget = NewObject<UNightSkyBattleWidget>(World);
	Game->BattleHudActor->BottomWidget = NewObject<UNightSkyBattleWidget>(World);
	auto* Hud = Game->BattleHudActor->TopWidget;
	Hud->P1Health.SetNum(1);
	Hud->P1RecoverableHealth.SetNum(1);
	Hud->P2Health.SetNum(1);
	Hud->P2RecoverableHealth.SetNum(1);
	Game->FighterRunner = World->SpawnActor<AFighterLocalRunner>();
	Game->MaxBattleObjects = 2;
}

// Players come first in the sorted list; the pooled objects stay inactive.
void FNSE004Battle::CreateObjectPool()
{
	for (int32 ObjectIndex = 0; ObjectIndex < Game->MaxBattleObjects; ++ObjectIndex)
	{
		auto* Object = World->SpawnActor<ABattleObject>();
		Object->GameState = Game;
		Object->ObjNumber = ObjectIndex;
		Game->Objects.Add(Object);
		Game->SortedObjects.Add(Object);
	}
}

UNSE004Script* FNSE004Battle::AddScript(APlayerObject* Owner, FGameplayTag Name, int32 MoveIndex,
										int32 RecoveryFrames, const TArray<FChargeCommand>& ChargeCommands,
										const TArray<FInputConditionList>& InputConditionLists)
{
	auto* State = NewObject<UNSE004Script>(Owner);
	State->Name = Name;
	State->Parent = Owner;
	State->Battle = this;
	State->MoveIndex = MoveIndex;
	State->RecoveryFrames = FMath::Max(RecoveryFrames, 1);
	if (MoveIndex != INDEX_NONE)
	{
		State->StateType = EStateType::Custom;
		State->CustomStateType = Name;
	}
	// Commands are in place before AddState, as they are on states created from a class default object.
	State->ChargeCommands = ChargeCommands;
	State->InputConditionLists = InputConditionLists;
	Owner->AddState(Name, State, StateMachine_Primary);
	return State;
}

void FNSE004Battle::CreatePlayers()
{
	for (int32 PlayerIndex = 0; PlayerIndex < 2; ++PlayerIndex)
	{
		auto* NewPlayer = World->SpawnActor<APlayerObject>();
		NewPlayer->GameState = Game;
		NewPlayer->Player = NewPlayer;
		NewPlayer->ObjNumber = PlayerIndex + Game->MaxBattleObjects;
		NewPlayer->PlayerIndex = PlayerIndex;
		NewPlayer->TeamIndex = 0;
		NewPlayer->CurrentHealth = NewPlayer->MaxHealth = 10000;
		NewPlayer->MiscFlags = 0;
		NewPlayer->PlayerFlags = PLF_IsOnScreen;
		NewPlayer->Gravity = 0;
		NewPlayer->Stance = ACT_Standing;
		NewPlayer->Direction = PlayerIndex == 0 ? DIR_Right : DIR_Left;
		NewPlayer->PosX = PlayerIndex == 0 ? -Game->BattleState.RoundStartPos : Game->BattleState.RoundStartPos;
		NewPlayer->PosY = 0;
		AddScript(NewPlayer, NSE004Idle, INDEX_NONE, 0);
		// The engine's round reset jumps to the universal stand state.
		AddScript(NewPlayer, FGameplayTag(State_Universal_Stand), INDEX_NONE, 0);
		Game->Players.Add(NewPlayer);
		Game->SortedObjects.Add(NewPlayer);
	}
	Player = Game->Players[0];
	Dummy = Game->Players[1];
}

void FNSE004Battle::ConfigureBattle()
{
	Game->BattleState.TeamData[0].TeamCount = 1;
	Game->BattleState.TeamData[1].TeamCount = 1;
	Game->BattleState.MainPlayer[0] = Player;
	Game->BattleState.MainPlayer[1] = Dummy;
	Player->Enemy = Dummy;
	Dummy->Enemy = Player;
	Game->BattleState.BattlePhase = EBattlePhase::Battle;
	Game->BattleState.CurrentSequenceTime = -1;
	for (auto& Channel : Game->BattleState.CommonAudioChannels)
	{
		Channel.Finished = true;
	}
	for (auto& Channel : Game->BattleState.CharaAudioChannels)
	{
		Channel.Finished = true;
	}
	PinTimers();
}

void FNSE004Battle::PinTimers() const
{
	Game->BattleState.RoundTimer = 100000;
	Game->BattleState.PauseTimer = true;
	Game->BattleState.TimeUntilRoundStart = 0;
}

int32 FNSE004Battle::AddMove(int32 RecoveryFrames, const TArray<FChargeCommand>& ChargeCommands,
							 const TArray<FInputConditionList>& InputConditionLists, int32 Side)
{
	FNSE004Side& Owner = Sides[Side];
	check(Owner.MoveTags.Num() < UE_ARRAY_COUNT(NSE004MoveTags));
	const int32 MoveIndex = Owner.MoveTags.Num();
	const FGameplayTag Tag = NSE004MoveTags[MoveIndex]->GetTag();
	Owner.MoveTags.Add(Tag);
	Owner.MoveEnterable.Add(true);
	AddScript(Game->Players[Side], Tag, MoveIndex, RecoveryFrames, ChargeCommands, InputConditionLists);
	// A custom state can be entered only while enabled; moves start enterable.
	Game->Players[Side]->EnableCustomState(Tag, StateMachine_Primary);
	return MoveIndex;
}

void FNSE004Battle::SetMoveEnterable(int32 Move, bool Enterable, int32 Side)
{
	Sides[Side].MoveEnterable[Move] = Enterable;
}

void FNSE004Battle::Step(int32 Input1, int32 Input2)
{
	Game->UpdateGameState(Input1, Input2, true);
}

int32 FNSE004Battle::CurrentMove(int32 Side) const
{
	const auto* State = Cast<UNSE004Script>(Game->Players[Side]->PrimaryStateMachine.CurrentState);
	return State ? State->MoveIndex : INDEX_NONE;
}

void FNSE004Battle::Hitstop(int32 Frames, int32 Side)
{
	Game->Players[Side]->Hitstop = Frames;
}

void FNSE004Battle::SuperFreeze(int32 Frames)
{
	Game->StartSuperFreeze(Frames, 0, Dummy);
}

void FNSE004Battle::SwitchSides()
{
	const int32 PlayerX = Player->PosX;
	Player->PosX = Dummy->PosX;
	Dummy->PosX = PlayerX;
	Player->FaceOpponent();
	Dummy->FaceOpponent();
}

void FNSE004Battle::ResetRound()
{
	// RoundInit runs the idle states once; that is not a step, so ClockFrames must not move.
	const int32 Clocks[2] = {Sides[0].ClockFrames, Sides[1].ClockFrames};
	Game->RoundInit();
	Sides[0].ClockFrames = Clocks[0];
	Sides[1].ClockFrames = Clocks[1];
	// RoundInit jumps both players to the universal stand state, disabling every custom state again.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		for (const FGameplayTag& Tag : Sides[Side].MoveTags)
		{
			Game->Players[Side]->EnableCustomState(Tag, StateMachine_Primary);
		}
	}
	PinTimers();
}

FRollbackData FNSE004Battle::Save()
{
	FRollbackData Snapshot;
	int32 Checksum = 0;
	Game->SaveGameState(Snapshot, &Checksum);
	return Snapshot;
}

void FNSE004Battle::Restore(FRollbackData& Snapshot)
{
	Game->LoadGameState(Snapshot);
	for (FNSE004Side& Side : Sides)
	{
		Side.LastClockFrame = Game->BattleState.FrameNumber;
	}
}

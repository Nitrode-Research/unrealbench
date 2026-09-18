#include "NSE006Fixture.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NativeGameplayTags.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Battle/Actors/AudioManager.h"
#include "NightSkyEngine/Battle/Actors/ParticleManager.h"
#include "NightSkyEngine/Battle/FighterRunners/FighterLocalRunner.h"
#include "NightSkyEngine/Data/CollisionData.h"
#include "NightSkyEngine/Data/StateData.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyBattleWidget.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE006Idle, "UnrealBench.NSE006.Idle");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE006Object, "UnrealBench.NSE006.Object");
// Each object keeps its own cel in its owner's collision data so authored boxes never overwrite each other.
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE006CelFighter, "UnrealBench.NSE006.Cel.Fighter");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE006CelSlot0, "UnrealBench.NSE006.Cel.Slot0");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE006CelSlot1, "UnrealBench.NSE006.Cel.Slot1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE006CelSlot2, "UnrealBench.NSE006.Cel.Slot2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE006CelSlot3, "UnrealBench.NSE006.Cel.Slot3");

static FGameplayTag CelFor(const ABattleObject* Object)
{
	if (Object->IsPlayer)
	{
		return NSE006CelFighter;
	}
	const FGameplayTag Slots[] = {NSE006CelSlot0, NSE006CelSlot1, NSE006CelSlot2, NSE006CelSlot3};
	return Slots[Object->ObjNumber % 4];
}

// The engine's rollback snapshot plus everything the fixture keeps outside the engine.
struct FNSE006Snapshot
{
	FRollbackData Rollback;
	TMap<int32, FNSE006Battle::FPendingMove> PendingMoves;
	TMap<int32, FNSE006Battle::FOnHitAction> OnHitActions;
	TMap<int32, TFunction<void()>> OnHitCustom;
	TMap<int32, TFunction<void()>> Deferred;
	bool bGuard[2] = {false, false};
	int32 InvulnerableFrames[2] = {0, 0};
	bool bPendingEntry[2] = {false, false};
	TArray<FCollisionStruct> CollisionFrames[2];
};

void UNSE006Script::Exec_Implementation()
{
	if (Battle)
	{
		Battle->ScriptExec(Parent);
	}
}
void UNSE006Script::RecordHit()
{
	if (Battle)
	{
		Battle->Record(ENSE006Event::Hit, Parent);
	}
}
void UNSE006Script::RecordCounterHit()
{
	if (Battle)
	{
		Battle->Record(ENSE006Event::CounterHit, Parent);
	}
}
void UNSE006Script::RecordBlock()
{
	if (Battle)
	{
		Battle->Record(ENSE006Event::Block, Parent);
	}
}
void UNSE006Script::RecordHitOrBlock()
{
	if (Battle)
	{
		Battle->Record(ENSE006Event::HitOrBlock, Parent);
	}
}
void UNSE006Script::RecordReceive()
{
	if (Battle)
	{
		Battle->Record(ENSE006Event::Receive, Parent);
	}
}
void UNSE006Script::RecordExit()
{
	if (Battle)
	{
		Battle->RecordStateExit(Parent);
	}
}

FCollisionBox FNSE006Battle::Box(int32 Width, int32 Height, EBoxType Type, int32 X, int32 Y)
{
	FCollisionBox CollisionBox;
	CollisionBox.SizeX = Width;
	CollisionBox.SizeY = Height;
	CollisionBox.Type = Type;
	CollisionBox.PosX = X;
	CollisionBox.PosY = Y;
	return CollisionBox;
}

FNSE006Battle::FNSE006Battle()
{
	CreateWorldAndServices();
	CreatePlayers();
	ConfigureBattle();
	CreatePool();
	Reset();
}

FNSE006Battle::~FNSE006Battle()
{
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
}

void FNSE006Battle::CreateWorldAndServices()
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
	Game->MaxBattleObjects = PoolSize;
}

UNSE006Script* FNSE006Battle::NewScript(UObject* Outer, FGameplayTag Name)
{
	auto* State = NewObject<UNSE006Script>(Outer);
	State->Name = Name;
	State->bHumanUsable = false;
	State->Battle = this;
	return State;
}

void FNSE006Battle::CreatePlayers()
{
	for (int32 PlayerIndex = 0; PlayerIndex < 2; ++PlayerIndex)
	{
		auto* Player = World->SpawnActor<APlayerObject>();
		Player->GameState = Game;
		Player->Player = Player;
		Player->ObjNumber = PoolSize + PlayerIndex;
		Player->PlayerIndex = PlayerIndex;
		Player->TeamIndex = 0;
		Player->MaxHealth = 10000;
		Player->ComboRate = 50;
		auto* Idle = NewScript(Player, NSE006Idle);
		Idle->Parent = Player;
		Player->AddState(NSE006Idle, Idle, StateMachine_Primary);
		// Every reaction the engine can buffer needs a registered state, or the fighter never leaves idle.
		const FGameplayTag Hitstun[] = {
			State_Universal_Hitstun_0, State_Universal_Hitstun_1, State_Universal_Hitstun_2,
			State_Universal_Hitstun_3, State_Universal_Hitstun_4, State_Universal_Hitstun_5,
			State_Universal_CrouchHitstun_0, State_Universal_CrouchHitstun_1, State_Universal_CrouchHitstun_2,
			State_Universal_CrouchHitstun_3, State_Universal_CrouchHitstun_4, State_Universal_CrouchHitstun_5,
			State_Universal_Launch_B, State_Universal_Launch_V, State_Universal_Launch_F,
			State_Universal_Blowback, State_Universal_Tailspin, State_Universal_Crumple,
			State_Universal_FloatingCrumpleBody, State_Universal_FloatingCrumpleHead,
			State_Universal_GuardBreakStand, State_Universal_GuardBreakCrouch, State_Universal_GuardBreakAir,
			State_Universal_ThrowLock};
		for (const FGameplayTag& Tag : Hitstun)
		{
			auto* State = NewScript(Player, Tag);
			State->Parent = Player;
			State->StateType = EStateType::Hitstun;
			Player->AddState(Tag, State, StateMachine_Primary);
		}
		const FGameplayTag Blockstun[] = {State_Universal_StandBlock, State_Universal_CrouchBlock,
										  State_Universal_AirBlock};
		for (const FGameplayTag& Tag : Blockstun)
		{
			auto* State = NewScript(Player, Tag);
			State->Parent = Player;
			State->StateType = EStateType::Blockstun;
			Player->AddState(Tag, State, StateMachine_Primary);
		}
		const FGameplayTag Neutral[] = {State_Universal_Stand, State_Universal_Crouch,
										State_Universal_StandBlockEnd, State_Universal_CrouchBlockEnd,
										State_Universal_AirBlockEnd};
		for (const FGameplayTag& Tag : Neutral)
		{
			auto* State = NewScript(Player, Tag);
			State->Parent = Player;
			Player->AddState(Tag, State, StateMachine_Primary);
		}
		Player->PrimaryStateMachine.CurrentState = Idle;
		// Template for pooled objects. AddBattleObject duplicates it per activation.
		Player->AddObjectState(NSE006Object, NewScript(Player, NSE006Object), false);
		Game->Players.Add(Player);
		Game->SortedObjects.Add(Player);
	}
	P1 = Game->Players[0];
	P2 = Game->Players[1];
}

void FNSE006Battle::ConfigureBattle()
{
	Game->BattleState.TeamData[0].TeamCount = 1;
	Game->BattleState.TeamData[1].TeamCount = 1;
	Game->BattleState.MainPlayer[0] = P1;
	Game->BattleState.MainPlayer[1] = P2;
	P1->Enemy = P2;
	P2->Enemy = P1;
	Game->BattleState.BattlePhase = EBattlePhase::Battle;
	Game->BattleState.RoundTimer = 100000;
	Game->BattleState.PauseTimer = true;
	Game->BattleState.CurrentSequenceTime = -1;
	for (auto& Channel : Game->BattleState.CommonAudioChannels)
	{
		Channel.Finished = true;
	}
	for (auto& Channel : Game->BattleState.CharaAudioChannels)
	{
		Channel.Finished = true;
	}
}

void FNSE006Battle::CreatePool()
{
	for (int32 Index = 0; Index < PoolSize; ++Index)
	{
		auto* Object = World->SpawnActor<ABattleObject>();
		Object->GameState = Game;
		Object->ObjNumber = Index;
		Game->Objects.Add(Object);
		Game->SortedObjects.Add(Object);
	}
}

void FNSE006Battle::Reset()
{
	// Fixture-side state goes first so the idle state entered below re-enables nothing from the last episode.
	Trace.Reset();
	PendingMoves.Reset();
	OnHitActions.Reset();
	OnHitCustom.Reset();
	Deferred.Reset();
	Snapshots.Reset();
	bGuard[0] = bGuard[1] = false;
	InvulnerableFrames[0] = InvulnerableFrames[1] = 0;
	for (auto* Object : Game->Objects)
	{
		if (Object->IsActive)
		{
			Object->ResetObject();
		}
	}
	ResetPlayer(P1);
	ResetPlayer(P2);
	// Entering idle above is a state entry; the episode starts with none recorded.
	StateEntries.Reset();
	bPendingEntry[0] = bPendingEntry[1] = false;
}

void FNSE006Battle::ResetPlayer(APlayerObject* Player)
{
	// A reaction buffered by the last frame of the previous episode would otherwise fire on the first frame of
	// the next one; entering it now lets the idle state below replace it.
	Player->HandleBufferedState();
	Player->PrimaryStateMachine.ForceSetState(NSE006Idle);
	// Re-arming the hit is the engine's way of forgetting who this attacker has already contacted.
	Player->EnableHit(true);
	Player->AttackFlags = 0;
	Player->PlayerFlags = PLF_IsOnScreen;
	Player->MiscFlags = 0;
	Player->InvulnFlags = 0;
	Player->StrikeInvulnerableTimer = 0;
	Player->Hitstop = 0;
	Player->StunTime = 0;
	Player->StunTimeMax = 0;
	Player->CurrentHealth = Player->MaxHealth;
	Player->RecoverableHealth = 0;
	Player->ComboCounter = 0;
	Player->ComboTimer = 0;
	Player->TotalProration = 10000;
	Player->OTGCount = 0;
	Player->HitCommon = FHitDataCommon();
	Player->NormalHit = FHitData();
	Player->CounterHit = FHitData();
	Player->ReceivedHitCommon = FHitDataCommon();
	Player->ReceivedHit = FHitData();
	Player->SuperArmorData = FSuperArmorData();
	Player->SetContactPriority(0);
	Player->AttackOwner = nullptr;
	Player->AttackTarget = nullptr;
	Player->Gravity = 0;
	Player->SpeedX = 0;
	Player->SpeedY = 0;
	Player->Inertia = 0;
	Player->Pushback = 0;
	Player->Stance = ACT_Standing;
	Player->GroundHeight = 0;
	Player->PosX = Player->PlayerIndex == 0 ? -FighterX : FighterX;
	Player->PosY = 0;
	Player->PrevPosX = Player->PosX;
	Player->PrevPosY = 0;
	Player->Direction = Player->PlayerIndex == 0 ? DIR_Right : DIR_Left;
	Player->StoredInputBuffer = FInputBuffer();
	Recorders(Player);
	Boxes(Player, {Box()});
}

void FNSE006Battle::Recorders(ABattleObject* Object) const
{
	Object->InitEventHandler(EVT_Hit, "RecordHit", 0, FGameplayTag::EmptyTag);
	Object->InitEventHandler(EVT_CounterHit, "RecordCounterHit", 0, FGameplayTag::EmptyTag);
	Object->InitEventHandler(EVT_Block, "RecordBlock", 0, FGameplayTag::EmptyTag);
	Object->InitEventHandler(EVT_HitOrBlock, "RecordHitOrBlock", 0, FGameplayTag::EmptyTag);
	Object->InitEventHandler(EVT_ReceiveHit, "RecordReceive", 0, FGameplayTag::EmptyTag);
	Object->InitEventHandler(EVT_Exit, "RecordExit", 0, FGameplayTag::EmptyTag);
}

UNSE006Script* FNSE006Battle::Script(ABattleObject* Object) const
{
	return CastChecked<UNSE006Script>(Object->IsPlayer ? Object->Player->PrimaryStateMachine.CurrentState
													   : Object->ObjectState.Get());
}

int32 FNSE006Battle::KeyOf(const ABattleObject* Object) const
{
	if (!Object)
	{
		return -1;
	}
	if (Object->IsPlayer)
	{
		return Object->Player->PlayerIndex;
	}
	return CastChecked<UNSE006Script>(Object->ObjectState.Get())->Key;
}

ABattleObject* FNSE006Battle::Find(int32 Key) const
{
	if (Key == 0 || Key == 1)
	{
		return Game->Players[Key];
	}
	for (auto* Object : Game->Objects)
	{
		if (Object->IsActive && KeyOf(Object) == Key)
		{
			return Object;
		}
	}
	return nullptr;
}

int32 FNSE006Battle::Count(ENSE006Event Kind, int32 Actor, int32 Other) const
{
	int32 Total = 0;
	for (const auto& Event : Trace)
	{
		if (Event.Kind == Kind && (Actor < 0 || Event.Actor == Actor) && (Other < 0 || Event.Other == Other))
		{
			++Total;
		}
	}
	return Total;
}

void FNSE006Battle::Record(ENSE006Event Kind, ABattleObject* Actor)
{
	const ABattleObject* Other = Kind == ENSE006Event::Receive ? Actor->AttackOwner : Actor->AttackTarget;
	const ABattleObject* Target = Kind == ENSE006Event::Receive ? Actor : Other;
	const int32 TargetHealth = Target && Target->IsPlayer ? Target->Player->CurrentHealth : -1;
	Trace.Add(FNSE006Event{Kind, KeyOf(Actor), KeyOf(Other), TargetHealth});
	if (Kind == ENSE006Event::Hit)
	{
		if (const auto* Action = OnHitActions.Find(KeyOf(Actor)))
		{
			switch (Action->Action)
			{
			case ENSE006OnHit::DisableHit:
				Actor->EnableHit(false);
				break;
			case ENSE006OnHit::Deactivate:
				Actor->DeactivateObject();
				break;
			case ENSE006OnHit::SpawnFollowUp:
				// One-shot, so an attacker that contacts several targets in a frame spawns a single follow-up.
				if (auto* FollowUp = Spawn(Actor->Player, Action->FollowUpKey, Action->X, Action->Y))
				{
					Strike(FollowUp);
					Boxes(FollowUp, {Box(4, 4, BOX_Hit)});
				}
				OnHitActions.Remove(KeyOf(Actor));
				break;
			default:
				break;
			}
		}
		TFunction<void()> Custom;
		if (OnHitCustom.RemoveAndCopyValue(KeyOf(Actor), Custom))
		{
			Custom();
		}
	}
	if (Kind == ENSE006Event::Receive && Actor->IsPlayer && InvulnerableFrames[Actor->Player->PlayerIndex] > 0)
	{
		// The timer, unlike the invulnerability flag, survives the reaction state change on the next frame.
		Actor->Player->SetStrikeInvulnerableForTime(InvulnerableFrames[Actor->Player->PlayerIndex]);
	}
}

void FNSE006Battle::ScriptExec(ABattleObject* Object)
{
	// State changes clear event handlers and the cel name, so every execution restores both.
	Recorders(Object);
	if (Object->CelName != CelFor(Object))
	{
		Object->SetCelName(CelFor(Object));
	}
	if (Object->IsPlayer)
	{
		const int32 Index = Object->Player->PlayerIndex;
		if (bPendingEntry[Index])
		{
			bPendingEntry[Index] = false;
			StateEntries.Add({Index, Object->Player->GetCurrentStateName(StateMachine_Primary)});
			// The game's universal hitstun states apply the received hit's stun on entry; pushback is cancelled
			// so nothing moves.
			if (Object->Player->PrimaryStateMachine.CurrentState->StateType == EStateType::Hitstun &&
				Object->Player->AttackOwner)
			{
				Object->Player->SetHitValues();
				Object->Player->Pushback = 0;
			}
		}
		if (bGuard[Index])
		{
			Object->Player->EnableState(ENB_Block, StateMachine_Primary);
		}
	}
	FPendingMove Pending;
	if (PendingMoves.RemoveAndCopyValue(KeyOf(Object), Pending))
	{
		Object->PosX += Pending.X;
		Object->PosY += Pending.Y;
	}
	TFunction<void()> Action;
	if (Deferred.RemoveAndCopyValue(KeyOf(Object), Action))
	{
		Action();
	}
}

void FNSE006Battle::RecordStateExit(ABattleObject* Object)
{
	if (Object->IsPlayer)
	{
		bPendingEntry[Object->Player->PlayerIndex] = true;
	}
}

void FNSE006Battle::Step(int32 Input1, int32 Input2)
{
	Game->UpdateGameState(Input1, Input2, true);
}

ABattleObject* FNSE006Battle::Spawn(APlayerObject* Owner, int32 Key, int32 X, int32 Y, int32 Slot)
{
	TArray<ABattleObject*> Held;
	for (int32 Index = 0; Index < Slot && Index < PoolSize; ++Index)
	{
		if (!Game->Objects[Index]->IsActive)
		{
			Game->Objects[Index]->IsActive = true;
			Held.Add(Game->Objects[Index]);
		}
	}
	const int32 StateIndex = Owner->ObjectStateNames.Find(NSE006Object);
	auto* Object = Game->AddBattleObject(Owner->ObjectStates[StateIndex], X, Y, Owner->Direction, StateIndex,
										 false, Owner);
	for (auto* Placeholder : Held)
	{
		Placeholder->IsActive = false;
	}
	if (!Object)
	{
		return nullptr;
	}
	Script(Object)->Key = Key;
	Script(Object)->Battle = this;
	Object->MiscFlags = 0;
	Object->Gravity = 0;
	Object->GroundHeight = 0;
	Recorders(Object);
	Boxes(Object, {Box()});
	return Object;
}

void FNSE006Battle::Strike(ABattleObject* Attacker, int32 Damage, int32 Priority, int32 AttackLevel, int32 Hitstop)
{
	// Start from the engine defaults so a re-issued strike takes the level's blockstun instead of a stale one.
	Attacker->HitCommon = FHitDataCommon();
	Attacker->NormalHit = FHitData();
	Attacker->AttackFlags |= ATK_IsAttacking | ATK_HitActive;
	if (!Attacker->IsPlayer)
	{
		Attacker->AttackFlags |= ATK_AttackProjectileAttribute;
	}
	Attacker->HitCommon.AttackLevel = AttackLevel;
	Attacker->HitCommon.BlockType = BLK_Mid;
	Attacker->HitCommon.ChipDamagePercent = 10;
	Attacker->HitCommon.EnemyBlockstopModifier = 0;
	Attacker->NormalHit.Damage = Damage;
	Attacker->NormalHit.Hitstop = Hitstop;
	Attacker->NormalHit.EnemyHitstopModifier = 0;
	Attacker->NormalHit.Hitstun = 10;
	Attacker->NormalHit.Untech = 10;
	Attacker->NormalHit.InitialProration = 100;
	Attacker->NormalHit.ForcedProration = 100;
	Attacker->NormalHit.MinimumDamagePercent = 0;
	Attacker->NormalHit.GroundHitAction = HACT_GroundNormal;
	Attacker->NormalHit.AirHitAction = HACT_GroundNormal;
	Attacker->CounterHit = Attacker->NormalHit;
	Attacker->SetContactPriority(Priority);
}

void FNSE006Battle::Boxes(ABattleObject* Object, const TArray<FCollisionBox>& InBoxes)
{
	auto* Data = Object->Player->CollisionData;
	if (!Data)
	{
		Data = NewObject<UCollisionData>(Object->Player);
		Object->Player->CollisionData = Data;
	}
	const FGameplayTag Cel = CelFor(Object);
	FCollisionStruct* Frame = Data->CollisionFrames.FindByPredicate(
		[Cel](const FCollisionStruct& Candidate) { return Candidate.CelName == Cel; });
	if (!Frame)
	{
		Frame = &Data->CollisionFrames.AddDefaulted_GetRef();
		Frame->CelName = Cel;
	}
	Frame->Boxes = InBoxes;
	Object->SetCelName(Cel);
}

void FNSE006Battle::Armor(ABattleObject* Target, int32 Hits, bool bStrikes, bool bProjectiles,
						  bool bDisableIncomingHit)
{
	FSuperArmorData Data = FSuperArmorData();
	Data.Type = ARM_Guard;
	Data.bArmorStrike = bStrikes;
	Data.bArmorProjectile = bProjectiles;
	Data.bArmorDisableIncomingHit = bDisableIncomingHit;
	Data.ArmorHits = Hits;
	Target->SuperArmorData = Data;
}

void FNSE006Battle::Guard(APlayerObject* Player, bool bEnabled)
{
	bGuard[Player->PlayerIndex] = bEnabled;
}

void FNSE006Battle::Move(ABattleObject* Object, int32 DeltaX, int32 DeltaY)
{
	PendingMoves.Add(KeyOf(Object), {DeltaX, DeltaY});
}

void FNSE006Battle::OnHit(ABattleObject* Attacker, ENSE006OnHit Action, int32 FollowUpKey, int32 FollowUpX,
						  int32 FollowUpY)
{
	OnHitActions.Add(KeyOf(Attacker), {Action, FollowUpKey, FollowUpX, FollowUpY});
}

void FNSE006Battle::OnHitDo(ABattleObject* Attacker, TFunction<void()> Action)
{
	OnHitCustom.Add(KeyOf(Attacker), MoveTemp(Action));
}

void FNSE006Battle::InvulnerableOnReceive(APlayerObject* Player, int32 Frames)
{
	InvulnerableFrames[Player->PlayerIndex] = Frames;
}

void FNSE006Battle::Defer(ABattleObject* Object, TFunction<void()> Action)
{
	Deferred.Add(KeyOf(Object), MoveTemp(Action));
}

int32 FNSE006Battle::Snapshot()
{
	auto Snap = MakeShared<FNSE006Snapshot>();
	int32 Checksum = 0;
	Game->SaveGameState(Snap->Rollback, &Checksum);
	Snap->PendingMoves = PendingMoves;
	Snap->OnHitActions = OnHitActions;
	Snap->OnHitCustom = OnHitCustom;
	Snap->Deferred = Deferred;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Snap->bGuard[Index] = bGuard[Index];
		Snap->InvulnerableFrames[Index] = InvulnerableFrames[Index];
		Snap->bPendingEntry[Index] = bPendingEntry[Index];
		Snap->CollisionFrames[Index] = Game->Players[Index]->CollisionData->CollisionFrames;
	}
	Snapshots.Add(Snap);
	return Snapshots.Num() - 1;
}

void FNSE006Battle::Restore(int32 Handle)
{
	const auto& Snap = Snapshots[Handle];
	Game->LoadGameState(Snap->Rollback);
	PendingMoves = Snap->PendingMoves;
	OnHitActions = Snap->OnHitActions;
	OnHitCustom = Snap->OnHitCustom;
	Deferred = Snap->Deferred;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		bGuard[Index] = Snap->bGuard[Index];
		InvulnerableFrames[Index] = Snap->InvulnerableFrames[Index];
		bPendingEntry[Index] = Snap->bPendingEntry[Index];
		Game->Players[Index]->CollisionData->CollisionFrames = Snap->CollisionFrames[Index];
	}
	// Objects only refresh their boxes when their cel is set, so restored objects re-read the restored cel.
	for (auto* Object : Game->Objects)
	{
		if (Object->IsActive)
		{
			Object->SetCelName(CelFor(Object));
		}
	}
	Trace.Reset();
	StateEntries.Reset();
}

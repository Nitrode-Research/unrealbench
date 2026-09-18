#include "NSE010Fixture.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NativeGameplayTags.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Actors/AudioManager.h"
#include "NightSkyEngine/Battle/Actors/ParticleManager.h"
#include "NightSkyEngine/Battle/FighterRunners/FighterLocalRunner.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Battle/Script/StateMachine.h"
#include "NightSkyEngine/Data/BattleExtensionData.h"
#include "NightSkyEngine/Data/CollisionData.h"
#include "NightSkyEngine/Data/StateData.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyBattleWidget.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE010Idle, "UnrealBench.NSE010.Idle");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE010Shot, "UnrealBench.NSE010.Shot");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE010BodyCel, "UnrealBench.NSE010.BodyCel");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE010ShotCel, "UnrealBench.NSE010.ShotCel");

namespace
{
TMap<const ANightSkyGameState*, FNSE010Battle*> GNSE010Battles;

FCollisionBox MakeBox(EBoxType Type, int32 Width, int32 Height, int32 X, int32 Y)
{
	FCollisionBox Box;
	Box.Type = Type;
	Box.SizeX = Width;
	Box.SizeY = Height;
	Box.PosX = X;
	Box.PosY = Y;
	return Box;
}
} // namespace

void UNSE010Script::Exec_Implementation()
{
	Counter++;
	ANightSkyGameState* Game = Parent->GameState;
	if (Parent->IsPlayer)
	{
		// Entering any state clears the cel; keep the body hurtbox in every state.
		if (Parent->CelName != NSE010BodyCel) Parent->SetCelName(NSE010BodyCel);
		const int32 In = Parent->Player->Inputs;
		if (In & INP_Right) Parent->PosX += WalkSpeed;
		if (In & INP_Left) Parent->PosX -= WalkSpeed;
		const bool bADown = (In & INP_A) != 0;
		if (bShootOnA && bADown && !bAWasDown)
		{
			Parent->AddBattleObject(NSE010Shot, 60000, 50000);
		}
		bAWasDown = bADown;
		if (FollowStoredIndex >= 0 && FollowStoredIndex < 16)
		{
			if (const ABattleObject* Stored = Parent->Player->StoredBattleObjects[FollowStoredIndex];
				Stored && Stored->IsActive)
			{
				Parent->PosY = Stored->PosY;
			}
		}
	}
	else
	{
		if (!bArmed) Arm();
		Parent->PosX += Parent->Direction == DIR_Left ? -Travel : Travel;
	}
	Parent->PosX += DriftX;
	Parent->PosY += DriftY;
	if (RandomWalk > 0 && Game)
	{
		Parent->PosX += Game->BattleState.RandomManager.RandRange(-RandomWalk, RandomWalk);
	}
	if (Counter == FreezeAtCounter && Game)
	{
		Game->StartSuperFreeze(FreezeDuration, FreezeSelfDuration, Parent);
	}
}

void UNSE010Script::Arm()
{
	bArmed = true;
	Parent->MiscFlags = 0;
	Parent->Gravity = 0;
	Parent->AttackFlags = ATK_IsAttacking | ATK_HitActive | ATK_AttackProjectileAttribute;
	Parent->NormalHit.Damage = Damage;
	Parent->NormalHit.Hitstop = HitstopFrames;
	Parent->NormalHit.EnemyHitstopModifier = 0;
	Parent->NormalHit.InitialProration = 100;
	Parent->NormalHit.ForcedProration = 100;
	Parent->NormalHit.MinimumDamagePercent = 100;
	Parent->NormalHit.GroundHitAction = HACT_None;
	Parent->NormalHit.AirHitAction = HACT_None;
	Parent->HitCommon.ChipDamagePercent = 10;
	Parent->HitCommon.EnemyBlockstopModifier = 0;
	Parent->InitEventHandler(EVT_Hit, "RecordHit", 0, FGameplayTag::EmptyTag);
	Parent->InitEventHandler(EVT_Block, "RecordHit", 0, FGameplayTag::EmptyTag);
	Parent->InitEventHandler(EVT_ReceiveHit, "RecordReceive", 0, FGameplayTag::EmptyTag);
	Parent->SetCelName(NSE010ShotCel);
	Parent->EnableHit(true);
}

void UNSE010Script::RecordHit()
{
	if (FNSE010Battle* Battle = FNSE010Battle::Find(Parent->GameState))
	{
		Battle->Trace.Add(TraceId);
	}
	if (bDeactivateOnHit)
	{
		Parent->DeactivateObject();
	}
}

void UNSE010Script::RecordReceive()
{
	if (FNSE010Battle* Battle = FNSE010Battle::Find(Parent->GameState))
	{
		Battle->Trace.Add(-(TraceId + 1));
	}
}

void UNSE010Script::RecordFreezeEnd()
{
	if (FNSE010Battle* Battle = FNSE010Battle::Find(Parent->GameState))
	{
		Battle->Trace.Add(5000 + TraceId);
	}
}

void UNSE010Extension::Exec_Implementation()
{
	Ticks++;
	if (LiftPeriod > 0 && Ticks % LiftPeriod == 0 && Parent && Parent->Players.Num() > 0)
	{
		Parent->Players[0]->PosY += LiftAmount;
	}
}

FNSE010Battle::FNSE010Battle(const FNSE010Lineup& Lineup)
{
	CreateWorldAndServices(Lineup);
	CreatePlayers(Lineup);
	CreatePool(Lineup);
	ConfigureBattle(Lineup);
	GNSE010Battles.Add(Game, this);
	int32 Checksum = 0;
	Game->SaveGameState(InitialSnapshot, &Checksum);
	ForEachRegisteredScript([this](USerializableObj* Script) { InitialScriptData.Add(Script->SaveForRollback()); });
}

FNSE010Battle::~FNSE010Battle()
{
	GNSE010Battles.Remove(Game);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
}

FNSE010Battle* FNSE010Battle::Find(const ANightSkyGameState* InGame)
{
	FNSE010Battle* const* Found = GNSE010Battles.Find(InGame);
	return Found ? *Found : nullptr;
}

void FNSE010Battle::CreateWorldAndServices(const FNSE010Lineup& Lineup)
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
	Game->GameInstance->BattleData.StartRoundTimer = 99;
	Game->ParticleManager = World->SpawnActor<AParticleManager>();
	Game->AudioManager = World->SpawnActor<AAudioManager>();
	Game->BattleHudActor = World->SpawnActor<ANightSkyBattleHudActor>();
	Game->BattleHudActor->TopWidget = NewObject<UNightSkyBattleWidget>(World);
	Game->BattleHudActor->BottomWidget = NewObject<UNightSkyBattleWidget>(World);
	UNightSkyBattleWidget* Hud = Game->BattleHudActor->TopWidget;
	Hud->P1Health.SetNum(Lineup.TeamCountP1);
	Hud->P1RecoverableHealth.SetNum(Lineup.TeamCountP1);
	Hud->P2Health.SetNum(Lineup.TeamCountP2);
	Hud->P2RecoverableHealth.SetNum(Lineup.TeamCountP2);
	Game->FighterRunner = World->SpawnActor<AFighterLocalRunner>();
	Game->MaxBattleObjects = Lineup.PoolCapacity;
	if (Lineup.bWithExtension)
	{
		UNSE010Extension* Extension = NewObject<UNSE010Extension>(Game);
		Extension->Parent = Game;
		Extension->Name = BattleExtension_Update;
		Game->BattleExtensions.Add(Extension);
		Game->BattleExtensionNames.Add(BattleExtension_Update);
	}
}

UNSE010Script* FNSE010Battle::NewScript(APlayerObject* Owner, FGameplayTag Name) const
{
	UNSE010Script* State = NewObject<UNSE010Script>(Owner);
	State->Name = Name;
	State->bHumanUsable = false;
	State->Parent = Owner;
	State->TraceId = 1 + Players.Find(Owner);
	return State;
}

void FNSE010Battle::CreatePlayers(const FNSE010Lineup& Lineup)
{
	const int32 Total = Lineup.TeamCountP1 + Lineup.TeamCountP2;
	for (int32 Index = 0; Index < Total; ++Index)
	{
		const bool bSecondSide = Index >= Lineup.TeamCountP1;
		UClass* Class = bSecondSide && Lineup.bAltClassP2 ? ANSE010AltPlayer::StaticClass()
														  : APlayerObject::StaticClass();
		APlayerObject* Player = World->SpawnActor<APlayerObject>(Class);
		Player->GameState = Game;
		Player->Player = Player;
		Player->ObjNumber = Lineup.PoolCapacity + Index;
		Player->PlayerIndex = bSecondSide ? 1 : 0;
		Player->TeamIndex = bSecondSide ? Index - Lineup.TeamCountP1 : Index;
		Player->CurrentHealth = Player->MaxHealth = 10000;
		Player->MiscFlags = 0;
		Player->PlayerFlags = Player->TeamIndex == 0 ? PLF_IsOnScreen : 0;
		Player->Gravity = 0;
		Player->Direction = bSecondSide ? DIR_Left : DIR_Right;
		Player->PosX = bSecondSide ? 100000 : -100000;
		Player->PosY = 0;
		Players.Add(Player);

		UNSE010Script* Idle = NewScript(Player, NSE010Idle);
		Player->AddState(NSE010Idle, Idle, StateMachine_Primary);
		for (const FGameplayTag& Tag :
			 {FGameplayTag(State_Universal_Stand), FGameplayTag(State_Universal_StandBlock),
			  FGameplayTag(State_Universal_StandBlockEnd), FGameplayTag(State_Universal_AirBlock),
			  FGameplayTag(State_Universal_AirBlockEnd), FGameplayTag(State_Universal_RoundWin),
			  FGameplayTag(State_Universal_RoundLose), FGameplayTag(State_Universal_MatchWin),
			  FGameplayTag(State_Universal_JumpLanding), FGameplayTag(State_Universal_TagIn)})
		{
			Player->AddState(Tag, NewScript(Player, Tag), StateMachine_Primary);
		}
		Player->PrimaryStateMachine.CurrentState = Idle;
		Player->InitEventHandler(EVT_ReceiveHit, "RecordReceive", 0, FGameplayTag::EmptyTag);
		Player->InitEventHandler(EVT_SuperFreezeEnd, "RecordFreezeEnd", 0, FGameplayTag::EmptyTag);

		UNSE010Script* Shot = NewScript(Player, NSE010Shot);
		Shot->Travel = 4000;
		Shot->TraceId = 100 + 10 * Index;
		Player->AddObjectState(NSE010Shot, Shot, false);

		UCollisionData* Collision = NewObject<UCollisionData>(Player);
		FCollisionStruct Body;
		Body.CelName = NSE010BodyCel;
		Body.Boxes = {MakeBox(BOX_Hurt, 60000, 100000, 0, 50000)};
		Collision->CollisionFrames.Add(Body);
		FCollisionStruct ShotFrame;
		ShotFrame.CelName = NSE010ShotCel;
		ShotFrame.Boxes = {MakeBox(BOX_Hit, 20000, 20000, 0, 0)};
		Collision->CollisionFrames.Add(ShotFrame);
		Player->CollisionData = Collision;
		Player->SetCelName(NSE010BodyCel);

		Game->Players.Add(Player);
		Game->SortedObjects.Add(Player);
	}
}

void FNSE010Battle::CreatePool(const FNSE010Lineup& Lineup)
{
	for (int32 Index = 0; Index < Lineup.PoolCapacity; ++Index)
	{
		ABattleObject* Object = World->SpawnActor<ABattleObject>();
		Object->GameState = Game;
		Object->ObjNumber = Index;
		// Start every slot in the engine's freed state, so a never-used slot and a reused one agree.
		Object->ResetObject();
		Game->Objects.Add(Object);
		Game->SortedObjects.Add(Object);
	}
}

void FNSE010Battle::ConfigureBattle(const FNSE010Lineup& Lineup)
{
	FBattleState& State = Game->BattleState;
	State.TeamData[0].TeamCount = Lineup.TeamCountP1;
	State.TeamData[0].CooldownTimer.AddDefaulted(Lineup.TeamCountP1);
	State.TeamData[1].TeamCount = Lineup.TeamCountP2;
	State.TeamData[1].CooldownTimer.AddDefaulted(Lineup.TeamCountP2);
	State.MainPlayer[0] = Players[0];
	State.MainPlayer[1] = Players[Lineup.TeamCountP1];
	State.MaxGauge = {10000};
	State.GaugeP1.AddDefaulted(1);
	State.GaugeP2.AddDefaulted(1);
	State.BattleFormat = EBattleFormat::Rounds;
	State.MaxRoundCount = 2;
	State.BattlePhase = EBattlePhase::Battle;
	State.RoundCount = 1;
	State.RoundTimer = 99 * 60;
	State.PauseTimer = false;
	State.TimeUntilRoundStart = 0;
	State.CurrentSequenceTime = -1;
	State.RandomManager.Reseed(0x5EED0133);
	State.ScreenData.TargetObjects.AddUnique(State.MainPlayer[0]);
	State.ScreenData.TargetObjects.AddUnique(State.MainPlayer[1]);
	Game->AssignEnemy();
}

void FNSE010Battle::Step(int32 Input1, int32 Input2)
{
	Game->UpdateGameState(Input1, Input2, true);
}

void FNSE010Battle::ForEachRegisteredScript(TFunctionRef<void(USerializableObj*)> Fn) const
{
	for (APlayerObject* Player : Players)
	{
		for (UState* State : Player->PrimaryStateMachine.States) Fn(State);
		for (UState* State : Player->ObjectStates) Fn(State);
		for (UState* State : Player->CommonObjectStates) Fn(State);
	}
}

void FNSE010Battle::Reset()
{
	Game->LoadGameState(InitialSnapshot);
	// The rollback snapshot only carries the current state of on-screen players;
	// registered scripts and templates are restored here so knobs set by a test do not linger.
	int32 Index = 0;
	ForEachRegisteredScript([this, &Index](USerializableObj* Script) { Script->LoadForRollback(InitialScriptData[Index++]); });
	// Slot order as built: players first, then the pool in slot order.
	Game->SortedObjects.Reset();
	for (APlayerObject* Player : Players) Game->SortedObjects.Add(Player);
	for (ABattleObject* Object : Game->Objects) Game->SortedObjects.Add(Object);
	Trace.Reset();
}

UNSE010Script* FNSE010Battle::Script(ABattleObject* Object) const
{
	return CastChecked<UNSE010Script>(Object->IsPlayer ? Object->Player->PrimaryStateMachine.CurrentState
													   : Object->ObjectState.Get());
}

UNSE010Script* FNSE010Battle::ShotTemplate(APlayerObject* Owner) const
{
	const int32 Index = Owner->ObjectStateNames.Find(NSE010Shot);
	return CastChecked<UNSE010Script>(Owner->ObjectStates[Index]);
}

ABattleObject* FNSE010Battle::Spawn(APlayerObject* Owner, int32 X, int32 Y)
{
	const int32 Index = Owner->ObjectStateNames.Find(NSE010Shot);
	return Game->AddBattleObject(Owner->ObjectStates[Index], X, Y, Owner->Direction, Index, false, Owner);
}

void FNSE010Battle::Store(APlayerObject* Owner, ABattleObject* Object, int32 Index)
{
	Owner->AddBattleObjectToStorage(Object, Index);
}

UNSE010Extension* FNSE010Battle::Extension() const
{
	return Game->BattleExtensions.Num() > 0 ? Cast<UNSE010Extension>(Game->BattleExtensions[0]) : nullptr;
}

#include "NSE005Fixture.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Battle/Actors/ParticleManager.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyBattleWidget.h"
#include "NightSkyEngine/Data/CollisionData.h"
#include "NativeGameplayTags.h"
#include "NightSkyEngine/Battle/Actors/AudioManager.h"
#include "NightSkyEngine/Battle/FighterRunners/FighterLocalRunner.h"
#include "NightSkyEngine/Data/StateData.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE005Idle, "UnrealBench.NSE005.Idle");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE005Cel, "UnrealBench.NSE005.Cel");

void UNSE005Script::Exec_Implementation()
{
	if (bSpawnTargetPending && Battle)
	{
		bSpawnTargetPending = false;
		Battle->SpawnTarget(SpawnX);
	}
	if (bSpawnPending && Battle)
	{
		bSpawnPending = false;
		Battle->Spawn(SpawnX);
	}
	if (bTeleportPending)
	{
		Parent->TeleportBattlePosition(TeleportDestinationX, TeleportDestinationY);
		bTeleportPending = false;
	}
	Parent->PosX += PendingMoveX;
	Parent->PosY += PendingMoveY;
	PendingMoveX = 0;
	PendingMoveY = 0;
}
void UNSE005Script::RecordHit()
{
	if (Trace && Parent->AttackTarget)
	{
		Trace->Add(Parent->AttackTarget->ContactOrderKey);
	}
	if (OnContactAction == ENSE005ContactAction::DeactivateProjectile)
	{
		Parent->DeactivateObject();
	}
	if (OnContactAction == ENSE005ContactAction::DisableHit)
	{
		Parent->EnableHit(false);
	}
	if (OnContactAction == ENSE005ContactAction::MakeTargetInvulnerable && ChangeTarget)
	{
		ChangeTarget->Player->SetStrikeInvulnerable(true);
	}
}
void UNSE005Script::RecordBlock()
{
	RecordHit();
}
void UNSE005Script::RecordReceive()
{
	if (Trace)
	{
		Trace->Add(-Parent->ContactOrderKey - 1);
	}
}
FCollisionBox FNSE005Battle::Box(int32 Width, int32 Height, EBoxType Type, int32 X, int32 Y)
{
	FCollisionBox CollisionBox;
	CollisionBox.SizeX = Width;
	CollisionBox.SizeY = Height;
	CollisionBox.Type = Type;
	CollisionBox.PosX = X;
	CollisionBox.PosY = Y;
	return CollisionBox;
}
FNSE005Battle::FNSE005Battle(int32 TargetCount, bool bConfigureSweep)
{
	CreateWorldAndServices(TargetCount);
	CreatePlayers(TargetCount);
	ConfigureBattle(TargetCount);
	CreateProjectilePool(bConfigureSweep);
}

void FNSE005Battle::CreateWorldAndServices(int32 TargetCount)
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
	Hud->P2Health.SetNum(TargetCount);
	Hud->P2RecoverableHealth.SetNum(TargetCount);
	Game->FighterRunner = World->SpawnActor<AFighterLocalRunner>();
	Game->MaxBattleObjects = 2;
}

void FNSE005Battle::CreatePlayers(int32 TargetCount)
{
	for (int32 PlayerIndex = 0; PlayerIndex <= TargetCount; ++PlayerIndex)
	{
		auto* Player = World->SpawnActor<APlayerObject>();
		Player->GameState = Game;
		Player->Player = Player;
		Player->ObjNumber = PlayerIndex + 2;
		Player->PlayerIndex = PlayerIndex == 0 ? 0 : 1;
		Player->TeamIndex = PlayerIndex == 0 ? 0 : PlayerIndex - 1;
		Player->CurrentHealth = Player->MaxHealth = 10000;
		Player->MiscFlags = 0;
		Player->PlayerFlags = PLF_IsOnScreen;
		Player->Gravity = 0;
		Player->Direction = DIR_Right;
		Player->PosX = PlayerIndex == 0 ? -100000 : 50;
		Player->PosY = 100;
		Player->SetContactOrderKey(PlayerIndex);
		auto* State = NewObject<UNSE005Script>(Player);
		State->Name = NSE005Idle;
		State->bHumanUsable = false;
		State->Parent = Player;
		State->Trace = &Trace;
		State->Battle = this;
		Player->AddState(NSE005Idle, State, StateMachine_Primary);
		for (FGameplayTag Tag :
			 {FGameplayTag(State_Universal_Stand), FGameplayTag(State_Universal_StandBlock),
			  FGameplayTag(State_Universal_StandBlockEnd), FGameplayTag(State_Universal_AirBlock),
			  FGameplayTag(State_Universal_AirBlockEnd)})
		{
			auto* Extra = NewObject<UNSE005Script>(Player);
			Extra->Name = Tag;
			Extra->bHumanUsable = false;
			Extra->Parent = Player;
			Extra->Trace = &Trace;
			Extra->Battle = this;
			Player->AddState(Tag, Extra, StateMachine_Primary);
		}
		Player->PrimaryStateMachine.CurrentState = State;
		Player->InitEventHandler(EVT_ReceiveHit, "RecordReceive", 0, FGameplayTag::EmptyTag);
		Game->Players.Add(Player);
		Game->SortedObjects.Add(Player);
		if (PlayerIndex)
		{
			Targets.Add(Player);
		}
	}
}

void FNSE005Battle::ConfigureBattle(int32 TargetCount)
{
	Game->BattleState.TeamData[0].TeamCount = 1;
	Game->BattleState.TeamData[1].TeamCount = TargetCount;
	Game->BattleState.MainPlayer[0] = Game->Players[0];
	Game->BattleState.MainPlayer[1] = Targets[0];
	for (auto* Player : Game->Players)
	{
		Player->Enemy = Game->Players[Player->PlayerIndex == 0 ? 1 : 0];
	}
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

void FNSE005Battle::CreateProjectilePool(bool bConfigureSweep)
{
	Projectile = World->SpawnActor<ABattleObject>();
	Projectile->GameState = Game;
	Projectile->ObjNumber = 0;
	Game->Objects.Add(Projectile);
	Game->SortedObjects.Add(Projectile);
	auto* Spare = World->SpawnActor<ABattleObject>();
	Spare->GameState = Game;
	Spare->ObjNumber = 1;
	Game->Objects.Add(Spare);
	Game->SortedObjects.Add(Spare);
	for (auto* Player : Targets)
	{
		Boxes(Player, {Box()});
	}
	Spawn(0, 100, true, false, 1, bConfigureSweep);
}

FNSE005Battle::~FNSE005Battle()
{
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
}
UNSE005Script* FNSE005Battle::Script(ABattleObject* Object) const
{
	return CastChecked<UNSE005Script>(Object->IsPlayer ? Object->Player->PrimaryStateMachine.CurrentState
													   : Object->ObjectState.Get());
}
void FNSE005Battle::Boxes(ABattleObject* Object, const TArray<FCollisionBox>& InBoxes)
{
	auto* Data = NewObject<UCollisionData>(Object->Player);
	FCollisionStruct Frame;
	Frame.CelName = NSE005Cel;
	Frame.Boxes = InBoxes;
	Data->CollisionFrames.Add(Frame);
	Object->Player->CollisionData = Data;
	Object->SetCelName(NSE005Cel);
	Object->GetBoxes();
}
void FNSE005Battle::Move(ABattleObject* Object, int32 X, int32 Y)
{
	Script(Object)->PendingMoveX = X;
	Script(Object)->PendingMoveY = Y;
}
void FNSE005Battle::Teleport(ABattleObject* Object, int32 X, int32 Y, int32 MoveAfterTeleportX)
{
	auto* State = Script(Object);
	State->bTeleportPending = true;
	State->TeleportDestinationX = X;
	State->TeleportDestinationY = Y;
	State->PendingMoveX = MoveAfterTeleportX;
}
void FNSE005Battle::Step(int32 Input1, int32 Input2)
{
	Game->UpdateGameState(Input1, Input2, true);
}
void FNSE005Battle::Spawn(int32 X, int32 Y, bool Sweep, bool Piercing, int32 Limit, bool bConfigureSweep)
{
	Projectile->ResetObject();
	auto* State = NewObject<UNSE005Script>(Game->Players[0]);
	State->Name = NSE005Idle;
	State->Trace = &Trace;
	State->Battle = this;
	if (Game->Players[0]->ObjectStateNames.IsEmpty())
	{
		Game->Players[0]->AddObjectState(NSE005Idle, State, false);
	}
	Game->AddBattleObject(State, X, Y, DIR_Right, 0, false, Game->Players[0]);
	Script(Projectile)->Trace = &Trace;
	Script(Projectile)->Battle = this;
	if (bConfigureSweep)
	{
		Projectile->ConfigureSweptProjectile(Sweep, Piercing, Limit);
	}
	Projectile->AttackFlags = ATK_IsAttacking | ATK_HitActive | ATK_AttackProjectileAttribute;
	Projectile->MiscFlags = 0;
	Projectile->Gravity = 0;
	Projectile->EnableHit(true);
	Projectile->NormalHit.Damage = 100;
	Projectile->NormalHit.Hitstop = 3;
	Projectile->NormalHit.EnemyHitstopModifier = 0;
	Projectile->NormalHit.InitialProration = 100;
	Projectile->NormalHit.ForcedProration = 100;
	Projectile->NormalHit.MinimumDamagePercent = 100;
	Projectile->NormalHit.GroundHitAction = HACT_None;
	Projectile->NormalHit.AirHitAction = HACT_None;
	Projectile->HitCommon.ChipDamagePercent = 10;
	Projectile->HitCommon.EnemyBlockstopModifier = 0;
	Projectile->InitEventHandler(EVT_Hit, "RecordHit", 0, FGameplayTag::EmptyTag);
	Projectile->InitEventHandler(EVT_Block, "RecordBlock", 0, FGameplayTag::EmptyTag);
	Boxes(Projectile, {Box(4, 4, BOX_Hit)});
}

ABattleObject* FNSE005Battle::SpawnTarget(int32 X, int32 Y)
{
	auto* State = NewObject<UNSE005Script>(Targets[0]);
	State->Trace = &Trace;
	State->Battle = this;
	auto* Target = Game->AddBattleObject(State, X, Y, DIR_Right, 0, false, Targets[0]);
	if (!Target)
	{
		// Report through the test instead of aborting the editor: a candidate whose object
		// activation cannot provide a target must fail its assertions, not crash the suite.
		return nullptr;
	}
	Script(Target)->Trace = &Trace;
	Script(Target)->Battle = this;
	Target->SetContactOrderKey(99);
	Target->MiscFlags = 0;
	Target->Gravity = 0;
	Target->InitEventHandler(EVT_ReceiveHit, "RecordReceive", 0, FGameplayTag::EmptyTag);
	Boxes(Target, {Box()});
	return Target;
}

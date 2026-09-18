#include "NSE025Fixture.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NativeGameplayTags.h"
#include "NightSkyEngine/Battle/Actors/AudioManager.h"
#include "NightSkyEngine/Battle/Actors/ParticleManager.h"
#include "NightSkyEngine/Battle/FighterRunners/FighterLocalRunner.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Data/CollisionData.h"
#include "NightSkyEngine/Data/StateData.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyBattleWidget.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025CelIdle, "UnrealBench.NSE025.Cel.Idle");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025CelHurt, "UnrealBench.NSE025.Cel.Hurt");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Tech, "UnrealBench.NSE025.Tech");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Move0, "UnrealBench.NSE025.Move.0");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Move1, "UnrealBench.NSE025.Move.1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Move2, "UnrealBench.NSE025.Move.2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Move3, "UnrealBench.NSE025.Move.3");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Move4, "UnrealBench.NSE025.Move.4");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Move5, "UnrealBench.NSE025.Move.5");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Move6, "UnrealBench.NSE025.Move.6");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Move7, "UnrealBench.NSE025.Move.7");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Active0, "UnrealBench.NSE025.Cel.Active.0");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Active1, "UnrealBench.NSE025.Cel.Active.1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Active2, "UnrealBench.NSE025.Cel.Active.2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Active3, "UnrealBench.NSE025.Cel.Active.3");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Active4, "UnrealBench.NSE025.Cel.Active.4");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Active5, "UnrealBench.NSE025.Cel.Active.5");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Active6, "UnrealBench.NSE025.Cel.Active.6");
UE_DEFINE_GAMEPLAY_TAG_STATIC(NSE025Active7, "UnrealBench.NSE025.Cel.Active.7");

namespace
{
constexpr int32 HurtWidth = 100000;
constexpr int32 BoxHeight = 200000;
constexpr int32 FixtureHealth = 100000;

FGameplayTag ActiveCel(int32 Index)
{
	switch (Index)
	{
	case 0: return NSE025Active0;
	case 1: return NSE025Active1;
	case 2: return NSE025Active2;
	case 3: return NSE025Active3;
	case 4: return NSE025Active4;
	case 5: return NSE025Active5;
	case 6: return NSE025Active6;
	case 7: return NSE025Active7;
	default: return FGameplayTag();
	}
}

FCollisionBox Box(EBoxType Type, int32 X, int32 Y, int32 Width, int32 Height)
{
	FCollisionBox CollisionBox;
	CollisionBox.Type = Type;
	CollisionBox.PosX = X;
	CollisionBox.PosY = Y;
	CollisionBox.SizeX = Width;
	CollisionBox.SizeY = Height;
	return CollisionBox;
}
} // namespace

FGameplayTag FNSE025Battle::MoveTag(int32 Index)
{
	switch (Index)
	{
	case 0: return NSE025Move0;
	case 1: return NSE025Move1;
	case 2: return NSE025Move2;
	case 3: return NSE025Move3;
	case 4: return NSE025Move4;
	case 5: return NSE025Move5;
	case 6: return NSE025Move6;
	case 7: return NSE025Move7;
	default: return FGameplayTag();
	}
}

void UNSE025Stand::Exec_Implementation()
{
	Parent->Player->EnableState(Enable, StateMachine_Primary);
	Parent->SetCelName(Cel);
}

void UNSE025Attack::Exec_Implementation()
{
	APlayerObject* Player = Parent->Player;
	if (!Entered)
	{
		Entered = 1;
		Parent->SetAttacking(true, true);
		Parent->HitCommon = FHitDataCommon();
		FHitData Hit;
		Hit.Damage = Spec.Damage;
		Hit.Hitstun = Spec.Hitstun;
		Hit.RecoverableDamagePercent = 0;
		Hit.MinimumDamagePercent = Spec.MinimumDamagePercent;
		Hit.InitialProration = Spec.InitialProration;
		Hit.ForcedProration = Spec.ForcedProration;
		Hit.GroundPushbackX = Spec.Pushback;
		Hit.AirPushbackX = 0;
		Hit.AirPushbackY = Spec.AirPushbackY;
		Hit.Gravity = Spec.Gravity;
		Hit.Untech = Spec.Untech > 0 ? Spec.Untech : Spec.Hitstun;
		Hit.Hitstop = Spec.Hitstop;
		Hit.EnemyHitstopModifier = 0;
		Hit.GroundHitAction = Spec.AirHitAction;
		Hit.AirHitAction = Spec.AirHitAction;
		Parent->NormalHit = Hit;
		Parent->CounterHit = Hit;
		Parent->AttackFlags |= ATK_HitOTG;
		Parent->SetCelName(IdleCel);
	}
	if (Parent->ActionTime == Spec.CancelFrom)
	{
		for (int32 Option : Spec.ChainCancels)
		{
			Player->AddChainCancelOption(FNSE025Battle::MoveTag(Option));
		}
	}
	if (Parent->ActionTime == Spec.Startup)
	{
		Parent->EnableHit(true);
		Parent->SetCelName(ActiveCel);
	}
	if (Parent->ActionTime == Spec.Startup + Spec.Active)
	{
		Parent->EnableHit(false);
		Parent->SetCelName(IdleCel);
	}
	if (Spec.MultiHit > 0 && Parent->ActionTime == Spec.MultiHit)
	{
		Parent->EnableHit(true);
	}
	if (Parent->ActionTime >= Spec.Startup + Spec.Active + Spec.Recovery)
	{
		Player->JumpToStatePrimary(State_Universal_Stand);
	}
}

void UNSE025Reaction::Exec_Implementation()
{
	if (!Entered)
	{
		Entered = 1;
		if (bGuard)
		{
			Parent->Player->SetGuardValues();
		}
		else
		{
			Parent->Player->SetHitValues();
		}
	}
	Parent->SetCelName(Cel);
}

void UNSE025Tech::Exec_Implementation()
{
	if (Parent->Player->PosY <= Parent->Player->GroundHeight)
	{
		Parent->Player->JumpToStatePrimary(State_Universal_Stand);
	}
	Parent->SetCelName(Cel);
}

FNSE025Battle::FNSE025Battle()
{
	CreateWorldAndServices();
	Attacker = CreatePlayer(0, 2);
	Defender = CreatePlayer(1, 3);
	ConfigureBattle();

	AddCel(Attacker, NSE025CelIdle, {});
	AddStand(Attacker, State_Universal_Stand, ENB_Standing | ENB_NormalAttack | ENB_SpecialAttack, NSE025CelIdle);

	AddCel(Defender, NSE025CelHurt, {Box(BOX_Hurt, 0, BoxHeight / 2, HurtWidth, BoxHeight)});
	AddStand(Defender, State_Universal_Stand, ENB_Standing | ENB_Block, NSE025CelHurt);
	AddReaction(Defender, State_Universal_Hitstun_0, EStateType::Hitstun, false, NSE025CelHurt);
	AddReaction(Defender, State_Universal_StandBlock, EStateType::Blockstun, true, NSE025CelHurt);
	AddStand(Defender, State_Universal_StandBlockEnd, ENB_Standing | ENB_Block, NSE025CelHurt);
	AddReaction(Defender, State_Universal_Launch_B, EStateType::Hitstun, false, NSE025CelHurt);
	AddReaction(Defender, State_Universal_Launch_V, EStateType::Hitstun, false, NSE025CelHurt);
	AddReaction(Defender, State_Universal_Launch_F, EStateType::Hitstun, false, NSE025CelHurt);
	AddReaction(Defender, State_Universal_Blowback, EStateType::Hitstun, false, NSE025CelHurt);
	AddReaction(Defender, State_Universal_Tailspin, EStateType::Hitstun, false, NSE025CelHurt);
	// The engine's landing code jumps here when a teched defender lands; a stand keeps it idle.
	AddStand(Defender, State_Universal_JumpLanding, ENB_Standing | ENB_Block, NSE025CelHurt);
	AddTech(Defender, NSE025Tech, NSE025CelHurt);
}

void FNSE025Battle::CreateWorldAndServices()
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
	for (int32 ObjectIndex = 0; ObjectIndex < Game->MaxBattleObjects; ++ObjectIndex)
	{
		auto* Object = World->SpawnActor<ABattleObject>();
		Object->GameState = Game;
		Object->ObjNumber = ObjectIndex;
		Game->Objects.Add(Object);
	}
}

APlayerObject* FNSE025Battle::CreatePlayer(int32 PlayerIndex, int32 ObjNumber)
{
	auto* Player = World->SpawnActor<APlayerObject>();
	Player->GameState = Game;
	Player->Player = Player;
	Player->ObjNumber = ObjNumber;
	Player->PlayerIndex = PlayerIndex;
	Player->TeamIndex = 0;
	Player->CurrentHealth = Player->MaxHealth = FixtureHealth;
	Player->PlayerFlags = PLF_IsOnScreen | PLF_DefaultLandingAction;
	Player->MiscFlags = MISC_PushCollisionActive | MISC_WallCollisionActive | MISC_FloorCollisionActive |
		MISC_InertiaEnable;
	Player->PosY = 0;
	Game->Players.Add(Player);
	Game->SortedObjects.Add(Player);
	return Player;
}

void FNSE025Battle::ConfigureBattle()
{
	for (auto* Object : Game->Objects)
	{
		Game->SortedObjects.Add(Object);
	}
	Game->BattleState.TeamData[0].TeamCount = 1;
	Game->BattleState.TeamData[1].TeamCount = 1;
	Game->BattleState.MainPlayer[0] = Attacker;
	Game->BattleState.MainPlayer[1] = Defender;
	Attacker->Enemy = Defender;
	Defender->Enemy = Attacker;
	Game->BattleState.BattlePhase = EBattlePhase::Battle;
	// Walls are the stage bounds; the screen-edge walls would creep inward because the camera has no targets here.
	Game->BattleState.ScreenData.Flags = SCR_DisableScreenSides;
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

void FNSE025Battle::AddCel(APlayerObject* Player, FGameplayTag Cel, const TArray<FCollisionBox>& Boxes)
{
	if (!Player->CollisionData)
	{
		Player->CollisionData = NewObject<UCollisionData>(Player);
	}
	FCollisionStruct Frame;
	Frame.CelName = Cel;
	Frame.Boxes = Boxes;
	Player->CollisionData->CollisionFrames.Add(Frame);
}

void FNSE025Battle::AddStand(APlayerObject* Player, FGameplayTag Name, int32 Enable, FGameplayTag Cel)
{
	auto* State = NewObject<UNSE025Stand>(Player);
	State->Name = Name;
	State->StateType = EStateType::Standing;
	State->Enable = Enable;
	State->Cel = Cel;
	Player->AddState(Name, State, StateMachine_Primary);
}

void FNSE025Battle::AddReaction(APlayerObject* Player, FGameplayTag Name, EStateType Type, bool bGuard,
								FGameplayTag Cel)
{
	auto* State = NewObject<UNSE025Reaction>(Player);
	State->Name = Name;
	State->StateType = Type;
	State->bGuard = bGuard;
	State->Cel = Cel;
	Player->AddState(Name, State, StateMachine_Primary);
}

void FNSE025Battle::AddTech(APlayerObject* Player, FGameplayTag Name, FGameplayTag Cel)
{
	auto* State = NewObject<UNSE025Tech>(Player);
	State->Name = Name;
	State->StateType = EStateType::Tech;
	State->Cel = Cel;
	FInputCondition Condition;
	Condition.Sequence.Add(FInputBitmask(static_cast<EInputFlags>(TechInput())));
	Condition.Method = EInputMethod::Normal;
	FInputConditionList List;
	List.InputConditions.Add(Condition);
	State->InputConditionLists = {List};
	Player->AddState(Name, State, StateMachine_Primary);
}

void FNSE025Battle::AddMove(const FNSE025MoveSpec& Spec)
{
	check(Moves.Num() < MaxMoves);
	const int32 Index = Moves.Num();
	Moves.Add(Spec);
	AddCel(Attacker, ActiveCel(Index), {});
	auto* State = NewObject<UNSE025Attack>(Attacker);
	State->Name = MoveTag(Index);
	State->IdleCel = NSE025CelIdle;
	State->ActiveCel = ActiveCel(Index);
	State->EntryStance = EEntryStance::Standing;
	Attacker->AddState(State->Name, State, StateMachine_Primary);
	AttackStates.Add(State);
	SetMove(Index, Spec);
}

void FNSE025Battle::SetMove(int32 Index, const FNSE025MoveSpec& Spec)
{
	check(AttackStates.IsValidIndex(Index));
	Moves[Index] = Spec;
	for (auto& Frame : Attacker->CollisionData->CollisionFrames)
	{
		if (Frame.CelName == ActiveCel(Index))
		{
			Frame.Boxes = {Box(BOX_Hit, Spec.Reach / 2, BoxHeight / 2, Spec.Reach, BoxHeight)};
		}
	}
	auto* State = AttackStates[Index];
	State->Spec = Spec;
	State->StateType = Spec.Type;
	State->MaxChain = Spec.MaxChain;
	State->bEnableReverseBeat = Spec.bReverseBeat;
	State->IsFollowupState = Spec.bFollowup;
	FInputCondition Condition;
	Condition.Sequence.Add(FInputBitmask(static_cast<EInputFlags>(Spec.Input)));
	Condition.Method = EInputMethod::Once;
	FInputConditionList List;
	List.InputConditions.Add(Condition);
	State->InputConditionLists = {List};
}

void FNSE025Battle::Start(int32 AttackerX, int32 DefenderX)
{
	// CanReverseBeat is rollback state, so Reset would revert it; keep the caller's value.
	const bool bCanReverseBeat = Attacker->CanReverseBeat;
	if (bStarted)
	{
		Reset();
	}
	bStarted = true;
	Attacker->CanReverseBeat = bCanReverseBeat;
	for (auto* Player : {Attacker, Defender})
	{
		Player->PosX = Player == Attacker ? AttackerX : DefenderX;
		Player->PosY = 0;
		Player->PrevPosX = Player->PosX;
		Player->PrevPosY = 0;
		Player->Direction = Player == Attacker ? (AttackerX <= DefenderX ? DIR_Right : DIR_Left)
											  : (AttackerX <= DefenderX ? DIR_Left : DIR_Right);
		// Seed the buffer with neutral like a new player: the engine stores no-direction frames as 0, and a Once
		// input needs a recorded entry before the press, so a bare FInputBuffer would swallow a frame 1 press.
		Player->StoredInputBuffer = FInputBuffer();
		for (int32& Input : Player->StoredInputBuffer.InputBufferInternal)
		{
			Input = INP_Neutral;
		}
		Player->JumpToStatePrimary(State_Universal_Stand);
	}
	Step();
	StartState = FRollbackData();
	int32 Checksum = 0;
	Game->SaveGameState(StartState, &Checksum);
	StartFrame = Game->BattleState.FrameNumber;
}

void FNSE025Battle::Step(int32 Input1, int32 Input2)
{
	Game->UpdateGameState(Input1, Input2, true);
}

void FNSE025Battle::Reset()
{
	Game->LoadGameState(StartState);
}

int32 FNSE025Battle::Frame() const
{
	return Game->BattleState.FrameNumber - StartFrame;
}

int32 FNSE025Battle::GuardInput() const
{
	return Defender->Direction == DIR_Left ? INP_Right : INP_Left;
}

int32 FNSE025Battle::TechInput()
{
	return INP_A;
}

FGameplayTag FNSE025Battle::TechTag()
{
	return NSE025Tech;
}

bool FNSE025Battle::AttackerBegan(int32 MoveIndex) const
{
	return Attacker->GetCurrentStateName(StateMachine_Primary) == MoveTag(MoveIndex) && Attacker->ActionTime == 1;
}

bool FNSE025Battle::DefenderInHitstun() const
{
	return Defender->GetStateType() == EStateType::Hitstun;
}

FNSE025Battle::~FNSE025Battle()
{
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
}

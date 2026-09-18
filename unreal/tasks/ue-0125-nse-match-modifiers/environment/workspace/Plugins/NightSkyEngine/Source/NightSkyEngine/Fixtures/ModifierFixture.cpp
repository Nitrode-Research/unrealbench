#include "ModifierFixture.h"
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyBattleWidget.h"
#include "NightSkyEngine/Data/CollisionData.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AModifierFixtureFighter::AModifierFixtureFighter()
{
	MaxHealth = 1000;
	MaxMeter = 100;
	MeterPercentOnHit = 0;
	MeterPercentOnReceiveHit = 0;
	auto* Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FixtureBody"));
	Body->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(
	    TEXT("/Engine/BasicShapes/Cube.Cube"));
	Body->SetStaticMesh(Mesh.Object);
	Body->SetRelativeScale3D(FVector(.5, .5, 1.5));
	Body->SetRelativeLocation(FVector(0, 0, 75));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AModifierFixtureFighter::BeginPlay()
{
	Super::BeginPlay();
	CollisionData = NewObject<UCollisionData>(this);
	FCollisionStruct Idle;
	Idle.CelName = State_Universal_Stand;
	FCollisionBox Hurt;
	Hurt.Type = BOX_Hurt;
	Hurt.SizeX = 90000;
	Hurt.SizeY = 200000;
	Hurt.PosY = 100000;
	Idle.Boxes.Add(Hurt);
	CollisionData->CollisionFrames.Add(Idle);
	FCollisionStruct Attack = Idle;
	Attack.CelName = State_Universal_Throw;
	FCollisionBox Hit;
	Hit.Type = BOX_Hit;
	Hit.SizeX = 350000;
	Hit.SizeY = 200000;
	Hit.PosX = 180000;
	Hit.PosY = 100000;
	Attack.Boxes.Add(Hit);
	CollisionData->CollisionFrames.Add(Attack);
	// Opposing fixture projectiles occupy separate narrow vertical lanes. They
	// contact the tall fighter hurtbox without canceling each other by clash.
	for (FGameplayTag Tag : {FGameplayTag(State_ModifierFixture_ProjectileA),
	                         FGameplayTag(State_ModifierFixture_ProjectileB)})
	{
		FCollisionStruct ProjectileFrame;
		ProjectileFrame.CelName = Tag;
		FCollisionBox ProjectileHit = Hit;
		ProjectileHit.SizeY = 6000;
		ProjectileHit.PosY = 0;
		ProjectileFrame.Boxes.Add(ProjectileHit);
		CollisionData->CollisionFrames.Add(ProjectileFrame);
	}
	auto* Stand = NewObject<UModifierFixtureState>(this);
	Stand->Name = State_Universal_Stand;
	Stand->StateType = EStateType::Standing;
	Stand->bHumanUsable = false;
	AddState(Stand->Name, Stand, StateMachine_Primary);
	for (FGameplayTag Tag : {FGameplayTag(State_ModifierFixture_ProjectileA),
	                         FGameplayTag(State_ModifierFixture_ProjectileB)})
	{
		auto* Projectile = NewObject<UModifierProjectileState>(this);
		Projectile->Name = Tag;
		AddObjectState(Tag, Projectile, false);
	}
	// Hit reactions use the engine's collision and health path with a fixed recovery state.
	for (auto Tag :
	     {FGameplayTag(State_Universal_StandBlockEnd), FGameplayTag(State_Universal_Crouch),
	      FGameplayTag(State_Universal_MatchWin), FGameplayTag(State_Universal_RoundWin),
	      FGameplayTag(State_Universal_RoundLose), FGameplayTag(State_Universal_JumpLanding)})
	{
		auto* Other = NewObject<UModifierFixtureState>(this);
		Other->Name = Tag;
		Other->bHumanUsable = false;
		if (Tag == State_Universal_Crouch)
			Other->StateType = EStateType::Hitstun;
		AddState(Tag, Other, StateMachine_Primary);
	}
}

void UModifierFixtureState::Init_Implementation()
{
	auto* Fighter = Cast<APlayerObject>(Parent);
	if (Fighter && Name == State_Universal_Crouch && Fighter->AttackOwner)
		Fighter->SetHitValues();
}

void UModifierFixtureState::Exec_Implementation()
{
	auto* Fighter = Cast<APlayerObject>(Parent);
	if (!Fighter || !Fighter->GameState)
		return;
	if (Name == State_Universal_RoundWin || Name == State_Universal_RoundLose ||
	    Name == State_Universal_MatchWin)
	{
		Fighter->RoundEndFlag = true;
		return;
	}
	if (Name == State_Universal_Crouch)
	{
		Fighter->SetCelName(State_Universal_Stand);
		return;
	}
	Fighter->EnableAll(StateMachine_Primary);
	// Inputs have already been normalized to facing-relative directions by the engine.
	const int32 Forward = (Fighter->Inputs & INP_Right) ? 1 : 0;
	const int32 Back = (Fighter->Inputs & INP_Left) ? 1 : 0;
	if (Fighter->CheckStateEnabled(Forward ? EStateType::ForwardWalk : EStateType::BackwardWalk,
	                               FGameplayTag(), StateMachine_Primary))
		Fighter->SpeedX = (Forward - Back) * 1000;
	if ((Fighter->Inputs & INP_E) && (Forward || Back) &&
	    Fighter->CheckStateEnabled(Forward ? EStateType::ForwardDash : EStateType::BackwardDash,
	                               FGameplayTag(), StateMachine_Primary))
		Fighter->SpeedX = (Forward - Back) * 3000;
	if ((Fighter->Inputs & INP_Up) && Fighter->PosY == Fighter->GroundHeight &&
	    Fighter->CheckStateEnabled(EStateType::NeutralJump, FGameplayTag(), StateMachine_Primary))
	{
		Fighter->SpeedY = 10000;
		Fighter->Gravity = 1000;
	}
	if (Fighter->Inputs & INP_C)
		Fighter->GameState->BattleState.SuperFreezeDuration = 5;
	// Authored gameplay command: its ordinary input is recorded and corrected by the engine.
	if (Fighter->Inputs & INP_D)
		Fighter->GameState->DeactivateModifier(TEXT("Restriction"));
	if (Fighter->ObjectReg1 > 0)
		--Fighter->ObjectReg1;
	if ((Fighter->Inputs & INP_A) && Fighter->ObjectReg1 == 0)
	{
		Fighter->ObjectReg1 = 20;
		Fighter->SetAttacking(true);
		Fighter->EnableHit(true);
		Fighter->NormalHit.Damage = (Fighter->Inputs & INP_B) ? 10 : 21;
		Fighter->NormalHit.Hitstop = 3;
		const bool Knockback = (Fighter->Inputs & INP_F) != 0;
		Fighter->NormalHit.Hitstun = Knockback ? 18 : 0;
		Fighter->NormalHit.GroundPushbackX = Knockback ? 10000 : 0;
		Fighter->AttackFlags |= ATK_IgnorePushbackScaling;
		Fighter->NormalHit.CustomHitAction =
		    Knockback ? State_Universal_Crouch : State_Universal_Stand;
		Fighter->NormalHit.GroundHitAction = HACT_Custom;
		Fighter->NormalHit.AirHitAction = HACT_Custom;
		Fighter->CounterHit = Fighter->NormalHit;
	}
	const bool Active = Fighter->ObjectReg1 >= 14 && Fighter->ObjectReg1 <= 17;
	Fighter->SetCelName(Active ? State_Universal_Throw : State_Universal_Stand);
	if (Fighter->ObjectReg1 == 0)
		Fighter->SetAttacking(false);
}

AModifierFixtureBattle::AModifierFixtureBattle()
{
	MaxBattleObjects = 16;
	BattleObjectClass = AModifierFixtureProjectile::StaticClass();
	BattleState.RoundStartPos = 100000;
}

void AModifierFixtureBattle::BeginPlay()
{
	BattleHudActor = GetWorld()->SpawnActor<ANightSkyBattleHudActor>();
	BattleHudActor->TopWidget = NewObject<UNightSkyBattleWidget>(GetWorld());
	BattleHudActor->BottomWidget = NewObject<UNightSkyBattleWidget>(GetWorld());
	Super::BeginPlay();
	FString PeerRole;
	FParse::Value(FCommandLine::Get(), TEXT("ModifierPeerRole="), PeerRole);
	if (FParse::Param(FCommandLine::Get(), TEXT("ModifierSample")) || PeerRole == TEXT("replay"))
		GameInstance->IsTraining = false;
}

UE_DEFINE_GAMEPLAY_TAG(State_ModifierFixture_ProjectileA, "State.ModifierFixture.ProjectileA");
UE_DEFINE_GAMEPLAY_TAG(State_ModifierFixture_ProjectileB, "State.ModifierFixture.ProjectileB");
AModifierFixtureProjectile::AModifierFixtureProjectile()
{
	auto* Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileBody"));
	Body->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(
	    TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Body->SetStaticMesh(Mesh.Object);
	Body->SetRelativeScale3D(FVector(.2));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void UModifierProjectileState::Exec_Implementation()
{
	if (!Parent)
		return;
	if (Parent->ActionTime == 0)
	{
		if (Parent->Player && Parent->GameState)
		{
			auto* Target = Parent->GameState->GetMainPlayer(Parent->Player->PlayerIndex == 1);
			if (Target)
				Parent->SetFacing(Parent->PosX > Target->PosX ? DIR_Left : DIR_Right);
		}
		Parent->SetAttacking(true);
		Parent->EnableHit(true);
		Parent->SpeedX = 3000; // Movement applies facing through AddPosXWithDir.
		Parent->NormalHit.Damage = 10;
		Parent->NormalHit.MinimumDamagePercent = 100;
		Parent->NormalHit.Hitstop = 0;
		Parent->NormalHit.Hitstun = 0;
		Parent->NormalHit.CustomHitAction = State_Universal_Stand;
		Parent->NormalHit.GroundHitAction = HACT_Custom;
		Parent->NormalHit.AirHitAction = HACT_Custom;
		Parent->CounterHit = Parent->NormalHit;
		Parent->PosY = Parent->Player->GroundHeight +
		               (Parent->Player->PlayerIndex == 0 ? 30000 : 150000) +
		               (Name == State_ModifierFixture_ProjectileA ? 0 : 10000);
	}
	Parent->SetCelName(Parent->ActionTime >= 8 ? Name : FGameplayTag(State_Universal_Stand));
	if (Parent->ActionTime >= 30)
		Parent->DeactivateObject();
}

UModifierFixtureCharaData::UModifierFixtureCharaData()
{
	PlayerClass = AModifierFixtureFighter::StaticClass();
}

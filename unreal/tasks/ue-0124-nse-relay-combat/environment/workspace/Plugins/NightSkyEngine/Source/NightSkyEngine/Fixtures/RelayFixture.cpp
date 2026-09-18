#include "RelayFixture.h"
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyBattleWidget.h"
#include "NightSkyEngine/Data/CollisionData.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "GameplayTagsManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

ARelayFixtureFighter::ARelayFixtureFighter()
{
    auto *Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FixtureBody"));
    Body->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    Body->SetStaticMesh(Mesh.Object);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Engine/EngineDebugMaterials/DebugMeshMaterial.DebugMeshMaterial"));
    Body->SetMaterial(0, Material.Object);
    Body->SetRelativeScale3D(FVector(.5, .5, 1.5));
    Body->SetRelativeLocation(FVector(0, 0, 75));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetCastShadow(false);
}

void ARelayFixtureFighter::BeginPlay()
{
    Super::BeginPlay();
    if (auto Body = FindComponentByClass<UStaticMeshComponent>())
    {
        Body->CreateDynamicMaterialInstance(0);
    }
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
    auto *Stand = NewObject<URelayFixtureState>(this);
    Stand->Name = State_Universal_Stand;
    Stand->StateType = EStateType::Standing;
    Stand->bHumanUsable = false;
    AddState(Stand->Name, Stand, StateMachine_Primary);
    for (const TCHAR *Name : {TEXT("State.Relay.Synchronized"), TEXT("State.Relay.Followup")})
    {
        auto Move = NewObject<URelayFixtureMove>(this);
        Move->Name = FGameplayTag::RequestGameplayTag(FName(Name));
        Move->StateType = EStateType::NormalAttack;
        Move->bHumanUsable = false;
        AddState(Move->Name, Move, StateMachine_Primary);
    }
    for (auto Tag :
         {FGameplayTag(State_Universal_StandBlock), FGameplayTag(State_Universal_CrouchBlock),
          FGameplayTag(State_Universal_AirBlock)})
    {
        auto Block = NewObject<URelayFixtureReaction>(this);
        Block->Name = Tag;
        Block->StateType = EStateType::Blockstun;
        Block->bHumanUsable = false;
        AddState(Tag, Block, StateMachine_Primary);
    }
    auto Throw = NewObject<URelayFixtureThrow>(this);
    Throw->Name = FGameplayTag::RequestGameplayTag(FName("State.Relay.Throw"));
    Throw->bHumanUsable = false;
    Throw->StateType = EStateType::NormalAttack;
    AddState(Throw->Name, Throw, StateMachine_Primary);
    auto Lock = NewObject<URelayFixtureReaction>(this);
    Lock->Name = State_Universal_ThrowLock;
    Lock->bHumanUsable = false;
    AddState(Lock->Name, Lock, StateMachine_Primary);
    auto Projectile = NewObject<URelayFixtureProjectile>(this);
    Projectile->Name = FGameplayTag::RequestGameplayTag(FName("State.Relay.Projectile"));
    ObjectStates.Add(Projectile);
    ObjectStateNames.Add(Projectile->Name);
    for (auto Tag :
         {FGameplayTag(State_Universal_Hitstun_0), FGameplayTag(State_Universal_Hitstun_1),
          FGameplayTag(State_Universal_Hitstun_2), FGameplayTag(State_Universal_Hitstun_3),
          FGameplayTag(State_Universal_Hitstun_4), FGameplayTag(State_Universal_Hitstun_5),
          FGameplayTag(State_Universal_Crumple), FGameplayTag(State_Universal_Launch_B),
          FGameplayTag(State_Universal_Launch_F), FGameplayTag(State_Universal_Launch_V)})
    {
        auto Reaction = NewObject<URelayFixtureReaction>(this);
        Reaction->Name = Tag;
        Reaction->StateType = EStateType::Hitstun;
        Reaction->bHumanUsable = false;
        AddState(Tag, Reaction, StateMachine_Primary);
    }
    // Hit reactions use the engine's collision and health path with a fixed recovery state.
    for (auto Tag : {FGameplayTag(State_Universal_StandBlockEnd),
                     FGameplayTag(State_Universal_Crouch), FGameplayTag(State_Universal_MatchWin)})
    {
        auto *Other = NewObject<URelayFixtureState>(this);
        Other->Name = Tag;
        Other->bHumanUsable = false;
        AddState(Tag, Other, StateMachine_Primary);
    }
}

namespace
{
    void FixtureBurst(APlayerObject *Fighter)
    {
        if (Fighter->ObjectReg2 > 0)
        {
            --Fighter->ObjectReg2;
        }
        if ((Fighter->Inputs & INP_C) && Fighter->ObjectReg2 == 0)
        {
            Fighter->ObjectReg2 = 60;
            Fighter->StartSuperFreeze(6, 6);
        }
    }
} // namespace
void URelayFixtureState::Exec_Implementation()
{
    auto *Fighter = Cast<APlayerObject>(Parent);
    if (!Fighter || !Fighter->GameState)
    {
        return;
    }
    FixtureBurst(Fighter);
    Fighter->EnableState(ENB_Block | ENB_NormalAttack, StateMachine_Primary);
    // Inputs have already been normalized to facing-relative directions by the engine.
    const int32 Forward = (Fighter->Inputs & INP_Right) ? 1 : 0;
    const int32 Back = (Fighter->Inputs & INP_Left) ? 1 : 0;
    Fighter->PosX += (Forward - Back) * (Fighter->Direction == DIR_Right ? 1000 : -1000);
    if (Fighter->ObjectReg1 > 0)
    {
        --Fighter->ObjectReg1;
    }
    if ((Fighter->Inputs & INP_A) && Fighter->ObjectReg1 == 0)
    {
        Fighter->ObjectReg1 = 20;
        Fighter->SetAttacking(true);
        Fighter->EnableHit(true);
        Fighter->NormalHit.Damage = 500;
        Fighter->NormalHit.InitialProration = 100;
        Fighter->NormalHit.ForcedProration = 90;
        Fighter->NormalHit.RecoverableDamagePercent = 25;
        Fighter->NormalHit.Hitstop = 3;
        Fighter->NormalHit.Hitstun = 8;
        Fighter->CounterHit = Fighter->NormalHit;
    }
    if ((Fighter->Inputs & INP_B) && Fighter->ObjectReg1 == 0)
    {
        Fighter->CanProximityThrow = false;
        Fighter->SetAttacking(true);
        Fighter->SetThrowActive(true);
        Fighter->SetThrowRange(CastChecked<ARelayFixtureFighter>(Fighter)->SampleThrowRange);
        Fighter->SetThrowExeState(FGameplayTag::RequestGameplayTag(FName("State.Relay.Throw")));
    }
    const bool Active = Fighter->ObjectReg1 >= 14 && Fighter->ObjectReg1 <= 17;
    Fighter->SetCelName(Active ? State_Universal_Throw : State_Universal_Stand);
    if (Fighter->ObjectReg1 == 0 && !(Fighter->Inputs & INP_B))
    {
        Fighter->SetAttacking(false);
    }
}

ARelayFixtureBattle::ARelayFixtureBattle()
{
    MaxBattleObjects = 16;
    BattleState.RoundStartPos = 150000;
}

void ARelayFixtureBattle::BeginPlay()
{
    BattleHudActor = GetWorld()->SpawnActor<ANightSkyBattleHudActor>();
    BattleHudActor->TopWidget = NewObject<UNightSkyBattleWidget>(GetWorld());
    BattleHudActor->BottomWidget = NewObject<UNightSkyBattleWidget>(GetWorld());
    ConfigureRelayMoves(FGameplayTag::RequestGameplayTag(FName("State.Relay.Synchronized")),
                        FGameplayTag::RequestGameplayTag(FName("State.Relay.Followup")));
    Super::BeginPlay();
}

#include "NativeGameplayTags.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(RelaySyncTag, "State.Relay.Synchronized");
UE_DEFINE_GAMEPLAY_TAG_STATIC(RelayFollowTag, "State.Relay.Followup");
void URelayFixtureMove::Init_Implementation()
{
    auto Fighter = Cast<APlayerObject>(Parent);
    if (!Fighter)
    {
        return;
    }
    {
        Fighter->SetAttacking(true);
        Fighter->EnableHit(true);
        Fighter->NormalHit.Damage = Name == RelaySyncTag ? 400 : 700;
        Fighter->NormalHit.Hitstop = 3;
        Fighter->NormalHit.EnemyHitstopModifier = 2;
        Fighter->NormalHit.Hitstun = CastChecked<ARelayFixtureFighter>(Fighter)->SampleHitstun;
        Fighter->NormalHit.InitialProration = 100;
        Fighter->NormalHit.ForcedProration = 80;
        Fighter->NormalHit.RecoverableDamagePercent = 25;
        Fighter->CounterHit = Fighter->NormalHit;
        if (Name == RelaySyncTag && Fighter->TeamIndex != 0)
        {
            auto Projectile = Fighter->AddBattleObject(
                FGameplayTag::RequestGameplayTag(FName("State.Relay.Projectile")));
            if (Projectile)
            {
                Projectile->PosY +=
                    CastChecked<ARelayFixtureFighter>(Fighter)->SampleProjectileHeight;
            }
        }
    }
}
void URelayFixtureMove::Exec_Implementation()
{
    auto Fighter = Cast<APlayerObject>(Parent);
    if (!Fighter)
    {
        return;
    }
    Fighter->SetCelName(Fighter->ActionTime >= 2 && Fighter->ActionTime <= 4
                            ? State_Universal_Throw
                            : State_Universal_Stand);
    if (Fighter->ActionTime >= 10)
    {
        Fighter->SetAttacking(false);
        Fighter->JumpToStatePrimary(State_Universal_Stand);
    }
}
UE_DEFINE_GAMEPLAY_TAG_STATIC(RelayProjectileTag, "State.Relay.Projectile");
void URelayFixtureProjectile::Exec_Implementation()
{
    if (Parent->ActionTime == 0)
    {
        Parent->SetAttacking(true);
        Parent->EnableHit(true);
        Parent->NormalHit.Damage = 300;
        Parent->NormalHit.Hitstop = 3;
        Parent->NormalHit.Hitstun =
            CastChecked<ARelayFixtureFighter>(Parent->Player)->SampleHitstun;
        Parent->NormalHit.ForcedProration = 80;
        Parent->NormalHit.InitialProration = 100;
        Parent->NormalHit.RecoverableDamagePercent = 25;
        Parent->CounterHit = Parent->NormalHit;
        Parent->SpeedX = 6000;
    }
    Parent->SetCelName(State_Universal_Throw);
    if (Parent->ActionTime >= 90)
    {
        Parent->ResetObject();
    }
}
void URelayFixtureReaction::Init_Implementation()
{
    auto Fighter = Cast<APlayerObject>(Parent);
    if (Fighter && StateType == EStateType::Hitstun && Fighter->AttackOwner)
    {
        Fighter->SetHitValues();
    }
}
void URelayFixtureReaction::Exec_Implementation()
{
    auto Fighter = Cast<APlayerObject>(Parent);
    if (!Fighter)
    {
        return;
    }
    FixtureBurst(Fighter);
    Fighter->SetAttacking(false);
    Fighter->SetCelName(State_Universal_Stand);
    if (Fighter->ActionTime >= CastChecked<ARelayFixtureFighter>(Fighter)->SampleHitstun &&
        Fighter->CurrentHealth > 0)
    {
        Fighter->PlayerFlags &= ~PLF_IsStunned;
        Fighter->JumpToStatePrimary(State_Universal_Stand);
    }
}
UE_DEFINE_GAMEPLAY_TAG_STATIC(RelayThrowTag, "State.Relay.Throw");
void URelayFixtureThrow::Exec_Implementation()
{
    auto Fighter = Cast<APlayerObject>(Parent);
    if (!Fighter)
    {
        return;
    }
    Fighter->SetAttacking(false);
    Fighter->SetCelName(State_Universal_Stand);
    if (Fighter->ActionTime >= 12)
    {
        Fighter->ThrowEnd();
        Fighter->JumpToStatePrimary(State_Universal_Stand);
    }
}

// The sample draws immutable slots in a small formation even when synchronized
// collision origins coincide. This changes only mesh presentation.
void ARelayFixtureFighter::UpdateVisuals()
{
    Super::UpdateVisuals();
    if (auto Body = FindComponentByClass<UStaticMeshComponent>())
    {
        // Separate team presentation heights keep both bodies legible during contact.
        const FVector Offset((PlayerIndex == 0 ? 1.0 : -1.0) * TeamIndex * 65.0, 0,
                             75 + PlayerIndex * 90);
        const FVector WorldOffset =
            GameState ? GameState->BattleSceneTransform.GetRotation().RotateVector(Offset) : Offset;
        Body->SetWorldLocation(GetActorLocation() + WorldOffset);
        if (auto Material = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(0)))
        {
            const FLinearColor Colors[6] = {FLinearColor(.1, .8, 1), FLinearColor(.4, 1, .1),
                                            FLinearColor(1, .8, .1), FLinearColor(1, .1, .8),
                                            FLinearColor(1, .4, .1), FLinearColor(.2, .3, 1)};
            Material->SetVectorParameterValue(
                TEXT("Color"), Colors[FMath::Clamp(PlayerIndex * 3 + TeamIndex, 0, 5)]);
        }
    }
}

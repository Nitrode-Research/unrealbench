#include "ReplayFixture.h"
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyBattleWidget.h"
#include "NightSkyEngine/Data/CollisionData.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Sound/SoundWaveProcedural.h"
#include "Engine/DirectionalLight.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"

AReplayFixtureFighter::AReplayFixtureFighter()
{
    auto* Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FixtureBody"));
    Body->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Body->SetStaticMesh(Mesh.Object);
    Body->SetRelativeScale3D(FVector(.5, .5, 1.5));
    Body->SetRelativeLocation(FVector(0, 0, 75));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AReplayFixtureFighter::BeginPlay()
{
    Super::BeginPlay();
    Tone = NewObject<USoundWaveProcedural>(this);
    Tone->SetSampleRate(22050);
    Tone->NumChannels = 1;
    Tone->Duration = INDEFINITELY_LOOPING_DURATION;
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
    auto* Stand = NewObject<UReplayFixtureState>(this);
    Stand->Name = State_Universal_Stand;
    Stand->StateType = EStateType::Standing;
    Stand->bHumanUsable = false;
    AddState(Stand->Name, Stand, StateMachine_Primary);
    for (int32 I = 0; I < 8; ++I)
    {
        auto* Projectile = NewObject<UReplayFixtureProjectile>(this);
        Projectile->Name = State_Universal_Throw;
        AddObjectState(Projectile->Name, Projectile, false);
    }
    // Hit reactions use the engine's collision and health path with a fixed recovery state.
    for (auto Tag : {FGameplayTag(State_Universal_StandBlockEnd), FGameplayTag(State_Universal_Crouch),
                     FGameplayTag(State_Universal_MatchWin), FGameplayTag(State_Universal_RoundWin)})
    {
        auto* Other = NewObject<UReplayFixtureState>(this);
        Other->Name = Tag;
        Other->bHumanUsable = false;
        if (Tag == State_Universal_Crouch)
        {
            Other->StateType = EStateType::Hitstun;
        }
        AddState(Tag, Other, StateMachine_Primary);
    }
}

void UReplayFixtureState::Exec_Implementation()
{
    auto* Fighter = Cast<APlayerObject>(Parent);
    if (!Fighter || !Fighter->GameState)
    {
        return;
    }
    if (StateType == EStateType::Hitstun)
    {
        Fighter->SetCelName(State_Universal_Stand);
        return;
    }
    // Inputs have already been normalized to facing-relative directions by the engine.
    const int32 Forward = (Fighter->Inputs & INP_Right) ? 1 : 0;
    const int32 Back = (Fighter->Inputs & INP_Left) ? 1 : 0;
    Fighter->PosX += (Forward - Back) * (Fighter->Direction == DIR_Right ? 1000 : -1000);
    FInputCondition AttackInput;
    AttackInput.Sequence.Add(FInputBitmask(INP_A));
    AttackInput.Sequence[0].Lenience = 3;
    FInputCondition ProjectileInput;
    ProjectileInput.Sequence.Add(FInputBitmask(INP_B));
    ProjectileInput.Sequence[0].Lenience = 3;
    if (Fighter->ObjectReg2 > 0)
    {
        --Fighter->ObjectReg2;
    }
    if (Fighter->CheckInput(ProjectileInput) && Fighter->ObjectReg2 == 0)
    {
        Fighter->ObjectReg2 = 50;
        CastChecked<AReplayFixtureFighter>(Fighter)->EmitAttackSound();
        if (auto* Projectile = Fighter->AddBattleObject(State_Universal_Throw))
        {
            Projectile->NormalHit.Damage =
                Fighter->GameState->BattleState.RandomManager.RandRange(0, 1) ? 400 : 200;
            Projectile->NormalHit.Hitstop = 3;
            Projectile->NormalHit.Hitstun = 8;
            Projectile->NormalHit.GroundHitAction = HACT_Custom;
            Projectile->NormalHit.AirHitAction = HACT_Custom;
            Projectile->NormalHit.CustomHitAction = State_Universal_Crouch;
            Projectile->CounterHit = Projectile->NormalHit;
        }
    }
    if (Fighter->ObjectReg1 > 0)
    {
        --Fighter->ObjectReg1;
    }
    if (Fighter->CheckInput(AttackInput) && Fighter->ObjectReg1 == 0)
    {
        Fighter->ObjectReg1 = 20;
        CastChecked<AReplayFixtureFighter>(Fighter)->EmitAttackSound();
        Fighter->SetAttacking(true);
        Fighter->EnableHit(true);
        Fighter->NormalHit.Damage = 500;
        Fighter->NormalHit.Hitstop = 3;
        Fighter->NormalHit.Hitstun = 8;
        Fighter->NormalHit.GroundHitAction = HACT_Custom;
        Fighter->NormalHit.AirHitAction = HACT_Custom;
        Fighter->NormalHit.CustomHitAction = State_Universal_Crouch;
        Fighter->CounterHit = Fighter->NormalHit;
    }
    const bool Active = Fighter->ObjectReg1 >= 14 && Fighter->ObjectReg1 <= 17;
    Fighter->SetCelName(Active ? State_Universal_Throw : State_Universal_Stand);
    if (Fighter->ObjectReg1 == 0)
    {
        Fighter->SetAttacking(false);
    }
}

AReplayFixtureBattle::AReplayFixtureBattle()
{
    MaxBattleObjects = 16;
    BattleObjectClass = AReplayFixtureProjectileActor::StaticClass();
    BattleState.RoundStartPos = 150000;
}

void AReplayFixtureBattle::BeginPlay()
{
    GetWorld()->SetGameState(this);
    auto* Light = GetWorld()->SpawnActor<ADirectionalLight>();
    Light->GetRootComponent()->SetMobility(EComponentMobility::Movable);
    Light->SetActorRotation(FRotator(-45, -45, 0));
    BattleHudActor = GetWorld()->SpawnActor<ANightSkyBattleHudActor>();
    BattleHudActor->TopWidget = NewObject<UNightSkyBattleWidget>(GetWorld());
    BattleHudActor->BottomWidget = NewObject<UNightSkyBattleWidget>(GetWorld());
    Super::BeginPlay();
}

void UReplayFixtureProjectile::Exec_Implementation()
{
    if (!Parent)
    {
        return;
    }
    Parent->SetCelName(State_Universal_Throw);
    Parent->SetProjectileAttribute(true);
    Parent->SpeedX = 4000;
    Parent->SpeedY = 0;
    Parent->Gravity = 0;
    if (Parent->ActionTime == 0)
    {
        Parent->SetAttacking(true);
        Parent->EnableHit(true);
    }
    if (Parent->ActionTime >= 40)
    {
        Parent->DeactivateObject();
    }
}

void AReplayFixtureFighter::EmitAttackSound()
{
    Tone = NewObject<USoundWaveProcedural>(this);
    Tone->SetSampleRate(22050);
    Tone->NumChannels = 1;
    Tone->Duration = INDEFINITELY_LOOPING_DURATION;
    TArray<int16> Samples;
    Samples.SetNumUninitialized(2205);
    for (int32 I = 0; I < Samples.Num(); ++I)
    {
        Samples[I] = I % 50 < 25 ? 4000 : -4000;
    }
    Tone->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
    GameState->PlayCommonAudio(Tone, .1f);
}

void AReplayFixtureBattle::ProcessEvent(UFunction* Function, void* Parameters)
{
    Super::ProcessEvent(Function, Parameters);
    if (Function->GetFName() == FName(TEXT("UpdateHUD_BP")) && BattleState.MainPlayer[0] &&
        BattleState.MainPlayer[1])
    {
        PresentedHealth.Add(
            FIntPoint(BattleState.MainPlayer[0]->CurrentHealth, BattleState.MainPlayer[1]->CurrentHealth));
    }
}

void AReplayFixtureBattle::MatchInit()
{
    // Fixture stages start immediately, with no authored intro sequence.
    const bool Training = GameInstance->IsTraining;
    GameInstance->IsTraining = true;
    Super::MatchInit();
    GameInstance->IsTraining = Training;
}

AReplayFixtureProjectileActor::AReplayFixtureProjectileActor()
{
    auto* Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileBody"));
    Body->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Body->SetStaticMesh(Mesh.Object);
    Body->SetRelativeScale3D(FVector(.25, .25, .25));
    Body->SetRelativeLocation(FVector(0, 0, 100));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

UReplayFixtureCharaData::UReplayFixtureCharaData()
{
    PlayerClass = AReplayFixtureFighter::StaticClass();
    CharaName = TEXT("ReplayFixtureFighter");
}

UReplayFixtureStageData::UReplayFixtureStageData()
{
    StageURL = TEXT("/Engine/Maps/Entry?game=/Script/NightSkyEngine.ReplayFixtureGameMode");
    StageName = TEXT("ReplayFixtureStage");
}

AReplayFixtureGameMode::AReplayFixtureGameMode()
{
    GameStateClass = AReplayFixtureBattle::StaticClass();
    PlayerControllerClass = ANightSkyPlayerController::StaticClass();
}

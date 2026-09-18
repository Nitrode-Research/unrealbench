#include "ReplayFixture.h"
#include "NightSkyEngine/Network/SpectatorRoom.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "NightSkyEngine/Data/PrimaryStageData.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "Engine/World.h"
#include "Components/AudioComponent.h"
#include "NightSkyEngine/Battle/Actors/AudioManager.h"

bool ReplayFixtureConfigurationMatches(const FBattleData &Actual, const FBattleData &Expected)
{
    // Asset identity is its path. Neither residency nor a particular allocation is required.
    return IsValid(Actual.Stage) && IsValid(Expected.Stage) &&
           FSoftObjectPath(Actual.Stage) == FSoftObjectPath(Expected.Stage) &&
           Actual.PlayerListP1 == Expected.PlayerListP1 && Actual.PlayerListP2 == Expected.PlayerListP2 &&
           Actual.Random.GetSeed() == Expected.Random.GetSeed() &&
           Actual.ColorIndicesP1 == Expected.ColorIndicesP1 && Actual.ColorIndicesP2 == Expected.ColorIndicesP2 &&
           Actual.BattleFormat == Expected.BattleFormat && Actual.StartRoundTimer == Expected.StartRoundTimer &&
           Actual.RoundCount == Expected.RoundCount && Actual.TimeUntilRoundStart == Expected.TimeUntilRoundStart &&
           Actual.MusicName == Expected.MusicName;
}

void UReplayFixtureGameInstance::TravelToVSInfo() const
{
    if (!RoomFixtureStageLifecycle || IsReplay)
    {
        Super::TravelToVSInfo();
        return;
    }
    // Use ordinary selected-stage travel only when the normal callback is invoked.
    // This does not require any particular start/loading callback sequence.
    if (GetWorld() && BattleData.Stage)
    {
        // Preserve this fixture's ordinary travel mode for worker scenarios while
        // keeping an explicitly authored stage option unique.
        FURL URL(nullptr, *BattleData.Stage->StageURL, TRAVEL_Absolute);
        URL.AddOption(TEXT("game=/Script/NightSkyEngine.ReplayFixtureRoomGameMode"));
        GetWorld()->ServerTravel(URL.ToString());
    }
}

void UReplayFixtureGameInstance::UseRoomFixtureBattle(AReplayFixtureBattle *Battle)
{
    RoomFixtureStageLifecycle = true;
    QueueConstructedRoomStage(Battle);
}

void UReplayFixtureGameInstance::QueueConstructedRoomStage(AReplayFixtureBattle *Battle)
{
    if (RoomFixtureStageLifecycle && !IsReplay && IsValid(Battle) && Battle->GetWorld() == GetWorld())
    {
        // A repeated callback for the same actor must not reenter its binding.
        if (AssociatedRoomBattle.Get() == Battle)
            return;
        if (ConstructedRoomStage.Get() != Battle)
        {
            ++RoomFixtureSetupGeneration;
            RoomFixtureBindingComplete = false;
            AssociatedRoomBattle.Reset();
            ConstructedRoomStage = Battle;
        }
        CompleteRoomFixtureBinding();
    }
}

void UReplayFixtureGameInstance::CompleteRoomFixtureBinding()
{
    auto *Battle = ConstructedRoomStage.Get();
    if (!RoomFixtureStageLifecycle || IsReplay || !IsValid(Battle) || Battle->GetWorld() != GetWorld() ||
        !Battle->DidCompleteFixtureInitialization() || !Battle->GetMainPlayer(true) ||
        !Battle->GetMainPlayer(false))
        return;
    const TWeakObjectPtr<AReplayFixtureBattle> BindingActor(Battle);
    const uint64 BindingGeneration = RoomFixtureSetupGeneration;
    ConstructedRoomStage.Reset();
    // Reserve this setup before the public call, but do not expose it as a
    // completed association until dispatch returns. Nested replacement wins.
    AssociatedRoomBattle = BindingActor;
    RoomFixtureBindingComplete = false;
    // Exactly one explicit association per initial/replacement fixture setup.
    // No frame, inputs, configuration, match identity or history is reset here.
    GetSubsystem<USpectatorRoom>()->BindBattle(Battle);
    if (BindingGeneration != RoomFixtureSetupGeneration)
        return;
    auto *CurrentActor = BindingActor.Get();
    RoomFixtureBindingComplete = !IsReplay && IsValid(CurrentActor) && CurrentActor->GetWorld() == GetWorld();
    if (!RoomFixtureBindingComplete)
        AssociatedRoomBattle.Reset();
}

AReplayFixtureBattle *UReplayFixtureGameInstance::AssociatedRoomFixtureBattle() const
{
    auto *Battle = AssociatedRoomBattle.Get();
    return RoomFixtureBindingComplete && !IsReplay && IsValid(Battle) && Battle->GetWorld() == GetWorld()
               ? Battle
               : nullptr;
}

FString UReplayFixtureGameInstance::DescribeRoomFixtureSetup() const
{
    // Read only the exact fixture-owned weak setup records, not discovered actors.
    auto *Recorded = AssociatedRoomBattle.Get();
    auto *Queued = ConstructedRoomStage.Get();
    return FString::Printf(
        TEXT("fixture-lifecycle=%d binding-complete=%d replay=%d recorded-actor=%d recorded-current-world=%d "
             "recorded-init=%d recorded-players=%d/%d queued-actor=%d queued-current-world=%d queued-init=%d queued-players=%d/%d"),
        int32(RoomFixtureStageLifecycle), int32(RoomFixtureBindingComplete), int32(IsReplay), int32(Recorded != nullptr),
        int32(Recorded && Recorded->GetWorld() == GetWorld()), Recorded ? int32(Recorded->DidCompleteFixtureInitialization()) : -1,
        Recorded ? int32(Recorded->GetMainPlayer(true) != nullptr) : -1,
        Recorded ? int32(Recorded->GetMainPlayer(false) != nullptr) : -1, int32(Queued != nullptr),
        int32(Queued && Queued->GetWorld() == GetWorld()), Queued ? int32(Queued->DidCompleteFixtureInitialization()) : -1,
        Queued ? int32(Queued->GetMainPlayer(true) != nullptr) : -1,
        Queued ? int32(Queued->GetMainPlayer(false) != nullptr) : -1);
}

AReplayFixtureRoomGameMode::AReplayFixtureRoomGameMode()
{
    GameStateClass = AReplayFixtureBattle::StaticClass();
    PlayerControllerClass = ANightSkyPlayerController::StaticClass();
    DefaultPawnClass = nullptr;
    bStartPlayersAsSpectators = true;
}

void AReplayFixtureRoomGameMode::StartPlay()
{
    Super::StartPlay();
    if (auto *Game = Cast<UReplayFixtureGameInstance>(GetGameInstance()))
        Game->QueueConstructedRoomStage(GetWorld()->GetGameState<AReplayFixtureBattle>());
}

AReplayFixtureBattle::FSoundStarted AReplayFixtureBattle::SoundStarted;
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyBattleWidget.h"
#include "NightSkyEngine/Data/CollisionData.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Sound/SoundWaveProcedural.h"
#include "Engine/DirectionalLight.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
#include "Math/RotationMatrix.h"
#include "NightSkyEngine/Battle/Actors/ParticleManager.h"

namespace
{
enum EFixturePart
{
    Torso, Hips, Head, Nose, NearEye, FarEye, LeadUpperArm, LeadForearm,
    RearUpperArm, RearForearm, LeadThigh, LeadShin, RearThigh, RearShin,
    LeadHand, RearHand, LeadFoot, RearFoot, Neck, FixturePartCount
};
}

AReplayFixtureFighter::AReplayFixtureFighter()
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    const TCHAR *Names[FixturePartCount] = {
        TEXT("FixtureBody"), TEXT("FixtureHips"), TEXT("FixtureHead"), TEXT("FixtureNose"),
        TEXT("FixtureNearEye"), TEXT("FixtureFarEye"), TEXT("FixtureLeadUpperArm"), TEXT("FixtureLeadForearm"),
        TEXT("FixtureRearUpperArm"), TEXT("FixtureRearForearm"), TEXT("FixtureLeadThigh"), TEXT("FixtureLeadShin"),
        TEXT("FixtureRearThigh"), TEXT("FixtureRearShin"), TEXT("FixtureLeadHand"), TEXT("FixtureRearHand"),
        TEXT("FixtureLeadFoot"), TEXT("FixtureRearFoot"), TEXT("FixtureNeck")};
    for (int32 Part = 0; Part < FixturePartCount; ++Part)
    {
        auto *Mesh = CreateDefaultSubobject<UStaticMeshComponent>(FName(Names[Part]));
        Mesh->SetupAttachment(RootComponent);
        Mesh->SetMobility(EComponentMobility::Movable);
        const bool Round = Part == Head || Part == Nose || Part == NearEye || Part == FarEye ||
                           Part == LeadHand || Part == RearHand;
        const bool Limb = Part >= LeadUpperArm && Part <= RearShin;
        Mesh->SetStaticMesh(Round ? Sphere.Object : Limb ? Cylinder.Object : Cube.Object);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        FixtureParts.Add(Mesh);
    }
    UpdateFixturePose();
}

void AReplayFixtureFighter::InitializeFixtureMaterials()
{
    auto *Base = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    FLinearColor DefaultColor;
    if (!Base || !Base->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Color")), DefaultColor))
    {
        UE_LOG(LogTemp, Error, TEXT("Readable fighter material requires BasicShapeMaterial's verified Color parameter"));
        return;
    }
    const FLinearColor Colors[] = {
        FLinearColor(.035f, .20f, .85f), FLinearColor(.025f, .035f, .05f), FLinearColor(.75f, .52f, .30f)};
    for (const auto &Color : Colors)
    {
        auto *Material = UMaterialInstanceDynamic::Create(Base, this);
        if (!Material)
        {
            UE_LOG(LogTemp, Error, TEXT("Readable fighter material instance creation failed"));
            FixtureMaterials.Reset();
            return;
        }
        Material->SetVectorParameterValue(TEXT("Color"), Color);
        FixtureMaterials.Add(Material);
    }
    for (int32 Part = 0; Part < FixtureParts.Num(); ++Part)
    {
        const bool Skin = Part == Head || Part == Nose || Part == Neck;
        const bool Trim = Part == NearEye || Part == FarEye || Part == LeadHand || Part == RearHand ||
                          Part == LeadFoot || Part == RearFoot || Part == Hips;
        FixtureParts[Part]->SetMaterial(0, FixtureMaterials[Skin ? 2 : Trim ? 1 : 0]);
    }
}

void AReplayFixtureFighter::UpdateFixturePose()
{
    if (FixtureParts.Num() != FixturePartCount)
        return;
    // The native visual update already places, mirrors and hides the fighter.
    // These local poses are functions only of its current combat state.
    const bool Attacking = GetCelName() == State_Universal_Throw || ObjectReg2 >= 45;
    const bool Hit = PrimaryStateMachine.CurrentState && GetStateType() == EStateType::Hitstun;
    const bool Moving = (Inputs & (INP_Left | INP_Right)) != 0;
    const float Stride = Moving && !Attacking && !Hit ? FMath::Sin(float(ActionTime % 20) * PI / 10.f) * 12.f : 0.f;
    const float Lean = Hit ? -12.f : Attacking ? 8.f : 0.f;
    auto Shape = [this](int32 Part, const FVector &Location, const FVector &Size)
    {
        FixtureParts[Part]->SetRelativeLocation(Location);
        FixtureParts[Part]->SetRelativeRotation(FRotator::ZeroRotator);
        FixtureParts[Part]->SetRelativeScale3D(Size / 100.f);
    };
    auto Segment = [this](int32 Part, const FVector &From, const FVector &To, float Diameter)
    {
        const FVector Delta = To - From;
        FixtureParts[Part]->SetRelativeLocation((From + To) * .5f);
        FixtureParts[Part]->SetRelativeRotation(FRotationMatrix::MakeFromZ(Delta).Rotator());
        FixtureParts[Part]->SetRelativeScale3D(FVector(Diameter, Diameter, Delta.Size()) / 100.f);
    };
    Shape(Torso, FVector(Lean, 0, 106), FVector(FixtureTorsoWidth, 28, 52));
    FixtureParts[Torso]->SetRelativeRotation(FRotator(Lean, 0, 0));
    Shape(Hips, FVector(0, 0, 72), FVector(32, 27, 20));
    Shape(Neck, FVector(Lean + 2, 0, 137), FVector(12, 12, 14));
    Shape(Head, FVector(Lean + 2, 0, 157), FVector(32, 30, 36));
    Shape(Nose, FVector(Lean + 20, 0, 158), FVector(12, 17, 11));
    Shape(NearEye, FVector(Lean + 10, -14, 164), FVector(6, 5, 6));
    Shape(FarEye, FVector(Lean + 10, 14, 164), FVector(6, 5, 6));
    const FVector LeadShoulder(Lean + 8, -19, 126);
    const FVector RearShoulder(Lean - 8, 19, 123);
    const FVector LeadElbow = Hit ? FVector(-2, -23, 126) : Attacking ? FVector(42, -19, 125) : FVector(25, -21, 108);
    const FVector LeadFist = Hit ? FVector(13, -24, 146) : Attacking ? FVector(72, -19, 124) : FVector(40, -21, 131);
    const FVector RearElbow(-23, 20, Hit ? 120 : 100);
    const FVector RearFist(Hit ? 4 : 7, 20, Hit ? 145 : 117);
    Segment(LeadUpperArm, LeadShoulder, LeadElbow, 13);
    Segment(LeadForearm, LeadElbow, LeadFist, 11);
    Segment(RearUpperArm, RearShoulder, RearElbow, 13);
    Segment(RearForearm, RearElbow, RearFist, 11);
    Shape(LeadHand, LeadFist, FVector(20, 19, 20));
    Shape(RearHand, RearFist, FVector(20, 19, 20));
    const FVector LeadKnee(22 + Stride, -6, 37);
    const FVector RearKnee(-20 - Stride, 6, 38);
    const FVector LeadAnkle(29 + Stride, -6, 9);
    const FVector RearAnkle(-30 - Stride, 6, 9);
    Segment(LeadThigh, FVector(11, -6, 70), LeadKnee, 17);
    Segment(LeadShin, LeadKnee, LeadAnkle, 14);
    Segment(RearThigh, FVector(-11, 6, 70), RearKnee, 17);
    Segment(RearShin, RearKnee, RearAnkle, 14);
    Shape(LeadFoot, LeadAnkle + FVector(7, 0, -2), FVector(28, 21, 14));
    Shape(RearFoot, RearAnkle + FVector(7, 0, -2), FVector(28, 21, 14));
}

void AReplayFixtureFighter::UpdateVisualsNoRollback()
{
    Super::UpdateVisualsNoRollback();
    if (FixtureMaterials.Num() == 3 && FixtureTintPlayerIndex != PlayerIndex)
    {
        FixtureMaterials[0]->SetVectorParameterValue(TEXT("Color"), PlayerIndex == 0
            ? FLinearColor(.035f, .20f, .85f) : FLinearColor(.9f, .10f, .025f));
        FixtureTintPlayerIndex = PlayerIndex;
    }
    UpdateFixturePose();
}

void AReplayFixtureFighter::BeginPlay()
{
    Super::BeginPlay();
    InitializeFixtureMaterials();
    IntroName = State_Universal_Throw;
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
    auto *Stand = NewObject<UReplayFixtureState>(this);
    Stand->Name = State_Universal_Stand;
    Stand->StateType = EStateType::Standing;
    Stand->bHumanUsable = false;
    AddState(Stand->Name, Stand, StateMachine_Primary);
    for (int32 I = 0; I < 8; ++I)
    {
        auto *Projectile = NewObject<UReplayFixtureProjectile>(this);
        Projectile->Name = State_Universal_Throw;
        AddObjectState(Projectile->Name, Projectile, false);
    }
    // Hit reactions use the engine's collision and health path with a fixed recovery state.
    for (auto Tag : {FGameplayTag(State_Universal_StandBlockEnd), FGameplayTag(State_Universal_Crouch),
                     FGameplayTag(State_Universal_MatchWin), FGameplayTag(State_Universal_RoundWin),
                     FGameplayTag(State_Universal_RoundLose), FGameplayTag(State_Universal_Throw)})
    {
        auto *Other = NewObject<UReplayFixtureState>(this);
        Other->Name = Tag;
        Other->bHumanUsable = false;
        if (Tag == State_Universal_Crouch)
            Other->StateType = EStateType::Hitstun;
        AddState(Tag, Other, StateMachine_Primary);
    }
}

void UReplayFixtureState::Exec_Implementation()
{
    auto *Fighter = Cast<APlayerObject>(Parent);
    if (!Fighter || !Fighter->GameState)
        return;
    if (Fighter->GameState->BattleState.BattlePhase == EBattlePhase::Intro)
    {
        Fighter->IntroEndFlag = true;
        Fighter->SetCelName(State_Universal_Stand);
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
    if (Fighter->ObjectReg2 > 0)
        --Fighter->ObjectReg2;
    if ((Fighter->Inputs & INP_B) && Fighter->ObjectReg2 == 0)
    {
        Fighter->ObjectReg2 = 50;
        CastChecked<AReplayFixtureFighter>(Fighter)->EmitAttackSound();
        if (auto *Projectile = Fighter->AddBattleObject(State_Universal_Throw))
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
        --Fighter->ObjectReg1;
    if ((Fighter->Inputs & INP_A) && Fighter->ObjectReg1 == 0)
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
        Fighter->SetAttacking(false);
}

AReplayFixtureBattle::AReplayFixtureBattle()
{
    MaxBattleObjects = 16;
    BattleObjectClass = AReplayFixtureProjectileActor::StaticClass();
    BattleState.RoundStartPos = 150000;
}

void AReplayFixtureBattle::BeginPlay()
{
    // Record authored construction inputs before the public native lifecycle can consume staging data.
    const auto *Game = CastChecked<UNightSkyGameInstance>(GetGameInstance());
    InitialConfiguration = Game->BattleData;
    InitialTraining = Game->IsTraining;
    InitialAssets.Add(InitialConfiguration.Stage);
    for (const auto &Character : InitialConfiguration.PlayerListP1)
        InitialAssets.Add(Character.LoadSynchronous());
    for (const auto &Character : InitialConfiguration.PlayerListP2)
        InitialAssets.Add(Character.LoadSynchronous());
    auto *Light = GetWorld()->SpawnActor<ADirectionalLight>();
    Light->GetRootComponent()->SetMobility(EComponentMobility::Movable);
    Light->SetActorRotation(FRotator(-45, -45, 0));
    BattleHudActor = GetWorld()->SpawnActor<ANightSkyBattleHudActor>();
    BattleHudActor->TopWidget = NewObject<UNightSkyBattleWidget>(GetWorld());
    BattleHudActor->BottomWidget = NewObject<UNightSkyBattleWidget>(GetWorld());
    Super::BeginPlay();
    for (auto *Audio : AudioManager->CommonAudioPlayers)
        Audio->OnAudioPlayStateChangedNative.AddWeakLambda(
            this, [this](const UAudioComponent *, EAudioComponentPlayState State) {
                if (State == EAudioComponentPlayState::Playing)
                    SoundStarted.Broadcast(this);
            });
    // Marks completion of this authored fixture's original native setup path.
    // It is not a claim that later room-side work has also completed.
    FixtureInitializationComplete = true;
}

void UReplayFixtureProjectile::Exec_Implementation()
{
    if (!Parent)
        return;
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
        Parent->DeactivateObject();
}

void AReplayFixtureFighter::EmitAttackSound()
{
    if (FApp::CanEverRender())
    {
        auto *Effect = LoadObject<UNiagaraSystem>(
            nullptr,
            TEXT(
                "/Game/NightSkyEngine/CharacterAssets/Common/Particles/Niagara/cmn_jumpsmoke.cmn_jumpsmoke"));
        if (Effect)
            if (auto *Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                    this, Effect,
                    GameState->BattleSceneTransform.TransformPosition(FVector(
                        float(PosX) / COORD_SCALE, float(PosZ) / COORD_SCALE, float(PosY) / COORD_SCALE))))
            {
                Component->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
                Component->SetDesiredAge(0);
                GameState->ParticleManager->BattleParticles.Add(FBattleParticle(Component, this));
            }
    }
    Tone = NewObject<USoundWaveProcedural>(this);
    Tone->SetSampleRate(22050);
    Tone->NumChannels = 1;
    Tone->Duration = INDEFINITELY_LOOPING_DURATION;
    TArray<int16> Samples;
    Samples.SetNumUninitialized(2205);
    for (int32 I = 0; I < Samples.Num(); ++I)
        Samples[I] = I % 50 < 25 ? 4000 : -4000;
    Tone->QueueAudio(reinterpret_cast<const uint8 *>(Samples.GetData()), Samples.Num() * sizeof(int16));
    GameState->PlayCommonAudio(Tone, .1f);
}

AReplayFixtureProjectileActor::AReplayFixtureProjectileActor()
{
    auto *Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileBody"));
    Body->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Body->SetStaticMesh(Mesh.Object);
    Body->SetRelativeScale3D(FVector(.25, .25, .25));
    Body->SetRelativeLocation(FVector(0, 0, 100));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

AReplayFixtureHeavyFighter::AReplayFixtureHeavyFighter()
{
    MaxHealth = 12000;
    FixtureTorsoWidth = 54.f;
}

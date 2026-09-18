#include "SweptProjectilePlaytest.h"

#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NativeGameplayTags.h"
#include "UObject/ConstructorHelpers.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Data/CollisionData.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(PlaytestShot, "UnrealBench.Playtest.SweptShot");
UE_DEFINE_GAMEPLAY_TAG_STATIC(PlaytestShotCel, "UnrealBench.Playtest.SweptShotCel");

namespace
{
FVector WorldPosition(ANightSkyGameState* Game, int32 X, int32 Y)
{
    // Battle objects apply rotation and translation only, even if the scene actor is scaled.
    return Game->BattleSceneTransform.GetRotation().RotateVector(FVector(X / COORD_SCALE, 0, Y / COORD_SCALE))
        + Game->BattleSceneTransform.GetLocation();
}
}

bool NSEPlaytest::CastPoseTime(const APlayerObject* Player, float& Time)
{
    if (!Player || !IsValid(Player->GameState)) return false;
    const auto* Game = Player->GameState.Get();
    const auto& B = Game->BattleState;
    if (!Game->GameInstance || !Game->GameInstance->bSweptProjectilePlaytest ||
        B.MainPlayer[0] != Player || B.PlaytestShots == 0) return false;
    const int32 Frame = B.FrameNumber - B.PlaytestLastShotFrame;
    if (Frame < 0 || Frame >= 26) return false;
    // Snap into the punch, hold the extended fist, then recover into the normal pose.
    Time = Frame < 5 ? .10f + Frame * .012f : Frame < 15 ? .16f : .16f + (Frame - 15) * .018f;
    return true;
}

FVector NSEPlaytest::FistPosition(APlayerObject* Player)
{
    for (auto* Component : TInlineComponentArray<USkeletalMeshComponent*>(Player))
    {
        if (Component->IsVisible() && Component->DoesSocketExist(TEXT("hand_l")))
        {
            // NightSky manually advances animation; evaluate that new pose before
            // reading its socket so the emitter and rendered fist use the same pose.
            Component->RefreshBoneTransforms();
            return Component->GetSocketLocation(TEXT("hand_l"));
        }
    }
    // Fixture/minimal fighters have no render mesh; gameplay still works without one.
    return WorldPosition(Player->GameState.Get(), Player->PosX + (Player->Direction == DIR_Right ? 55000 : -55000), Player->PosY + 240000);
}

void NSEPlaytest::Prepare(ANightSkyGameState* Game)
{
    for (auto* Player : Game->Players)
    {
        Player->SetContactOrderKey(Player->ObjNumber);
        if (Player->ObjectStateNames.Contains(PlaytestShot)) continue;
        // Clone the data asset before adding a cel: never mutate shared fighter assets.
        Player->CollisionData = Player->CollisionData
            ? DuplicateObject<UCollisionData>(Player->CollisionData, Player)
            : NewObject<UCollisionData>(Player);
        FCollisionStruct Cel;
        Cel.CelName = PlaytestShotCel;
        FCollisionBox Box;
        Box.Type = BOX_Hit;
        Box.SizeX = 24000;
        Box.SizeY = 24000;
        Cel.Boxes.Add(Box);
        Player->CollisionData->CollisionFrames.Add(Cel);
        auto* State = NewObject<USweptPlaytestShot>(Player);
        State->Name = PlaytestShot;
        Player->AddObjectState(PlaytestShot, State, false);
    }
}

void NSEPlaytest::Update(ANightSkyGameState* Game, int32 Input)
{
    auto& Battle = Game->BattleState;
    const bool bPressed = (Input & FireInput) != 0;
    const bool bRisingEdge = bPressed && !Battle.bPlaytestFireHeld;
    Battle.bPlaytestFireHeld = bPressed;
    if (!bRisingEdge || Battle.BattlePhase != EBattlePhase::Battle || Battle.SuperFreezeDuration) return;
    auto* Player = Battle.MainPlayer[0];
    if (!Player || Player->CurrentHealth <= 0) return;
    Prepare(Game);
    const int32 Index = Player->ObjectStateNames.Find(PlaytestShot);
    const int32 X = Player->PosX + (Player->Direction == DIR_Right ? 55000 : -55000);
    const int32 Y = Player->PosY + 240000;
    if (auto* Shot = Game->AddBattleObject(Player->ObjectStates[Index], X, Y, Player->Direction, Index, false, Player))
    {
        CastChecked<USweptPlaytestShot>(Shot->ObjectState)->Configure();
        ++Battle.PlaytestShots;
        Battle.PlaytestLastShotFrame = Battle.FrameNumber;
        CastChecked<USweptPlaytestShot>(Shot->ObjectState)->Presentation = ASweptShotEffect::Launch(Game, Player, Shot);
        UE_LOG(LogTemp, Display, TEXT("NSE0121 Shot %d: swept projectile fired"), Battle.PlaytestShots);
    }
}

void USweptPlaytestShot::Configure()
{
    Presentation.Reset();
    Parent->ObjectStateName = PlaytestShot;
    Parent->ConfigureSweptProjectile(true, false, 1);
    Parent->AttackFlags = ATK_IsAttacking | ATK_HitActive | ATK_AttackProjectileAttribute;
    Parent->MiscFlags = 0;
    Parent->Gravity = 0;
    // The first frame is stationary: newly spawned objects are endpoint-only.
    Parent->SpeedX = 0;
    Parent->SetCelName(PlaytestShotCel);
    Parent->GetBoxes();
    Parent->NormalHit.Damage = 400;
    Parent->NormalHit.Hitstop = 3;
    Parent->NormalHit.EnemyHitstopModifier = 0;
    Parent->NormalHit.InitialProration = 100;
    Parent->NormalHit.ForcedProration = 100;
    Parent->NormalHit.MinimumDamagePercent = 100;
    Parent->NormalHit.GroundHitAction = HACT_None;
    Parent->NormalHit.AirHitAction = HACT_None;
    Parent->HitCommon.ChipDamagePercent = 10;
    Parent->InitEventHandler(EVT_Hit, "Contact", 0, FGameplayTag::EmptyTag);
    Parent->InitEventHandler(EVT_Block, "Contact", 0, FGameplayTag::EmptyTag);
}

void USweptPlaytestShot::Exec_Implementation()
{
    Parent->SpeedX = 400000;
    if (Parent->ActionTime >= 8) Parent->DeactivateObject();
}

void USweptPlaytestShot::Contact()
{
    auto* Game = Parent->GameState.Get();
    ++Game->BattleState.PlaytestHits;
    Game->BattleState.PlaytestLastHitFrame = Game->BattleState.FrameNumber;
    if (Parent->AttackTarget)
    {
        const FVector Impact = WorldPosition(Game, Parent->AttackTarget->PosX, Parent->PosY);
        if (Presentation.IsValid()) Presentation->SetImpact(Impact);
    }
    UE_LOG(LogTemp, Display, TEXT("NSE0121 Contact %d: ordinary swept hit/block callback"), Game->BattleState.PlaytestHits);
}

ASweptShotEffect::ASweptShotEffect()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork; // Sample the fist after skeletal pose evaluation.
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PlasmaRoot")));
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlasmaCore"));
    Halo = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlasmaHalo"));
    Muzzle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FistCorona"));
    Filaments = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("LightningFilaments"));
    Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("ElectricBlueLight"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> CoreAsset(TEXT("/Game/Playtest0121/M_PlasmaCore.M_PlasmaCore"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> HaloAsset(TEXT("/Game/Playtest0121/M_PlasmaHalo.M_PlasmaHalo"));
    ShotMaterial = CoreAsset.Object;
    HaloMaterial = HaloAsset.Object;
    for (UStaticMeshComponent* Component : {Mesh.Get(), Halo.Get(), Muzzle.Get(), static_cast<UStaticMeshComponent*>(Filaments.Get())})
    {
        Component->SetupAttachment(RootComponent);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCastShadow(false);
        Component->SetCanEverAffectNavigation(false);
    }
    Mesh->SetStaticMesh(Sphere.Object);
    Halo->SetStaticMesh(Sphere.Object);
    Muzzle->SetStaticMesh(Sphere.Object);
    Filaments->SetStaticMesh(Cube.Object); // Twelve triangles per lightning segment, one instanced draw.
    Light->SetupAttachment(RootComponent);
    Light->SetCastShadows(false);
    Light->SetIntensityUnits(ELightUnits::Lumens);
    Light->SetLightColor(FLinearColor(.015f, .16f, 1.f));
    Light->SetAttenuationRadius(220.f);
}

ASweptShotEffect* ASweptShotEffect::Launch(ANightSkyGameState* Game, APlayerObject* Owner, ABattleObject* Shot)
{
    if (Game->IsResimulating() || IsRunningCommandlet()) return nullptr;
    auto* Effect = Game->GetWorld()->SpawnActor<ASweptShotEffect>();
    if (!Effect) return nullptr;
    Effect->Caster = Owner;
    Effect->Projectile = Shot;
    Effect->ShotNumber = Game->BattleState.PlaytestShots;
    Effect->Tip = NSEPlaytest::FistPosition(Owner);
    Effect->AddTickPrerequisiteActor(Game);
    Effect->CoreMID = UMaterialInstanceDynamic::Create(Effect->ShotMaterial, Effect);
    Effect->HaloMID = UMaterialInstanceDynamic::Create(Effect->HaloMaterial, Effect);
    Effect->ArcMID = UMaterialInstanceDynamic::Create(Effect->ShotMaterial, Effect);
    Effect->CoreMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(.03f, .20f, 1.f));
    Effect->HaloMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(.005f, .07f, 1.f));
    Effect->ArcMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(.004f, .05f, 1.f));
    Effect->Mesh->SetMaterial(0, Effect->CoreMID);
    Effect->Halo->SetMaterial(0, Effect->HaloMID);
    Effect->Muzzle->SetMaterial(0, Effect->HaloMID);
    Effect->Filaments->SetMaterial(0, Effect->ArcMID);
    // Fixed instance budget: no per-frame allocation/spawn for lightning or sparks.
    for (int32 I = 0; I < 128; ++I) Effect->Filaments->AddInstance(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector));
    Effect->SetLifeSpan(.48f);
    return Effect;
}

void ASweptShotEffect::SetImpact(FVector Position)
{
    // Place the visible discharge just in front of the struck surface, not
    // buried inside the target's opaque torso. Collision already resolved.
    Tip = Position;
    if (Caster.IsValid()) Tip += (NSEPlaytest::FistPosition(Caster.Get()) - Position).GetSafeNormal() * 18.f;
    bContact = true;
}

void ASweptShotEffect::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* Owner = Caster.Get();
    if (!Owner || !IsValid(Owner->GameState)) { Destroy(); return; }
    auto* Game = Owner->GameState.Get();
    // Training reset/rollback must not leave light or trails from a discarded shot.
    if (Game->BattleState.PlaytestShots < ShotNumber) { Destroy(); return; }
    Age += DeltaSeconds;
    const FVector Start = NSEPlaytest::FistPosition(Owner);
    if (!bContact && Projectile.IsValid() && Projectile->IsActive)
        Tip = WorldPosition(Game, Projectile->PosX, Projectile->PosY);
    const FVector Axis = (Tip - Start).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    FVector Side, Up;
    Axis.FindBestAxisVectors(Side, Up);
    const float Fade = FMath::Clamp((.48f - Age) / .22f, 0.f, 1.f);
    const float Pulse = .85f + .15f * FMath::Sin(Age * 95.f);
    const float Radius = (bContact ? 10.f + Age * 22.f : 9.f) * Fade * Pulse;
    Muzzle->SetWorldLocation(Start);
    Muzzle->SetWorldScale3D(FVector(.24f * Fade * Pulse));
    Mesh->SetWorldLocation(Tip);
    Mesh->SetWorldScale3D(FVector(Radius * .02f));
    Halo->SetWorldLocation(Tip);
    Halo->SetWorldScale3D(FVector(Radius * .05f));
    CoreMID->SetScalarParameterValue(TEXT("Energy"), 16.f * Fade);
    ArcMID->SetScalarParameterValue(TEXT("Energy"), 8.f * Fade * Pulse);
    HaloMID->SetScalarParameterValue(TEXT("Energy"), 5.f * Fade);
    Light->SetWorldLocation(FMath::Lerp(Start, Tip, .25f));
    Light->SetIntensity(1400.f * Fade * Pulse);
    int32 Instance = 0;
    auto Segment = [&](const FVector& A, const FVector& B, float Width)
    {
        const FVector Delta = B - A;
        Filaments->UpdateInstanceTransform(Instance++, FTransform(Delta.Rotation(), (A+B)*.5f,
            FVector(Delta.Size()/100.f, Width/100.f, Width/100.f)), true, false, true);
    };
    // Three braided, irregular plasma channels start exactly on the fist.
    for (int32 Strand = 0; Strand < 3; ++Strand)
    {
        FVector Previous = Start;
        for (int32 J = 1; J <= 16; ++J)
        {
            const float T = J / 16.f;
            const float Phase = T * 38.f - Age * 70.f + Strand * 2.094f;
            const float Envelope = FMath::Sin(T * PI) * (3.f + Strand * 2.f) * Fade;
            const FVector P = FMath::Lerp(Start, Tip, T) + Envelope *
                (Side * FMath::Sin(Phase) + Up * FMath::Cos(Phase * 1.7f + J));
            Segment(Previous, P, (Strand == 0 ? 1.8f : .8f) * Fade);
            Previous = P;
        }
    }
    // Tumbling lightning cages give the head a ball-lightning silhouette.
    for (int32 Ring = 0; Ring < 3; ++Ring)
    {
        const FVector A = Ring == 0 ? Side : Axis;
        const FVector B = Ring == 2 ? Side : Up;
        auto RingPoint = [&](int32 J)
        {
            const float Angle = J * 2.f * PI / 20.f;
            const float R = Radius * (1.4f + .2f * FMath::Sin(J * 3.f + Age * 80.f));
            return Tip + R * (A * FMath::Cos(Angle + Age * 8.f) + B * FMath::Sin(Angle + Age * 8.f));
        };
        for (int32 J = 0; J < 20; ++J) Segment(RingPoint(J), RingPoint(J+1), .9f * Fade);
    }
    // Short ballistic streaks spray forward on contact, trailing backwards on a miss.
    for (int32 J = 0; J < 20; ++J)
    {
        const float Angle = J * 2.39996f;
        const FVector Direction = (Side * FMath::Cos(Angle) + Up * FMath::Sin(Angle) + Axis * (bContact ? .35f : -1.2f)).GetSafeNormal();
        const float Travel = FMath::Fmod(Age * (100.f + J * 7.f), 48.f);
        const FVector A = Tip + Direction * (Radius + Travel);
        Segment(A, A + Direction * (5.f + J % 4 * 2.f), .7f * Fade);
    }
    Filaments->MarkRenderStateDirty();
}

void ASweptPlaytestHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    auto* Game = GetWorld()->GetGameState<ANightSkyGameState>();
    if (!Game) return;
    const float W = FMath::Min(760.f, Canvas->SizeX - 32.f);
    const float X = (Canvas->SizeX - W) * .5f;
    const float Y = Canvas->SizeY - 108.f;
    DrawRect(FLinearColor(.008f, .018f, .033f, .92f), X, Y, W, 78);
    DrawRect(FLinearColor(.02f, .65f, 1), X, Y, 4, 78);
    DrawText(TEXT("F - Fire swept projectile     R - Reset training"), FLinearColor::White,
        X + 18, Y + 12, GEngine->GetMediumFont(), 1.05f);
    const auto& Battle = Game->BattleState;
    const bool bHit = Battle.PlaytestHits > 0 && Battle.FrameNumber - Battle.PlaytestLastHitFrame < 45;
    const FString Status = FString::Printf(TEXT("%s    Shots: %d    Contacts: %d    |    Release F to fire again"),
        bHit ? TEXT("CONTACT") : TEXT("READY"), Battle.PlaytestShots, Battle.PlaytestHits);
    DrawText(Status, bHit ? FLinearColor(1, .5f, .1f) : FLinearColor(.3f, .8f, 1),
        X + 18, Y + 44, GEngine->GetSmallFont(), 1.f);
}

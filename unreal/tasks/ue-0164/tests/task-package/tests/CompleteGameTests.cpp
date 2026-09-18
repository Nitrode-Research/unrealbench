#include "CompleteGameFixtures.h"
#include "Camera/CameraComponent.h"
#include "Components/LightComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/WorldInitializationValues.h"
#include "Facility/FacilitySettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

namespace SSEGame
{
constexpr auto Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
using Circuit = EFacilityCircuit;

struct FWorld
{
    UFacilitySettings* Settings = GetMutableDefault<UFacilitySettings>();
    float OldStep = Settings->StepSeconds;
    TArray<FDoorLockConfig> Locks = Settings->DoorLocks;
    TArray<FPickupConfig> Pickups = Settings->Pickups;
    TArray<FHackTargetConfig> Hacks = Settings->HackTargets;
    UWorld* World = nullptr;
    explicit FWorld()
    {
        Settings->StepSeconds = 0;
        Settings->DoorLocks.Reset(); Settings->Pickups.Reset(); Settings->HackTargets.Reset();
        FDoorLockConfig Gate; Gate.Id = TEXT("JourneyGate"); Gate.LockLevel = 3;
        Settings->DoorLocks.Add(Gate);
        FDoorLockConfig Tunnel; Tunnel.Id = TEXT("JourneyTunnel"); Tunnel.KeyItem = EFacilityItem::ServiceKey;
        Settings->DoorLocks.Add(Tunnel);
        FHackTargetConfig Controller; Controller.Id = TEXT("JourneyControl"); Controller.Level = 2;
        Settings->HackTargets.Add(Controller);
        FPickupConfig Drive; Drive.Id = TEXT("JourneyDrive"); Drive.Item = EFacilityItem::ServerDrive;
        FPickupConfig Pry; Pry.Id = TEXT("JourneyPry"); Pry.Item = EFacilityItem::PryBar;
        Settings->Pickups.Add(Drive); Settings->Pickups.Add(Pry);
        if (!GEngine) return;
        FWorldInitializationValues Values;
        Values.SetDefaultGameMode(nullptr).CreatePhysicsScene(true);
        World = UWorld::CreateWorld(EWorldType::Game, false,
            MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("SSEGame")),
            GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values, true);
        if (!World) return;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitWorld(Values); World->InitializeActorsForPlay(FURL()); World->BeginPlay();
        World->GetWorldSettings()->NotifyBeginPlay();
    }
    ~FWorld()
    {
        if (World)
        {
            World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }
        Settings->StepSeconds = OldStep;
        Settings->DoorLocks = Locks; Settings->Pickups = Pickups; Settings->HackTargets = Hacks;
    }
    UFacilityStateSubsystem* Facility() const { return World ? World->GetSubsystem<UFacilityStateSubsystem>() : nullptr; }
    template<class T, class Configure> T* Make(FVector Position, Configure Setup)
    {
        if (!World) return nullptr;
        const FTransform Transform(Position);
        T* Actor = World->SpawnActorDeferred<T>(T::StaticClass(), Transform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (Actor) { Setup(Actor); Actor->FinishSpawning(Transform); }
        return Actor;
    }
    template<class T> T* Spawn(FVector Position = FVector(10000,0,0))
    { return Make<T>(Position, [](T*) {}); }
};

UBoxComponent* InteractionCollision(AActor* Owner)
{
    UBoxComponent* Box = NewObject<UBoxComponent>(Owner);
    Owner->AddInstanceComponent(Box); Box->SetupAttachment(Owner->GetRootComponent());
    Box->InitBoxExtent(FVector(25)); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldDynamic); Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block); Box->RegisterComponent();
    return Box;
}

void Enter(ASSEGamePlayer* Player, UBoxComponent* Volume)
{
    Player->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
    Player->SetActorLocation(Volume->GetComponentLocation(), false, nullptr, ETeleportType::TeleportPhysics);
    Player->GetCapsuleComponent()->UpdateOverlaps(); Volume->UpdateOverlaps();
}

void Aim(ASSEGamePlayer* Player)
{
    Player->GetFirstPersonCameraComponent()->SetWorldLocationAndRotation(FVector(0,0,160), FRotator::ZeroRotator);
}
}

#define NEED(X) if (!TestNotNull(TEXT(#X), X)) return false

// REQUIRED: true submitted player->interactor->pickup/base->rules->gate->exit->GameMode.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSEGameGate, "GameEngineBench.UE0164.EndToEnd.PlayerUpgradeGateEscapeReset", SSEGame::Flags)
bool FSSEGameGate::RunTest(const FString&)
{
    using namespace SSEGame; FWorld F; auto* S = F.Facility(); NEED(S);
    auto* Player = F.Spawn<ASSEGamePlayer>(FVector(0,0,96)); NEED(Player);
    auto* PC = F.Spawn<APlayerController>(); NEED(PC); PC->Possess(Player);
    auto* Mode = F.Spawn<ASSEGameModeObserver>(); NEED(Mode);
    auto* Gate = F.Make<AGE152Door>(FVector(3000,0,0), [](auto* D) { D->Configure(TEXT("JourneyGate"), Circuit::EFC_Doors, EDoorKind::Gate); }); NEED(Gate);
    auto* Exit = F.Make<ASSEGameExit>(FVector(3500,0,100), [](auto* E) { E->Configure(EFacilityExit::MainGate); }); NEED(Exit);
    auto* Drive = F.Make<AGE152Pickup>(FVector(220,0,160), [](auto* P) { P->Configure(TEXT("JourneyDrive")); }); NEED(Drive);
    InteractionCollision(Drive);
    auto* Binding = NewObject<USSEGameVerbBinding>(Player); Binding->Pickup = Drive;
    Drive->GetInteractable()->OnInteracted.AddDynamic(Binding, &USSEGameVerbBinding::Take);
    auto* Interactor = Player->FindComponentByClass<UInteractorComponent>(); NEED(Interactor);
    Aim(Player); Interactor->StartSphereDetection(); Interactor->PerformSphereDetection();
    if (!TestEqual(TEXT("Player focuses real pickup"), Interactor->GetFocusedInteractable(), Drive->GetInteractable())) return false;
    Player->PressInteract();
    TestEqual(TEXT("Acting owner travels through dispatch"), Binding->LastUser.Get(), static_cast<AActor*>(Player));
    if (!TestTrue(TEXT("Pickup upgrades live handheld through submitted rules"), S->IsServerDrivePulled() && S->GetHandheldLevel() >= 2)) return false;
    Drive->Destroy();
    auto* Controller = F.Make<ASSE153GateController>(FVector(220,0,160), [](auto* C) { C->Configure(TEXT("JourneyControl")); }); NEED(Controller);
    InteractionCollision(Controller); Interactor->PerformSphereDetection();
    if (!TestEqual(TEXT("Focus switches to controller"), Interactor->GetFocusedInteractable(), Controller->GetInteractable())) return false;
    Player->PressHack();
    if (!TestTrue(TEXT("Real hack opens registered gate"), Gate->IsOpen())) return false;
    TestFalse(TEXT("Opening from a distance does not win"), S->HasEscaped());
    Enter(Player, Exit->Box());
    TestTrue(TEXT("Crossing open threshold wins"), S->HasEscaped() && Mode->HasEscaped());
    TestEqual(TEXT("Exactly one victory"), Mode->Wins, 1);
    TestTrue(TEXT("Movement and look frozen, not generic UI input"), PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
    S->GiveItem(EFacilityItem::ServiceKey);
    TestEqual(TEXT("Unrelated state changes do not replay win"), Mode->Wins, 1);
    S->ResetToInitialState();
    TestFalse(TEXT("Reset clears victory"), S->HasEscaped() || Mode->HasEscaped());
    TestFalse(TEXT("Reset releases both input counts"), PC->IsMoveInputIgnored() || PC->IsLookInputIgnored());
    TestFalse(TEXT("Gate and inventory reset together"), Gate->IsOpen() || S->IsServerDrivePulled());
    return true;
}

// REQUIRED: sabotage must flow through actor rules into lighting, stealth and flood.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSEGameSabotage, "GameEngineBench.UE0164.EndToEnd.PowerStealthSabotageReset", SSEGame::Flags)
bool FSSEGameSabotage::RunTest(const FString&)
{
    using namespace SSEGame; FWorld F; auto* S = F.Facility(); NEED(S);
    auto* Player = F.Spawn<ASSEGamePlayer>(FVector(0,0,96)); NEED(Player);
    auto* Lamp = F.Make<ASSEGameLamp>(FVector(2000,0,0), [](auto* L) { L->Configure(TEXT("JourneyLights"), Circuit::EFC_Doors); }); NEED(Lamp);
    auto* Zone = F.Make<ASSE153LightZone>(FVector(0,0,100), [](auto* Z) { Z->Configure(TEXT("JourneyLights")); }); NEED(Zone);
    auto* Breaker = F.Make<AGE152Breaker>(FVector(4000,0,0), [](auto* B) { B->Configure(Circuit::EFC_Doors); }); NEED(Breaker);
    Player->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
    Player->GetCapsuleComponent()->UpdateOverlaps(); Zone->Box()->UpdateOverlaps();
    Player->GetCharacterMovement()->Velocity = FVector(400,0,0);
    auto* Stealth = Player->GetStealthComponent(); NEED(Stealth);
    if (!TestTrue(TEXT("Live lamp starts lit"), Lamp->IsLit())) return false;
    S->SetCircuitState(Circuit::EFC_Doors, ECircuitState::Cut);
    TestFalse(TEXT("Shared power immediately darkens lamp"), Lamp->IsLit());
    TestEqual(TEXT("Live stealth reads blackout"), Stealth->GetStealthState(), EStealthState::Dark);
    TInlineComponentArray<ULightComponent*> Lights; Lamp->GetComponents(Lights);
    if (!TestTrue(TEXT("Fixture has actual light components"), Lights.Num() > 0)) return false;
    for (auto* Light : Lights) TestFalse(TEXT("Every owned light follows blackout"), Light->IsVisible());
    auto* Generator = F.Spawn<AGE152Generator>(); NEED(Generator);
    TestTrue(TEXT("Backup restores cut supply"), Generator->Start());
    TestTrue(TEXT("Lamp follows effective generator power"), Lamp->IsLit());
    auto* Pry = F.Make<AGE152Pickup>(FVector(5000,0,0), [](auto* P) { P->Configure(TEXT("JourneyPry")); }); NEED(Pry);
    TestTrue(TEXT("Real pickup supplies sabotage tool"), Pry->Take());
    auto* Valve = F.Spawn<ASSEGameValve>(); NEED(Valve);
    auto* Rack = F.Make<ASSEGameRack>(FVector(6000,0,0), [](auto* R) { R->Configure(TEXT("JourneyRack")); }); NEED(Rack);
    TestTrue(TEXT("Valve actor opens"), Valve->Use());
    TestTrue(TEXT("Valve writes shared flood and prevents drainage"), S->IsPlantFlooded() && S->IsCoolantValveOpen());
    TestTrue(TEXT("Valve actor closes"), Valve->Use());
    TestTrue(TEXT("Closing leaves standing water"), S->IsPlantFlooded());
    TestTrue(TEXT("Powered rack overload accepted"), Rack->Use());
    TestTrue(TEXT("Rack writes persistent smoke and overload"), S->IsServerRackOverloaded() && S->IsLabSmokeFilled());
    TestFalse(TEXT("Repeat rack overload has no new effect"), Rack->Use());
    TestEqual(TEXT("One ordinary rack edge"), Rack->Ordinary, 1);
    S->ResetToInitialState();
    TestFalse(TEXT("One reset clears coupled sabotage state"), S->IsPlantFlooded() || Valve->IsOpen() || Rack->IsOverloaded());
    TestTrue(TEXT("Reset reconciles actual lamp"), Lamp->IsLit());
    TestEqual(TEXT("Rack startup and reset are instant"), Rack->Instant, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSEGameTraversal, "GameEngineBench.UE0164.EndToEnd.CharacterLadderInputAndRelease", SSEGame::Flags)
bool FSSEGameTraversal::RunTest(const FString&)
{
    using namespace SSEGame; FWorld F; NEED(F.World); NEED(F.Facility());
    auto* Player = F.Spawn<ASSEGamePlayer>(FVector(45,0,96)); NEED(Player);
    auto* Ladder = F.Spawn<AFacilityLadder>(FVector::ZeroVector); NEED(Ladder);
    auto* Climb = Player->GetClimbComponent(); NEED(Climb);
    if (!TestTrue(TEXT("Real character attaches through real ladder"), Ladder->Use(Player))) return false;
    const float Before = Player->GetActorLocation().Z;
    Player->MoveAxes(99.f, 1.f);
    static_cast<UActorComponent*>(Climb)->TickComponent(.5f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Character routes forward to climb, ignores strafe"), FMath::IsNearlyEqual(Player->GetActorLocation().Z, Before + 75.f, .1f));
    const FVector Consumed = Player->GetActorLocation();
    static_cast<UActorComponent*>(Climb)->TickComponent(.5f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Climb consumes routed input once"), Player->GetActorLocation().Equals(Consumed, .1f));
    Player->JumpPressed();
    TestFalse(TEXT("Character jump detaches existing climb"), Climb->IsClimbing());
    TestEqual(TEXT("Detach restores movement mode"), Player->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Falling);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSEGameTunnel, "GameEngineBench.UE0164.EndToEnd.TunnelThresholdRechecksLiveFans", SSEGame::Flags)
bool FSSEGameTunnel::RunTest(const FString&)
{
    using namespace SSEGame; FWorld F; auto* S = F.Facility(); NEED(S);
    auto* Player = F.Spawn<ASSEGamePlayer>(FVector(0,0,96)); NEED(Player);
    auto* PC = F.Spawn<APlayerController>(); NEED(PC); PC->Possess(Player);
    auto* Door = F.Make<AGE152Door>(FVector(2500,0,0), [](auto* D) { D->Configure(TEXT("JourneyTunnel"), Circuit::EFC_None, EDoorKind::Standard); }); NEED(Door);
    auto* Fan = F.Make<AGE152Fan>(FVector(5000,0,0), [](auto* A) { A->Configure(TEXT("JourneyFans"), Circuit::EFC_Plant); }); NEED(Fan);
    auto* Exit = F.Make<ASSEGameExit>(FVector(3000,0,100), [](auto* E) { E->Configure(EFacilityExit::ServiceTunnel, TEXT("JourneyTunnel"), TEXT("JourneyFans")); }); NEED(Exit);
    S->SetCircuitState(Circuit::EFC_Security, ECircuitState::Cut);
    S->SetCircuitState(Circuit::EFC_Plant, ECircuitState::Live);
    S->GiveItem(EFacilityItem::ServiceKey);
    TestTrue(TEXT("Service key opens physical tunnel"), Door->Toggle());
    TestTrue(TEXT("Powered fan prevents escape"), Fan->IsRunning());
    Enter(Player, Exit->Box());
    TestFalse(TEXT("Player on threshold cannot cross running fans"), S->HasEscaped());
    S->SetCircuitState(Circuit::EFC_Plant, ECircuitState::Cut);
    TestFalse(TEXT("Fan observes actual power loss"), Fan->IsRunning());
    TestTrue(TEXT("Already-inside player is rechecked and escapes"), S->HasEscaped());
    TestEqual(TEXT("Correct distinct exit recorded"), S->GetEscapeExit(), EFacilityExit::ServiceTunnel);
    return true;
}

#undef NEED

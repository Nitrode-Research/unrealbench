// Hidden UE-0153 behavioral suite. No fixture or verifier enters the solver tree.
#include "InfiltrationFixtures.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/WorldInitializationValues.h"
#include "Facility/FacilitySettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

namespace SSE153
{
static constexpr auto Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// Configuration is changed only in memory, restored after world teardown; no config writes.
struct FWorld
{
    UFacilitySettings* Settings = GetMutableDefault<UFacilitySettings>();
    float OldStep = Settings->StepSeconds;
    TArray<FHackTargetConfig> OldHacks = Settings->HackTargets;
    TArray<FDoorLockConfig> OldLocks = Settings->DoorLocks;
    TArray<FPickupConfig> OldPickups = Settings->Pickups;
    UWorld* World = nullptr;
    FWorld()
    {
        Settings->StepSeconds = 0.f;
        Settings->HackTargets.Reset(); Settings->DoorLocks.Reset(); Settings->Pickups.Reset();
        FHackTargetConfig Camera; Camera.Id = TEXT("TestCamera"); Camera.Level = 1;
        FHackTargetConfig Controller; Controller.Id = TEXT("TestController"); Controller.Level = 2;
        Settings->HackTargets.Add(Camera); Settings->HackTargets.Add(Controller);
        FDoorLockConfig Gate; Gate.Id = TEXT("TestGate"); Gate.LockLevel = 3;
        Settings->DoorLocks.Add(Gate);
        if (!GEngine) return;
        FWorldInitializationValues Values;
        Values.SetDefaultGameMode(nullptr); Values.CreatePhysicsScene(true);
        const FName Name = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("SSE153"));
        World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage(), true,
            ERHIFeatureLevel::Num, &Values, true);
        if (!World) return;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitWorld(Values);
        World->InitializeActorsForPlay(FURL());
        World->BeginPlay();
        // No GameMode: BeginPlay starts world subsystems but not actors. Engine-supported dispatch.
        World->GetWorldSettings()->NotifyBeginPlay();
    }
    ~FWorld()
    {
        if (World)
        {
            World->EndPlay(EEndPlayReason::Quit);
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }
        Settings->StepSeconds = OldStep; Settings->HackTargets = OldHacks;
        Settings->DoorLocks = OldLocks; Settings->Pickups = OldPickups;
    }
    UFacilityStateSubsystem* Facility() const { return World ? World->GetSubsystem<UFacilityStateSubsystem>() : nullptr; }
    template<class T> T* Spawn(FVector Location = FVector::ZeroVector)
    {
        if (!World) return nullptr;
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<T>(T::StaticClass(), Location, FRotator::ZeroRotator, P);
    }
    template<class T, class Configure> T* Make(FVector Location, Configure Config)
    {
        if (!World) return nullptr;
        const FTransform Transform(FRotator::ZeroRotator, Location);
        T* Actor = World->SpawnActorDeferred<T>(T::StaticClass(), Transform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (Actor) { Config(Actor); Actor->FinishSpawning(Transform); }
        return Actor;
    }
    void Lights()
    {
        AActor* Marker = Spawn<AActor>(FVector(10000.f));
        Facility()->RegisterDevice(Marker, TEXT("TestLights"), EFacilityCircuit::EFC_Doors, true);
        Facility()->SetCircuitState(EFacilityCircuit::EFC_Doors, ECircuitState::Live);
        Facility()->SetCircuitState(EFacilityCircuit::EFC_Security, ECircuitState::Live);
    }
};

UBoxComponent* AddBox(AActor* Owner, USceneComponent* Parent, FVector Position, FVector Extent)
{
    UBoxComponent* Box = NewObject<UBoxComponent>(Owner);
    Owner->AddInstanceComponent(Box); Box->SetupAttachment(Parent);
    Box->SetRelativeLocation(Position); Box->InitBoxExtent(Extent);
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldDynamic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->SetGenerateOverlapEvents(true); Box->RegisterComponent();
    return Box;
}

UStaticMeshComponent* AddMesh(AActor* Owner)
{
    auto* Mesh = NewObject<UStaticMeshComponent>(Owner);
    Owner->AddInstanceComponent(Mesh); Mesh->SetupAttachment(Owner->GetRootComponent());
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->RegisterComponent();
    return Mesh;
}

UMaterialInterface* Material(UObject* Outer)
{
    return UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), Outer);
}

ASSE153Viewer* Viewer(FWorld& Scope)
{
    return Scope.Make<ASSE153Viewer>(FVector::ZeroVector, [](ASSE153Viewer* V)
    { V->Interactor->Configure(UMaterial::GetDefaultMaterial(MD_Surface)); });
}

ASSE153Character* Character(FWorld& Scope, FVector Position)
{
    auto* C = Scope.Spawn<ASSE153Character>(Position);
    if (C)
    {
        C->GetCharacterMovement()->SetComponentTickEnabled(false);
        C->Stealth->SetComponentTickEnabled(false);
    }
    return C;
}

// Guards fixture/setup failure before dereferencing; such failures are not counted as model success.
#define REQUIRE_OBJECT(Value) if (!TestNotNull(TEXT(#Value), Value)) return false
}

// REQUIRED: effective display updates, per-mesh overlay ownership, live availability, cleanup.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSE153Presentation,
    "GameEngineBench.UE0164.Subsystem153.01.PresentationLifecycle", SSE153::Flags)
bool FSSE153Presentation::RunTest(const FString&)
{
    using namespace SSE153;
    FWorld Scope; REQUIRE_OBJECT(Scope.Facility());
    auto* Ladder = Scope.Spawn<AFacilityLadder>(FVector(100,0,0)); REQUIRE_OBJECT(Ladder);
    auto* MeshA = AddMesh(Ladder); auto* MeshB = AddMesh(Ladder); auto* Excluded = AddMesh(Ladder);
    Excluded->ComponentTags.Add(TEXT("NoHighlight"));
    auto* PriorA = Material(Ladder); auto* PriorB = Material(Ladder); auto* PriorX = Material(Ladder);
    MeshA->SetOverlayMaterial(PriorA); MeshB->SetOverlayMaterial(PriorB); Excluded->SetOverlayMaterial(PriorX);
    auto* I = Ladder->GetInteractable();
    auto* Probe = NewObject<USSE153Probe>();
    I->OnDisplayDataChanged.AddDynamic(Probe, &USSE153Probe::Display);
    I->SetDisplayData(FText::FromString(TEXT("Ready")), true);
    I->SetDisplayData(FText::FromString(TEXT("Ready")), true);
    TestEqual(TEXT("Repeated identical presentation is silent"), Probe->Displays, 1);
    I->SetHackDisplayData(FText::FromString(TEXT("Override")), true);
    I->SetHackDisplayData(FText::FromString(TEXT("Override")), true);
    TestEqual(TEXT("Hack presentation is independently idempotent"), Probe->Displays, 2);
    auto* Overlay = Material(Ladder);
    I->SetHighlightOverlay(Overlay);
    TestTrue(TEXT("All owned eligible meshes highlighted"), MeshA->GetOverlayMaterial() == Overlay && MeshB->GetOverlayMaterial() == Overlay);
    TestTrue(TEXT("Opt-out preserved"), Excluded->GetOverlayMaterial() == PriorX);
    I->SetHighlightOverlay(Material(Ladder));
    I->SetHighlightOverlay(nullptr);
    TestTrue(TEXT("Exact distinct prior materials restored"), MeshA->GetOverlayMaterial() == PriorA && MeshB->GetOverlayMaterial() == PriorB);
    TestFalse(TEXT("Highlight state cleared"), I->IsHighlighted());
    AddBox(Ladder, Ladder->GetRootComponent(), FVector::ZeroVector, FVector(20));
    auto* V = Viewer(Scope); REQUIRE_OBJECT(V);
    V->Interactor->PerformSphereDetection();
    auto* AvailableOverlay = MeshA->GetOverlayMaterial();
    I->SetDisplayData(FText::FromString(TEXT("Blocked")), false);
    TestTrue(TEXT("Availability recolors without a scan"), MeshA->GetOverlayMaterial() != AvailableOverlay);
    V->Interactor->StopSphereDetection();
    TestTrue(TEXT("Stopping restores original materials"), MeshA->GetOverlayMaterial() == PriorA && MeshB->GetOverlayMaterial() == PriorB);
    return true;
}

// REQUIRED: camera-view targeting routes a real hack through the facility actor, blockers preserve proximity.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSE153Targeting,
    "GameEngineBench.UE0164.Subsystem153.02.FocusHackAndCleanup", SSE153::Flags)
bool FSSE153Targeting::RunTest(const FString&)
{
    using namespace SSE153;
    FWorld Scope; REQUIRE_OBJECT(Scope.Facility()); Scope.Lights();
    auto* Camera = Scope.Make<ASSE153Camera>(FVector(150,0,0), [](auto* C)
    { C->Configure(TEXT("TestCamera"), TEXT("TestLights")); C->Pan(0,0,0); }); REQUIRE_OBJECT(Camera);
    AddBox(Camera, Camera->GetRootComponent(), FVector::ZeroVector, FVector(20));
    auto* Side = Scope.Spawn<AFacilityLadder>(FVector(0,150,0)); REQUIRE_OBJECT(Side);
    AddBox(Side, Side->GetRootComponent(), FVector::ZeroVector, FVector(20));
    auto* V = Viewer(Scope); REQUIRE_OBJECT(V);
    auto* Probe = NewObject<USSE153Probe>();
    V->Interactor->StopSphereDetection();
    V->Interactor->OnInteractableDetected.AddDynamic(Probe, &USSE153Probe::Detect);
    V->Interactor->OnInteractionLost.AddDynamic(Probe, &USSE153Probe::Lose);
    V->Interactor->OnFocusedDisplayDataChanged.AddDynamic(Probe, &USSE153Probe::Display);
    V->Interactor->StartSphereDetection(); V->Interactor->StartSphereDetection();
    TestTrue(TEXT("Camera forward determines focus"), V->Interactor->GetFocusedInteractable() == Camera->GetInteractable());
    TestEqual(TEXT("Repeated starts do not redetect"), Probe->Detected, 1);
    TestTrue(TEXT("Off-axis interactable still highlighted"), Side->GetInteractable()->IsHighlighted());
    TestTrue(TEXT("Focused hack dispatch succeeds"), V->Interactor->Hack());
    TestTrue(TEXT("Real camera was looped through base hack binding"), Camera->IsLooped());
    TestFalse(TEXT("Looped camera no longer watches"), Camera->IsWatching());
    TestTrue(TEXT("Second dispatch safely reaches ineligible target"), V->Interactor->Hack());
    TestEqual(TEXT("Successful/repeated hacks are quiet"), Scope.Facility()->GetAlarmLevel(), EAlarmLevel::EAL_Quiet);
    auto* Blocker = Scope.Spawn<AActor>(FVector(70,0,0)); REQUIRE_OBJECT(Blocker);
    auto* Wall = AddBox(Blocker, nullptr, FVector::ZeroVector, FVector(10));
    Blocker->SetRootComponent(Wall); Blocker->SetActorLocation(FVector(70,0,0));
    V->Interactor->PerformSphereDetection();
    TestNull(TEXT("Opaque visibility blocker removes focus"), V->Interactor->GetFocusedInteractable());
    TestTrue(TEXT("Blocked camera remains highlighted in range"), Camera->GetInteractable()->IsHighlighted());
    TestFalse(TEXT("No focus means no hack"), V->Interactor->Hack());
    const int32 DisplaysAfterLoss = Probe->Displays;
    Camera->GetInteractable()->SetDisplayData(FText::FromString(TEXT("Out-of-focus status changed")), false);
    TestEqual(TEXT("Old target display updates do not reach focused HUD after loss"), Probe->Displays, DisplaysAfterLoss);
    Blocker->Destroy(); V->Interactor->PerformSphereDetection();
    TestTrue(TEXT("Removing blocker reacquires camera"), V->Interactor->GetFocusedInteractable() == Camera->GetInteractable());
    Camera->Destroy(); V->Interactor->PerformSphereDetection();
    TestNull(TEXT("Destroyed target removed"), V->Interactor->GetFocusedInteractable());
    TestFalse(TEXT("Destroyed focus not usable"), V->Interactor->Interact());
    const int32 Losses = Probe->Lost;
    V->Interactor->StopSphereDetection(); V->Interactor->StopSphereDetection();
    TestEqual(TEXT("Stop does not emit stale duplicate losses"), Probe->Lost, Losses);
    TestFalse(TEXT("Stop removes off-axis highlights"), Side->GetInteractable()->IsHighlighted());
    return true;
}

// REQUIRED: swept climb is blocked by the physical closed hatch, then succeeds after native lid motion.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSE153Traversal,
    "GameEngineBench.UE0164.Subsystem153.03.LadderHatchTraversal", SSE153::Flags)
bool FSSE153Traversal::RunTest(const FString&)
{
    using namespace SSE153;
    FWorld Scope; REQUIRE_OBJECT(Scope.Facility());
    auto* Ladder = Scope.Spawn<AFacilityLadder>(); REQUIRE_OBJECT(Ladder);
    auto* Hatch = Scope.Make<ASSE153Hatch>(FVector(-100,0,400), [](auto* H) { H->Configure(TEXT("TestHatch")); }); REQUIRE_OBJECT(Hatch);
    // Authored collision normally belongs to the Lid mesh. Equivalent native box follows Hinge.
    auto* Hinge = Cast<USceneComponent>(Hatch->GetDefaultSubobjectByName(TEXT("Hinge"))); REQUIRE_OBJECT(Hinge);
    AddBox(Hatch, Hinge, FVector(100,0,0), FVector(100,100,5));
    auto* C = Character(Scope, FVector(45,0,88)); REQUIRE_OBJECT(C);
    auto* Probe = NewObject<USSE153Probe>();
    C->Climb->OnClimbingChanged.AddDynamic(Probe, &USSE153Probe::Climbing);
    TestTrue(TEXT("Use starts climb"), Ladder->Use(C));
    TestEqual(TEXT("Flying while attached"), C->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Flying);
    const float Joined = C->GetActorLocation().Z;
    C->Climb->AddClimbInput(1); C->Climb->AddClimbInput(1); C->Climb->Advance(0.5f);
    TestTrue(TEXT("Accumulated input clamps to one"), FMath::IsNearlyEqual(C->GetActorLocation().Z, Joined + 75.f, 0.1f));
    const FVector Consumed = C->GetActorLocation(); C->Climb->Advance(0.5f);
    TestTrue(TEXT("Input consumed once"), C->GetActorLocation().Equals(Consumed, 0.1f));
    C->Climb->AddClimbInput(1); C->Climb->Advance(1.f);
    C->Climb->AddClimbInput(1); C->Climb->Advance(0.5f);
    TestTrue(TEXT("Closed hatch collision stops upward sweep"), C->GetActorLocation().Z < 350.f);
    TestTrue(TEXT("Blocked character remains attached"), C->Climb->IsClimbing());
    TestTrue(TEXT("Mechanical hatch can open"), Hatch->CanOpen());
    TestTrue(TEXT("Toggle delegates to facility"), Hatch->Toggle());
    TestTrue(TEXT("Logical open before settled"), Hatch->IsOpen() && Hatch->IsMoving());
    // Real frame cadence: QInterpConstantTo caps each call to one radian, even
    // when a large dt would imply more travel. Simulate the same one second.
    for (int32 Frame = 0; Frame < 60; ++Frame) Hatch->Advance(1.f / 60.f);
    TestFalse(TEXT("Hatch settled"), Hatch->IsMoving()); TestEqual(TEXT("Single settlement"), Hatch->Settles, 1);
    C->Climb->AddClimbInput(1); C->Climb->Advance(4.f);
    TestFalse(TEXT("Opened hatch permits top exit"), C->Climb->IsClimbing());
    TestTrue(TEXT("Exit crosses to far side"), C->GetActorLocation().X < 0.f);
    TestEqual(TEXT("Exit restores falling"), C->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Falling);
    TestEqual(TEXT("One start and one stop"), Probe->Climbs, 2);
    // The climb entry and up-sweep remain valid; only the across path is blocked.
    // This distinguishes a real top-exit sweep from a teleport through a wall.
    // Independent actors keep prior detach/listener defects from contaminating
    // this collision scenario; replacement/reuse behavior is tested in group 04.
    auto* ExitLadder = Scope.Spawn<AFacilityLadder>(FVector(1000,0,0)); REQUIRE_OBJECT(ExitLadder);
    auto* ExitC = Character(Scope, FVector(1045,0,488)); REQUIRE_OBJECT(ExitC);
    auto* AcrossWall = Scope.Spawn<AActor>(); REQUIRE_OBJECT(AcrossWall);
    auto* AcrossBox = AddBox(AcrossWall, nullptr, FVector::ZeroVector, FVector(5,100,120));
    AcrossWall->SetRootComponent(AcrossBox);
    AcrossWall->SetActorLocation(FVector(970,0,488));
    TestTrue(TEXT("Attach at top before blocked across exit"), ExitLadder->Use(ExitC));
    const FVector TopCapsule = ExitLadder->GetTopLocation() + ExitLadder->GetFacing() * ExitLadder->GetStandOff()
        + FVector::UpVector * ExitC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    TestTrue(TEXT("Blocked-exit fixture starts at top"), ExitC->GetActorLocation().Equals(TopCapsule, 0.1f));
    ExitC->Climb->AddClimbInput(1); ExitC->Climb->Advance(0.1f);
    TestTrue(TEXT("Across obstruction retains attachment"), ExitC->Climb->IsClimbing());
    TestTrue(TEXT("Across obstruction restores exact top capsule pose"), ExitC->GetActorLocation().Equals(TopCapsule, 0.1f));
    AcrossWall->Destroy();
    ExitC->Climb->AddClimbInput(1); ExitC->Climb->Advance(0.1f);
    TestFalse(TEXT("Removing across obstruction permits exit"), ExitC->Climb->IsClimbing());
    Scope.Facility()->ResetToInitialState();
    TestFalse(TEXT("Reset closes hatch silently"), Hatch->IsOpen() || Hatch->IsMoving());
    TestEqual(TEXT("Reset did not settle"), Hatch->Settles, 1);
    TestTrue(TEXT("Reset restores authored pose"), Hatch->Pose().Equals(FQuat::Identity));
    TestTrue(TEXT("Begin fresh hatch swing for interruption"), Hatch->Toggle());
    for (int32 Frame = 0; Frame < 6; ++Frame) Hatch->Advance(1.f / 60.f);
    const FQuat PartialPose = Hatch->Pose();
    TestTrue(TEXT("Interruption fixture is between closed and open endpoints"), Hatch->IsMoving()
        && PartialPose.AngularDistance(FQuat::Identity) > 0.01f
        && PartialPose.AngularDistance(FQuat::Identity) < FMath::DegreesToRadians(100.f));
    TestTrue(TEXT("Reverse hatch before it reaches open endpoint"), Hatch->Toggle());
    TestTrue(TEXT("Reversing hatch preserves its current intermediate pose"), Hatch->Pose().Equals(PartialPose));
    for (int32 Frame = 0; Frame < 60; ++Frame) Hatch->Advance(1.f / 60.f);
    TestFalse(TEXT("Reversed hatch finishes closed and idle"), Hatch->IsOpen() || Hatch->IsMoving());
    TestTrue(TEXT("Reversed hatch returns to authored closed pose"), Hatch->Pose().Equals(FQuat::Identity));
    TestEqual(TEXT("Interrupted opening produces only the final closing settlement"), Hatch->Settles, 2);
    return true;
}

// REQUIRED: ladder tracking is last-user tracking, not invented exclusive occupancy; destruction cleanup.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSE153ClimbLifetime,
    "GameEngineBench.UE0164.Subsystem153.04.ClimberReplacementAndLifetime", SSE153::Flags)
bool FSSE153ClimbLifetime::RunTest(const FString&)
{
    using namespace SSE153;
    FWorld Scope; REQUIRE_OBJECT(Scope.Facility());
    auto* Ladder = Scope.Spawn<AFacilityLadder>(); auto* Other = Scope.Spawn<AFacilityLadder>(FVector(1000,0,0));
    auto* A = Character(Scope, FVector(45,0,88)); auto* B = Character(Scope, FVector(45,0,288));
    REQUIRE_OBJECT(Ladder); REQUIRE_OBJECT(Other); REQUIRE_OBJECT(A); REQUIRE_OBJECT(B);
    TestFalse(TEXT("Null user rejected"), Ladder->Use(nullptr));
    TestTrue(TEXT("First user attached"), Ladder->Use(A));
    TestFalse(TEXT("Other ladder cannot detach user"), Other->Use(A));
    TestTrue(TEXT("Second user can replace tracking"), Ladder->Use(B));
    TestTrue(TEXT("First user remains climbing"), A->Climb->IsClimbing());
    A->Climb->StopClimbing(); A->Climb->StopClimbing();
    // Stop this scenario after a proven tracking failure: reusing that invalid
    // lifecycle state can otherwise generate a secondary duplicate-delegate ensure.
    // The assertion remains the failure; all later checks run on the healthy path.
    if (!TestEqual(TEXT("Old user's end does not clear new user's prompt"), Ladder->GetInteractable()->GetDisplayData().InteractionDisplayText.ToString(), FString(TEXT("Let go"))))
        return false;
    TestTrue(TEXT("Same-ladder use lets tracked user go"), Ladder->Use(B));
    TestFalse(TEXT("Tracked user detached"), B->Climb->IsClimbing());
    TestEqual(TEXT("Prompt restored"), Ladder->GetInteractable()->GetDisplayData().InteractionDisplayText.ToString(), FString(TEXT("Climb")));
    TestTrue(TEXT("New climb can start after stop"), Ladder->Use(B));
    TestTrue(TEXT("Untracked direct climb can coexist"), A->Climb->StartClimbing(Ladder));
    Ladder->Destroy();
    TestFalse(TEXT("EndPlay immediately releases tracked climber"), B->Climb->IsClimbing());
    A->Climb->Advance(0.01f);
    TestFalse(TEXT("Invalid ladder cleaned on next tick"), A->Climb->IsClimbing());
    TestNull(TEXT("No stale ladder pointer"), A->Climb->GetLadder());
    TestFalse(TEXT("Inactive climb does not tick"), A->Climb->IsComponentTickEnabled());
    return true;
}

// REQUIRED: live stealth priority, actual volume configuration, deduplication, weak lifetime, reentrant transitions.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSE153Stealth,
    "GameEngineBench.UE0164.Subsystem153.05.StealthLightingAndOverlapLifetime", SSE153::Flags)
bool FSSE153Stealth::RunTest(const FString&)
{
    using namespace SSE153;
    FWorld Scope; REQUIRE_OBJECT(Scope.Facility()); Scope.Lights();
    auto* Zone = Scope.Make<ASSE153LightZone>(FVector(5000,0,0), [](auto* Z) { Z->Configure(TEXT("TestLights")); }); REQUIRE_OBJECT(Zone);
    auto* Spot = Scope.Spawn<ASSE153HidingSpot>(FVector(6000,0,0)); REQUIRE_OBJECT(Spot);
    TestEqual(TEXT("Light volume overlaps Pawn"), Zone->Box()->GetCollisionResponseToChannel(ECC_Pawn), ECR_Overlap);
    TestEqual(TEXT("Hiding volume ignores visibility"), Spot->Box()->GetCollisionResponseToChannel(ECC_Visibility), ECR_Ignore);
    TestTrue(TEXT("Light volume dimensions"), Zone->Box()->GetUnscaledBoxExtent().Equals(FVector(500,500,200)));
    TestTrue(TEXT("Hiding volume dimensions"), Spot->Box()->GetUnscaledBoxExtent().Equals(FVector(50,50,100)));
    auto* C = Character(Scope, FVector(0,0,88)); REQUIRE_OBJECT(C);
    auto* Probe = NewObject<USSE153Probe>(); Probe->Stealth = C->Stealth;
    C->Stealth->OnStealthStateChanged.AddDynamic(Probe, &USSE153Probe::StealthChanged);
    TestEqual(TEXT("No zones is dark"), C->Stealth->GetStealthState(), EStealthState::Dark);
    C->GetCharacterMovement()->Velocity = FVector(251,0,1000);
    // Drive documented owner delegates directly to isolate membership from physics timing.
    C->OnActorBeginOverlap.Broadcast(C, Zone); C->OnActorBeginOverlap.Broadcast(C, Zone);
    TestEqual(TEXT("Lit, upright, fast player exposed"), C->Stealth->GetStealthState(), EStealthState::Exposed);
    TestEqual(TEXT("Reentrant listener sees recorded baseline"), Probe->StealthChanges, 1);
    TestEqual(TEXT("Zone id from live lit overlap"), C->Stealth->GetLightZoneId(), FName(TEXT("TestLights")));
    C->GetCharacterMovement()->Velocity = FVector(250,0,1000);
    TestTrue(TEXT("Inclusive horizontal speed threshold"), C->Stealth->IsSneaking());
    C->Stealth->Advance(); C->Stealth->Advance();
    TestEqual(TEXT("Unchanged ticks suppressed"), Probe->StealthChanges, 2);
    C->OnActorBeginOverlap.Broadcast(C, Spot);
    TestEqual(TEXT("Cover outranks lighting and speed"), C->Stealth->GetStealthState(), EStealthState::Hidden);
    Spot->RequireCrouch(true);
    TestFalse(TEXT("Standing fails crouched cover"), C->Stealth->IsHidden());
    C->bIsCrouched = true; TestTrue(TEXT("Crouched cover succeeds immediately"), C->Stealth->IsHidden());
    C->bIsCrouched = false;
    Scope.Facility()->SetCircuitState(EFacilityCircuit::EFC_Doors, ECircuitState::Cut);
    TestEqual(TEXT("Power changes live without stealth tick"), C->Stealth->GetStealthState(), EStealthState::Dark);
    TestTrue(TEXT("Backup power fixture starts supplied generator"), Scope.Facility()->StartGenerator());
    TestFalse(TEXT("Backup power does not change the cut breaker"), Scope.Facility()->IsBreakerOn(EFacilityCircuit::EFC_Doors));
    TestTrue(TEXT("Generator-backed light zone is live immediately"), Zone->IsLit());
    TestEqual(TEXT("Stealth uses effective power before a tick"), C->Stealth->GetStealthState(), EStealthState::Sneaking);
    Scope.Facility()->SetCircuitState(EFacilityCircuit::EFC_Doors, ECircuitState::Live);
    C->OnActorEndOverlap.Broadcast(C, Zone);
    TestTrue(TEXT("One end removes duplicate begins"), C->Stealth->IsInDarkness());
    C->OnActorBeginOverlap.Broadcast(C, Zone); Zone->Destroy();
    TestTrue(TEXT("Destroyed light ignored before next tick"), C->Stealth->IsInDarkness());
    Spot->RequireCrouch(false); Spot->Destroy();
    TestFalse(TEXT("Destroyed cover ignored"), C->Stealth->IsHidden());
    TestEqual(TEXT("Description fallback"), UStealthComponent::DescribeStealthState(static_cast<EStealthState>(255)).ToString(), FString(TEXT("Exposed")));
    return true;
}

// REQUIRED: real overlap sightings, power shared with stealth, edge suppression, pan and reset.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSE153Camera,
    "GameEngineBench.UE0164.Subsystem153.06.CameraSightingsAndPower", SSE153::Flags)
bool FSSE153Camera::RunTest(const FString&)
{
    using namespace SSE153;
    FWorld Scope; REQUIRE_OBJECT(Scope.Facility()); Scope.Lights();
    auto* Camera = Scope.Make<ASSE153Camera>(FVector::ZeroVector, [](auto* C)
    { C->Configure(TEXT("TestCamera"), TEXT("TestLights")); C->Pan(30,-30,20); }); REQUIRE_OBJECT(Camera);
    TestTrue(TEXT("Initial camera watching"), Camera->IsWatching());
    TestEqual(TEXT("Initial instant hook"), Camera->InstantEdges, 1);
    Camera->Advance(1); TestTrue(TEXT("Reversed limits move toward lower end"), FMath::IsNearlyEqual(Camera->GetPanYaw(), 10.f));
    Camera->Advance(10); TestTrue(TEXT("Overshoot clamps"), FMath::IsNearlyEqual(Camera->GetPanYaw(), -30.f));
    Camera->Advance(0.5f); TestTrue(TEXT("Next tick reverses"), FMath::IsNearlyEqual(Camera->GetPanYaw(), -20.f));
    Scope.Facility()->SetCircuitState(EFacilityCircuit::EFC_Security, ECircuitState::Cut);
    Camera->Advance(1); TestTrue(TEXT("Power loss freezes"), FMath::IsNearlyEqual(Camera->GetPanYaw(), -20.f));
    Scope.Facility()->SetCircuitState(EFacilityCircuit::EFC_Security, ECircuitState::Live);
    // Disable motor so overlap edges in this test come from pawn movement only.
    Camera->Pan(-20,-20,0); Camera->Advance(0.01f);
    auto* Pawn = Character(Scope, FVector(4000,0,88)); REQUIRE_OBJECT(Pawn);
    auto* Controller = Scope.Spawn<APlayerController>(); REQUIRE_OBJECT(Controller);
    Controller->Possess(Pawn);
    Pawn->SetActorLocation(Camera->Box()->GetComponentLocation(), false, nullptr, ETeleportType::TeleportPhysics);
    Camera->Box()->UpdateOverlaps(); Pawn->GetCapsuleComponent()->UpdateOverlaps();
    TestTrue(TEXT("Fixture is actually in camera volume"), Camera->Box()->IsOverlappingActor(Pawn));
    TestEqual(TEXT("Player entry raises once"), Scope.Facility()->GetAlarmLevel(), EAlarmLevel::EAL_Alerted);
    const int32 Edges = Camera->WatchingEdges;
    Camera->Box()->UpdateOverlaps();
    TestEqual(TEXT("Stationary overlap is not another sighting"), Scope.Facility()->GetAlarmLevel(), EAlarmLevel::EAL_Alerted);
    TestEqual(TEXT("Nested alarm notification does not repeat watch hook"), Camera->WatchingEdges, Edges);
    Scope.Facility()->SetCircuitState(EFacilityCircuit::EFC_Doors, ECircuitState::Cut);
    Scope.Facility()->ResetAlarmLevel(); Camera->Reenter = true;
    Scope.Facility()->SetCircuitState(EFacilityCircuit::EFC_Doors, ECircuitState::Live);
    TestEqual(TEXT("Watch callback precedes sighting"), Camera->AlarmAtWatch, EAlarmLevel::EAL_Quiet);
    TestEqual(TEXT("Restored lights report already-inside player once"), Scope.Facility()->GetAlarmLevel(), EAlarmLevel::EAL_Alerted);
    Scope.Facility()->ResetToInitialState();
    TestEqual(TEXT("Reset does not report player inside"), Scope.Facility()->GetAlarmLevel(), EAlarmLevel::EAL_Quiet);
    TestEqual(TEXT("Reset always instant callback"), Camera->InstantEdges, 2);
    Camera->Destroy();
    Pawn->SetActorLocation(FVector(4000,0,88));
    TestEqual(TEXT("Destroyed watcher produces no alarm"), Scope.Facility()->GetAlarmLevel(), EAlarmLevel::EAL_Quiet);
    return true;
}

// REQUIRED: controller failure is a state-change result, upgraded success opens gate; powered console steps alarm.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSE153Overrides,
    "GameEngineBench.UE0164.Subsystem153.07.GateOverrideAndAlarmRecovery", SSE153::Flags)
bool FSSE153Overrides::RunTest(const FString&)
{
    using namespace SSE153;
    FWorld Scope; REQUIRE_OBJECT(Scope.Facility()); Scope.Lights();
    auto* Marker = Scope.Spawn<AActor>(); REQUIRE_OBJECT(Marker);
    Scope.Facility()->RegisterDevice(Marker, TEXT("TestGate"), EFacilityCircuit::EFC_Doors, false);
    Scope.Facility()->RegisterDoor(TEXT("TestGate"), EDoorKind::Gate, false);
    auto* Controller = Scope.Make<ASSE153GateController>(FVector(1000), [](auto* A) { A->Configure(TEXT("TestController")); }); REQUIRE_OBJECT(Controller);
    auto* Console = Scope.Make<ASSE153Console>(FVector(2000), [](auto* A) { A->Configure(TEXT("TestConsole")); }); REQUIRE_OBJECT(Console);
    TestTrue(TEXT("Underleveled attempts are eligible"), Controller->CanHack());
    TestTrue(TEXT("Loud failure returns state changed"), Controller->Hack());
    TestFalse(TEXT("Underleveled hack did not open gate"), Scope.Facility()->IsDoorOpen(TEXT("TestGate")));
    TestEqual(TEXT("Failed attempt raises alarm"), Scope.Facility()->GetAlarmLevel(), EAlarmLevel::EAL_Alerted);
    TestTrue(TEXT("Powered console can silence"), Console->CanSilence());
    TestTrue(TEXT("Silence lowers exactly one"), Console->Silence());
    TestEqual(TEXT("Alarm recovered to quiet"), Scope.Facility()->GetAlarmLevel(), EAlarmLevel::EAL_Quiet);
    TestFalse(TEXT("Quiet console refuses repeat"), Console->Silence());
    Scope.Facility()->GiveItem(EFacilityItem::ServerDrive);
    TestTrue(TEXT("Upgrade opens locked gate"), Controller->Hack());
    TestTrue(TEXT("Gate state changed"), Scope.Facility()->IsDoorOpen(TEXT("TestGate")));
    TestFalse(TEXT("Open gate not hackable"), Controller->CanHack());
    TestFalse(TEXT("Gate display updated"), Controller->GetInteractable()->GetDisplayData().InteractionDisplayText.IsEmpty());
    TestFalse(TEXT("Open gate is non-actionable"), Controller->GetInteractable()->GetDisplayData().bCanInteract);
    Scope.Facility()->SetAlarmLevel(EAlarmLevel::EAL_Lockdown);
    Scope.Facility()->SetCircuitState(EFacilityCircuit::EFC_Security, ECircuitState::Cut);
    TestFalse(TEXT("Unpowered console refuses"), Console->Silence());
    TestEqual(TEXT("Cut power does not silence itself"), Scope.Facility()->GetAlarmLevel(), EAlarmLevel::EAL_Lockdown);
    Scope.Facility()->SetCircuitState(EFacilityCircuit::EFC_Security, ECircuitState::Live);
    TestTrue(TEXT("Restored console lowers one level"), Console->Silence());
    TestEqual(TEXT("Lockdown becomes Alerted, not Quiet"), Scope.Facility()->GetAlarmLevel(), EAlarmLevel::EAL_Alerted);
    return true;
}

// REQUIRED: quiet transport differs from a drop; drop edge damages once despite nested state broadcasts; reset snaps.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSE153Crate,
    "GameEngineBench.UE0164.Subsystem153.08.CrateTransportDropAndReset", SSE153::Flags)
bool FSSE153Crate::RunTest(const FString&)
{
    using namespace SSE153;
    FWorld Scope; REQUIRE_OBJECT(Scope.Facility());
    auto* LiftMarker = Scope.Spawn<AActor>(); REQUIRE_OBJECT(LiftMarker);
    Scope.Facility()->RegisterDevice(LiftMarker, TEXT("TestLift"), EFacilityCircuit::EFC_Plant, false);
    FLiftState Lift; Lift.Stop = ELiftStop::ELS_Top;
    Scope.Facility()->RegisterLift(TEXT("TestLift"), Lift);
    Scope.Facility()->SetCircuitState(EFacilityCircuit::EFC_Plant, ECircuitState::Live);
    auto* Crate = Scope.Make<ASSE153Crate>(FVector::ZeroVector, [](auto* C)
    { C->Configure(TEXT("TestCrate"), TEXT("TestLift")); C->Head()->SetRelativeLocation(FVector(200,0,0)); C->Bottom()->SetRelativeLocation(FVector(200,0,-500)); }); REQUIRE_OBJECT(Crate);
    TestEqual(TEXT("Startup instant hook"), Crate->InstantEvents, 1);
    TestTrue(TEXT("Powered lift enables crate move"), Crate->Use());
    TestEqual(TEXT("Logical head position precedes body"), Crate->GetPosition(), ECratePosition::AtShaftHead);
    Crate->Advance(0.25f);
    TestTrue(TEXT("Constant push speed"), Crate->Mesh()->GetComponentLocation().Equals(FVector(50,0,0), 0.1f));
    TestFalse(TEXT("Cannot drop onto top car"), Crate->CanDrop());
    TestTrue(TEXT("Lift call transports crate quietly"), Scope.Facility()->CallLift(TEXT("TestLift"), ELiftStop::ELS_Bottom));
    TestEqual(TEXT("Quiet transport arrives in plant"), Crate->GetPosition(), ECratePosition::InPlant);
    TestFalse(TEXT("Transport is not dropped"), Crate->WasDropped());
    TestEqual(TEXT("Transport is quiet"), Scope.Facility()->GetAlarmLevel(), EAlarmLevel::EAL_Quiet);
    Scope.Facility()->ResetToInitialState();
    TestFalse(TEXT("Reset cancels moving body"), Crate->IsMoving());
    TestTrue(TEXT("Reset returns body to root"), Crate->Mesh()->GetComponentLocation().Equals(FVector::ZeroVector));
    Scope.Facility()->SetCircuitState(EFacilityCircuit::EFC_Plant, ECircuitState::Live);
    Scope.Facility()->CallLift(TEXT("TestLift"), ELiftStop::ELS_Bottom);
    Scope.Facility()->SetCircuitState(EFacilityCircuit::EFC_Plant, ECircuitState::Cut);
    TestFalse(TEXT("No pry or usable lift means blocked"), Crate->Move());
    Scope.Facility()->GiveItem(EFacilityItem::PryBar);
    TestTrue(TEXT("Pry moves crate with lift unpowered"), Crate->Move());
    TestTrue(TEXT("Bottom car permits drop without power"), Crate->CanDrop());
    auto* Victim = Character(Scope, Crate->Bottom()->GetComponentLocation()); REQUIRE_OBJECT(Victim);
    Victim->GetCapsuleComponent()->UpdateOverlaps(); Crate->Box()->UpdateOverlaps();
    TestTrue(TEXT("Victim is really in landing volume"), Crate->Box()->IsOverlappingActor(Victim));
    Crate->Reenter = true;
    const int32 Before = Crate->PositionEvents;
    TestTrue(TEXT("Use drops from head"), Crate->Use());
    TestTrue(TEXT("Logical dropped flag"), Crate->WasDropped());
    TestEqual(TEXT("Exactly one damage before landing"), Victim->DamageCalls, 1);
    TestTrue(TEXT("Damage causer is crate, instigator absent"), Victim->LastCauser == Crate && Victim->LastInstigator == nullptr);
    TestTrue(TEXT("Configured lethal amount"), FMath::IsNearlyEqual(Victim->LastDamage, 100000.f));
    TestEqual(TEXT("Nested broadcast does not repeat position hook"), Crate->PositionEvents, Before + 1);
    TestFalse(TEXT("Repeated drop rejected"), Crate->Drop());
    Scope.Facility()->GiveItem(EFacilityItem::Keycard2);
    TestEqual(TEXT("Unrelated broadcasts do not redamage"), Victim->DamageCalls, 1);
    Crate->Advance(2.f);
    TestFalse(TEXT("Body settles and disables motion"), Crate->IsMoving() || Crate->IsActorTickEnabled());
    Scope.Facility()->ResetToInitialState();
    TestEqual(TEXT("Reset never starts damage sweep"), Victim->DamageCalls, 1);
    TestFalse(TEXT("Reset clears dropped state"), Crate->WasDropped());
    TestEqual(TEXT("Reset restores quiet alarm"), Scope.Facility()->GetAlarmLevel(), EAlarmLevel::EAL_Quiet);
    return true;
}

#undef REQUIRE_OBJECT

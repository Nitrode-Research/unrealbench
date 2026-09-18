#include "FacilityOperationsFixtures.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/WorldInitializationValues.h"
#include "Facility/FacilitySettings.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "InteractionSystem/InteractableComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

namespace GE152
{
constexpr auto Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
constexpr auto Doors = EFacilityCircuit::EFC_Doors;
constexpr auto Security = EFacilityCircuit::EFC_Security;
constexpr auto Plant = EFacilityCircuit::EFC_Plant;
constexpr auto None = EFacilityCircuit::EFC_None;
constexpr auto Bottom = ELiftStop::ELS_Bottom;
constexpr auto Top = ELiftStop::ELS_Top;

FString Edge(const TCHAR* Name, bool Value, bool Instant)
{
    return FString::Printf(TEXT("%s:%d:%d"), Name, Value, Instant);
}

// Read reflected event parameters by property name rather than assuming generated layout.
bool BoolParam(UFunction* Function, void* Params, const TCHAR* Name)
{
    const FBoolProperty* Property = FindFProperty<FBoolProperty>(Function, Name);
    return Property && Params && Property->GetPropertyValue_InContainer(Params);
}

void CaptureEdge(TArray<FString>& Events, UFunction* Function, void* Params,
    const TCHAR* FunctionName, const TCHAR* Label, const TCHAR* ValueName)
{
    if (Function->GetFName() == FName(FunctionName))
        Events.Add(Edge(Label, BoolParam(Function, Params, ValueName), BoolParam(Function, Params, TEXT("bInstant"))));
}

struct FSettingsScope
{
    UFacilitySettings* Settings = GetMutableDefault<UFacilitySettings>();
    float Step = Settings->StepSeconds;
    int32 Decay = Settings->AlarmDecaySteps;
    int32 Duration = Settings->GeneratorRunSteps;
    int32 Short = Settings->FloodShortSteps;
    int32 Corrosion = Settings->CorrosionSteps;
    int32 Handheld = Settings->HandheldLevel;
    int32 DriveLevel = Settings->ServerDriveHandheldLevel;
    TArray<FDoorLockConfig> Locks = Settings->DoorLocks;
    TArray<FPickupConfig> Items = Settings->Pickups;
    TArray<FHackTargetConfig> Hacks = Settings->HackTargets;

    explicit FSettingsScope(float Interval)
    {
        Settings->StepSeconds = Interval;
        Settings->AlarmDecaySteps = 3;
        Settings->GeneratorRunSteps = 4;
        Settings->FloodShortSteps = 2;
        Settings->CorrosionSteps = 3;
        Settings->HandheldLevel = 1;
        Settings->ServerDriveHandheldLevel = 2;
        Settings->DoorLocks.Reset(); Settings->Pickups.Reset(); Settings->HackTargets.Reset();
        auto Lock = [&](FName Id, int32 Level, EFacilityItem Key) {
            FDoorLockConfig Entry; Entry.Id = Id; Entry.LockLevel = Level; Entry.KeyItem = Key;
            Settings->DoorLocks.Add(Entry);
        };
        Lock(TEXT("Locked"), 2, EFacilityItem::ServiceKey);
        Lock(TEXT("Tunnel"), 0, EFacilityItem::ServiceKey);
        for (const auto& Pair : TArray<TPair<FName, EFacilityItem>>{
            {TEXT("Card"), EFacilityItem::Keycard2}, {TEXT("Pry"), EFacilityItem::PryBar},
            {TEXT("Drive"), EFacilityItem::ServerDrive}})
        {
            FPickupConfig Entry; Entry.Id = Pair.Key; Entry.Item = Pair.Value; Settings->Pickups.Add(Entry);
        }
        for (const auto& Pair : TArray<TPair<FName, int32>>{
            {TEXT("Locked"), 2}, {TEXT("Elevator"), 1}, {TEXT("Routing"), 2}})
        {
            FHackTargetConfig Entry; Entry.Id = Pair.Key; Entry.Level = Pair.Value; Settings->HackTargets.Add(Entry);
        }
    }
    ~FSettingsScope()
    {
        Settings->StepSeconds = Step; Settings->AlarmDecaySteps = Decay;
        Settings->GeneratorRunSteps = Duration; Settings->FloodShortSteps = Short;
        Settings->CorrosionSteps = Corrosion; Settings->HandheldLevel = Handheld;
        Settings->ServerDriveHandheldLevel = DriveLevel;
        Settings->DoorLocks = Locks; Settings->Pickups = Items; Settings->HackTargets = Hacks;
    }
};

struct FFixture
{
    FSettingsScope Settings;
    UWorld* World = nullptr;
    UFacilityStateSubsystem* Facility = nullptr;
    UGE152StateObserver* Observer = nullptr;
    int32 SpawnIndex = 0;
    explicit FFixture(float Interval = 0.f) : Settings(Interval)
    {
        if (!GEngine) return;
        FWorldInitializationValues Values;
        Values.SetDefaultGameMode(nullptr).CreatePhysicsScene(true);
        World = UWorld::CreateWorld(EWorldType::Game, false,
            MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GE152")),
            GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values, true);
        if (!World) return;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitWorld(Values);
        World->InitializeActorsForPlay(FURL());
        World->BeginPlay();
        // No GameMode is installed: explicitly dispatch the engine's actor BeginPlay path.
        World->GetWorldSettings()->NotifyBeginPlay();
        Facility = World->GetSubsystem<UFacilityStateSubsystem>();
        if (!Facility) return;
        Observer = NewObject<UGE152StateObserver>(World);
        Observer->AddToRoot(); Observer->Facility = Facility;
        Facility->OnStateChanged.AddDynamic(Observer, &UGE152StateObserver::StateChanged);
        Facility->OnAlarmLevelChanged.AddDynamic(Observer, &UGE152StateObserver::AlarmChanged);
    }
    ~FFixture()
    {
        if (Observer)
        {
            Facility->OnStateChanged.RemoveDynamic(Observer, &UGE152StateObserver::StateChanged);
            Facility->OnAlarmLevelChanged.RemoveDynamic(Observer, &UGE152StateObserver::AlarmChanged);
            Observer->RemoveFromRoot();
        }
        if (World)
        {
            World->EndPlay(EEndPlayReason::Quit);
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }
    }
    template<class T, class Configure>
    T* Spawn(Configure Setup, FVector Position = FVector::ZeroVector)
    {
        // By default fixtures are separated; hazard tests explicitly move victims into volumes.
        if (Position.IsZero()) Position = FVector(++SpawnIndex * 2000.f, 0, 0);
        const FTransform Transform(Position);
        T* Actor = World->SpawnActorDeferred<T>(T::StaticClass(), Transform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (Actor) { Setup(Actor); Actor->FinishSpawning(Transform); }
        return Actor;
    }
    bool Ready(FAutomationTestBase& Test) const
    {
        return Test.TestNotNull(TEXT("Native Game world"), World)
            && Test.TestNotNull(TEXT("World subsystem initialized"), Facility)
            && Test.TestTrue(TEXT("Actor BeginPlay dispatch active"), World->HasBegunPlay());
    }
};

void Prompt(FAutomationTestBase& Test, UInteractableComponent* Interaction, const TCHAR* Text, bool Enabled)
{
    const auto Data = Interaction->GetDisplayData();
    Test.TestFalse(TEXT("Interaction supplies a status/action description"), Data.InteractionDisplayText.IsEmpty());
    Test.TestEqual(TEXT("Interaction enabled"), Data.bCanInteract, Enabled);
}
void Sequence(FAutomationTestBase& Test, const TArray<FString>& Actual, std::initializer_list<const TCHAR*> Expected)
{
    Test.TestEqual(TEXT("Notification count"), Actual.Num(), static_cast<int32>(Expected.size()));
    int32 I = 0;
    for (const TCHAR* Value : Expected)
    {
        if (Actual.IsValidIndex(I)) Test.TestEqual(FString::Printf(TEXT("Notification %d"), I), Actual[I], FString(Value));
        ++I;
    }
}
void Enter(UBoxComponent* Volume, AGE152Victim* Pawn)
{
    // Actual public overlap delegate, so bind/unbind and live damage gates are exercised.
    Volume->OnComponentBeginOverlap.Broadcast(Volume, Pawn, Pawn->Shape, 0, false, FHitResult());
}
}

void UGE152StateObserver::StateChanged()
{
    Events.Add(TEXT("state")); bLastStateWasReset = Facility->IsResetting(); bResetObserved |= bLastStateWasReset;
    if (bResetOnItem && Facility->HasItem(EFacilityItem::Keycard2))
    {
        bResetOnItem = false; Facility->ResetToInitialState();
    }
}
void UGE152StateObserver::AlarmChanged(EAlarmLevel Old, EAlarmLevel New)
{
    Events.Add(TEXT("alarm")); bLastAlarmWasReset = Facility->IsResetting(); bResetObserved |= bLastAlarmWasReset; LastOld = Old; LastNew = New;
}
void AGE152Door::Configure(FName Id, EFacilityCircuit Circuit, EDoorKind DoorKind, bool Open)
{
    DeviceId = Id; DefaultCircuit = Circuit; Kind = DoorKind; bInitiallyOpen = Open;
    DisplayName = FText::FromString(TEXT("door")); Panel->SetRelativeLocation(FVector(7, 11, 13));
}
void AGE152Door::OnPanelSettled(bool Open)
{
    bSettledWhileMoving |= IsMoving() || IsActorTickEnabled(); Events.Add(TEXT("native-settled"));
}
void AGE152Door::ProcessEvent(UFunction* Fn, void* Params)
{
    if (Fn->GetFName() == TEXT("ReceivePanelSettled")) Events.Add(TEXT("bp-settled"));
    Super::ProcessEvent(Fn, Params);
}
void AGE152Pickup::OnTakenChanged(bool Taken, bool Instant)
{
    Events.Add(GE152::Edge(TEXT("native-taken"), Taken, Instant));
    bVisibilityCorrectInHook &= IsHidden() == Taken && GetActorEnableCollision() != Taken;
    if (Taken && bNotifyUnrelatedOnTaken) { bNotifyUnrelatedOnTaken = false; FindFacility()->GiveItem(EFacilityItem::LockerItem); }
}
void AGE152Pickup::ProcessEvent(UFunction* Fn, void* Params)
{
    GE152::CaptureEdge(Events, Fn, Params, TEXT("ReceiveTakenChanged"), TEXT("bp-taken"), TEXT("bTaken"));
    Super::ProcessEvent(Fn, Params);
}
void AGE152Lift::Configure(FName Id, EFacilityCircuit Circuit, ELiftKind LiftKind, ELiftStop Initial)
{
    DeviceId = Id; DefaultCircuit = Circuit; Kind = LiftKind; InitialStop = Initial;
}
void AGE152Lift::OnCarArrived(ELiftStop Stop)
{
    bArrivedWhileMoving |= IsMoving() || IsActorTickEnabled(); Events.Add(TEXT("native-arrived"));
}
void AGE152Lift::ProcessEvent(UFunction* Fn, void* Params)
{
    if (Fn->GetFName() == TEXT("ReceiveCarArrived")) Events.Add(TEXT("bp-arrived"));
    Super::ProcessEvent(Fn, Params);
}
void AGE152Generator::OnRunningChanged(bool Running, bool Instant) { Events.Add(GE152::Edge(TEXT("native-running"), Running, Instant)); }
void AGE152Generator::OnOverloadedChanged(bool Overloaded, bool Instant) { Events.Add(GE152::Edge(TEXT("native-overload"), Overloaded, Instant)); }
void AGE152Generator::ProcessEvent(UFunction* Fn, void* Params)
{
    GE152::CaptureEdge(Events, Fn, Params, TEXT("ReceiveRunningChanged"), TEXT("bp-running"), TEXT("bRunning"));
    GE152::CaptureEdge(Events, Fn, Params, TEXT("ReceiveOverloadedChanged"), TEXT("bp-overload"), TEXT("bOverloaded"));
    Super::ProcessEvent(Fn, Params);
}
void AGE152Breaker::ProcessEvent(UFunction* Fn, void* Params)
{
    GE152::CaptureEdge(Events, Fn, Params, TEXT("ReceiveSwitchChanged"), TEXT("switch"), TEXT("bOn"));
    Super::ProcessEvent(Fn, Params);
}
void AGE152Routing::Configure(FName Panel, FName Target, EFacilityCircuit ToCircuit, bool Pump)
{
    DeviceId = Panel; Option.Action = Pump ? ERoutingAction::CoolantPump : ERoutingAction::RouteDevice;
    Option.DeviceId = Target; Option.Circuit = ToCircuit;
}
void AGE152Routing::ProcessEvent(UFunction* Fn, void* Params)
{
    GE152::CaptureEdge(Events, Fn, Params, TEXT("ReceiveSwitchChanged"), TEXT("switch"), TEXT("bOn"));
    Super::ProcessEvent(Fn, Params);
}
void AGE152Fan::Configure(FName Id, EFacilityCircuit Circuit, FName Lights)
{
    DeviceId = Id; DefaultCircuit = Circuit; RoomLightsId = Lights;
    Hub->SetRelativeRotation(FRotator(30, 15, 10)); KillDamage = 37.f;
}
void AGE152Fan::OnRunningChanged(bool Running, bool Instant) { Events.Add(GE152::Edge(TEXT("native-running"), Running, Instant)); }
void AGE152Fan::ProcessEvent(UFunction* Fn, void* Params)
{
    GE152::CaptureEdge(Events, Fn, Params, TEXT("ReceiveRunningChanged"), TEXT("bp-running"), TEXT("bRunning"));
    Super::ProcessEvent(Fn, Params);
}
void AGE152Flood::Configure(FName InFans)
{
    FansId = InFans; KillDamage = 53.f;
    Water->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Water->SetRelativeLocation(FVector(12, 18, 0)); Water->SetRelativeScale3D(FVector(2, 3, 1));
}
void AGE152Flood::ProcessEvent(UFunction* Fn, void* Params)
{
    GE152::CaptureEdge(Events, Fn, Params, TEXT("ReceiveFloodedChanged"), TEXT("flooded"), TEXT("bFlooded"));
    GE152::CaptureEdge(Events, Fn, Params, TEXT("ReceiveLethalChanged"), TEXT("lethal"), TEXT("bLethal"));
    if (Fn->GetFName() == TEXT("ReceiveSurfaceSettled"))
    {
        Events.Add(TEXT("settled")); bSettledWhileMoving |= IsSurfaceMoving() || IsActorTickEnabled();
    }
    Super::ProcessEvent(Fn, Params);
}
AGE152Victim::AGE152Victim()
{
    Shape = CreateDefaultSubobject<USphereComponent>(TEXT("Shape")); SetRootComponent(Shape);
    Shape->InitSphereRadius(5.f); Shape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Shape->SetCollisionObjectType(ECC_Pawn); Shape->SetCollisionResponseToAllChannels(ECR_Overlap);
    Shape->SetGenerateOverlapEvents(true); SetCanBeDamaged(true);
}
float AGE152Victim::TakeDamage(float Damage, const FDamageEvent& Event, AController* DamageInstigator, AActor* Causer)
{
    ++Hits; TotalDamage += Damage; LastCauser = Causer; LastInstigator = DamageInstigator;
    if (bResetOnHit) { bResetOnHit = false; GetWorld()->GetSubsystem<UFacilityStateSubsystem>()->ResetToInitialState(); }
    return Damage;
}

// REQUIRED: defaults, registration persistence, settings, and actor identity cleanup.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE152Registry, "GameEngineBench.UE0164.Subsystem152.RegistryAndPersistence", GE152::Flags)
bool FGE152Registry::RunTest(const FString& Parameters)
{
    using namespace GE152; FFixture F; if (!F.Ready(*this)) return false; auto* S = F.Facility;
    TestTrue(TEXT("DOORS starts live"), S->IsCircuitLive(Doors));
    TestTrue(TEXT("SECURITY starts live"), S->IsCircuitLive(Security));
    TestFalse(TEXT("PLANT starts cut"), S->IsCircuitLive(Plant));
    TestEqual(TEXT("Configured duration captured"), S->GetState().Config.GeneratorRunSteps, 4);
    TestEqual(TEXT("Configured decay captured"), S->GetAlarmDecaySteps(), 3);
    auto* A = F.Spawn<AGE152Fan>([](auto* X) { X->Configure(TEXT("Fans"), Plant); });
    auto* B = F.Spawn<AGE152Fan>([](auto* X) { X->Configure(TEXT("Fans"), Plant); });
    if (!TestNotNull(TEXT("First fan"), A) || !TestNotNull(TEXT("Second fan"), B)) return false;
    TestEqual(TEXT("First live shared actor"), S->FindDeviceActor(TEXT("Fans")), static_cast<AActor*>(A));
    TestEqual(TEXT("Registrations send no general events"), F.Observer->Events.Num(), 0);
    S->SetDeviceCircuit(TEXT("Fans"), Security);
    S->RegisterDevice(A, TEXT("Fans"), Plant, true);
    TestTrue(TEXT("Reregister preserves changed live routing"), S->GetDeviceCircuit(TEXT("Fans")) == Security);
    TestTrue(TEXT("Initial snapshot keeps home routing"), S->GetInitialState().DeviceCircuits.FindRef(TEXT("Fans")) == Plant);
    A->Destroy(); TestEqual(TEXT("Survivor discoverable"), S->FindDeviceActor(TEXT("Fans")), static_cast<AActor*>(B));
    B->Destroy(); TestNull(TEXT("Last actor removed from lookup"), S->FindDeviceActor(TEXT("Fans")));
    TestTrue(TEXT("State retained after actors end"), S->GetDeviceCircuit(TEXT("Fans")) == Security);
    auto* Door = F.Spawn<AGE152Door>([](auto* X) { X->Configure(TEXT("Locked"), Doors, EDoorKind::Standard); });
    if (!TestNotNull(TEXT("Door"), Door)) return false;
    TestEqual(TEXT("Settings seed lock"), Door->GetLockLevel(), 2);
    TestTrue(TEXT("Settings seed alternative key"), Door->GetKeyItem() == EFacilityItem::ServiceKey);
    S->GiveItem(EFacilityItem::Keycard2); TestTrue(TEXT("Open configured door"), Door->Use()); Door->Destroy();
    auto* Recreated = F.Spawn<AGE152Door>([](auto* X) { X->Configure(TEXT("Locked"), Doors, EDoorKind::Standard); });
    if (!TestNotNull(TEXT("Recreated door"), Recreated)) return false;
    TestTrue(TEXT("Recreated actor retains open state"), Recreated->IsOpen());
    TestTrue(TEXT("Recreated open pose snapped"), Recreated->Leaf()->GetRelativeLocation().Equals(FVector(7, 11, 213)));
    S->ResetToInitialState();
    TestFalse(TEXT("Registration snapshot restored closed"), Recreated->IsOpen());
    TestFalse(TEXT("Inventory reset"), S->HasItem(EFacilityItem::Keycard2));
    TestTrue(TEXT("Reset retains current actor registry"), S->FindDeviceActor(TEXT("Locked")) == Recreated);
    return true;
}

// REQUIRED: pickup -> keycard access -> continuous door movement and mechanical forcing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE152Access, "GameEngineBench.UE0164.Subsystem152.PickupAccessAndDoorMotion", GE152::Flags)
bool FGE152Access::RunTest(const FString& Parameters)
{
    using namespace GE152; FFixture F; if (!F.Ready(*this)) return false;
    auto* D = F.Spawn<AGE152Door>([](auto* X) { X->Configure(TEXT("Locked"), Doors, EDoorKind::Standard); });
    auto* P = F.Spawn<AGE152Pickup>([](auto* X) { X->Configure(TEXT("Card")); });
    if (!TestNotNull(TEXT("Door"), D) || !TestNotNull(TEXT("Pickup"), P)) return false;
    Prompt(*this, D->GetInteractable(), TEXT("Needs a level 2 keycard or the service key"), false);
    TestFalse(TEXT("Locked use refused"), D->Use());
    Prompt(*this, P->GetInteractable(), TEXT("Take level 2 keycard"), true);
    P->Events.Reset(); P->bNotifyUnrelatedOnTaken = true;
    TestTrue(TEXT("Take card"), P->Take()); TestFalse(TEXT("Repeated take no effect"), P->Take());
    Sequence(*this, P->Events, {TEXT("native-taken:1:0"), TEXT("bp-taken:1:0")});
    TestTrue(TEXT("Visibility reconciled before nested callback"), P->bVisibilityCorrectInHook);
    TestTrue(TEXT("Pickup grants access"), D->Use()); TestTrue(TEXT("Logical open immediate"), D->IsOpen());
    TestTrue(TEXT("Panel begins at authored pose"), D->Leaf()->GetRelativeLocation().Equals(FVector(7, 11, 13)));
    D->FrameStep(.25f); TestTrue(TEXT("Constant speed upward"), D->Leaf()->GetRelativeLocation().Equals(FVector(7, 11, 63), .01));
    F.Facility->GiveItem(EFacilityItem::Firearm); // unrelated notification must not restart movement
    D->FrameStep(.25f); TestTrue(TEXT("Unrelated notification preserves progress"), D->Leaf()->GetRelativeLocation().Equals(FVector(7, 11, 113), .01));
    TestTrue(TEXT("Reverse in flight"), D->Toggle()); D->FrameStep(.25f);
    TestTrue(TEXT("Reverse from present pose"), D->Leaf()->GetRelativeLocation().Equals(FVector(7, 11, 63), .01));
    D->FrameStep(1.f); D->FrameStep(1.f);
    Sequence(*this, D->Events, {TEXT("native-settled"), TEXT("bp-settled")});
    TestFalse(TEXT("Settled after movement disabled"), D->bSettledWhileMoving);
    F.Facility->TakeItem(EFacilityItem::Keycard2); F.Facility->GiveItem(EFacilityItem::PryBar);
    TestFalse(TEXT("Toggle does not force"), D->Toggle()); TestTrue(TEXT("Use selects pry"), D->Use());
    Prompt(*this, D->GetInteractable(), TEXT("Pried open"), false);
    TestTrue(TEXT("Pry raises alarm"), F.Facility->GetAlarmLevel() == EAlarmLevel::EAL_Alerted);
    D->Events.Reset(); F.Facility->ResetToInitialState();
    TestFalse(TEXT("Reset cancels door travel"), D->IsMoving()); TestEqual(TEXT("Reset emits no settled"), D->Events.Num(), 0);
    TestFalse(TEXT("Pickup restored in same actor"), P->IsTaken()); TestFalse(TEXT("Pickup visible again"), P->IsHidden());
    return true;
}

// REQUIRED: breaker overload, backup power versus switch position, inventory-driven sabotage.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE152Power, "GameEngineBench.UE0164.Subsystem152.BreakersGeneratorAndBlackout", GE152::Flags)
bool FGE152Power::RunTest(const FString& Parameters)
{
    using namespace GE152; FFixture F; if (!F.Ready(*this)) return false;
    auto* G = F.Spawn<AGE152Generator>([](auto*) {});
    auto* B = F.Spawn<AGE152Breaker>([](auto* X) { X->Configure(Plant); });
    auto* L = F.Spawn<AGE152Lift>([](auto* X) { X->Configure(TEXT("Cargo"), Plant, ELiftKind::CargoLift); });
    auto* Pry = F.Spawn<AGE152Pickup>([](auto* X) { X->Configure(TEXT("Pry")); });
    if (!G || !B || !L || !Pry) { AddError(TEXT("Actor spawn failed")); return false; }
    B->Interaction()->Interact(nullptr); // switching a third breaker trips all of them
    TestFalse(TEXT("Third breaker trips DOORS"), F.Facility->IsCircuitLive(Doors));
    TestFalse(TEXT("Third breaker trips SECURITY"), F.Facility->IsCircuitLive(Security));
    TestFalse(TEXT("Third breaker does not remain on"), B->IsOn());
    G->Events.Reset(); TestTrue(TEXT("Start generator through Use"), G->Use());
    TestTrue(TEXT("Cargo now usable"), L->CanUse()); TestFalse(TEXT("Breaker position remains off"), B->IsOn());
    TestTrue(TEXT("Backup makes cut PLANT live"), F.Facility->IsCircuitLive(Plant));
    Sequence(*this, G->Events, {TEXT("native-running:1:0"), TEXT("bp-running:1:0")});
    G->Events.Reset(); F.Facility->AdvanceStep();
    TestEqual(TEXT("Duration from configured four steps"), G->GetStepsLeft(), 3);
    TestEqual(TEXT("Countdown does not repeat running edge"), G->Events.Num(), 0);
    Prompt(*this, G->GetInteractable(), TEXT("Running, 3 steps left"), false);
    TestTrue(TEXT("Pry pickup taken"), Pry->Take());
    Prompt(*this, G->GetInteractable(), TEXT("Rig to overload (blackout)"), true);
    TestTrue(TEXT("Use chooses overload"), G->Use());
    Sequence(*this, G->Events, {TEXT("native-running:0:0"), TEXT("bp-running:0:0"), TEXT("native-overload:1:0"), TEXT("bp-overload:1:0")});
    TestTrue(TEXT("Overload sticky"), G->IsOverloaded()); TestFalse(TEXT("Overload refuses restart"), G->Start());
    TestFalse(TEXT("Blackout disables cargo calls"), L->CanUse());
    B->Interaction()->Interact(nullptr); TestTrue(TEXT("Breaker still repairable after overload"), B->IsOn());
    TestTrue(TEXT("Cargo restored by breaker"), L->CanUse());
    B->Destroy(); F.Facility->SetCircuitState(Plant, ECircuitState::Cut);
    TestFalse(TEXT("EndPlay removes interaction binding"), B->Interaction()->OnInteracted.IsBound());
    return true;
}

// REQUIRED: logical calls versus visual motion, elevator parking and cargo continuation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE152Lifts, "GameEngineBench.UE0164.Subsystem152.LiftCallsHackingAndParking", GE152::Flags)
bool FGE152Lifts::RunTest(const FString& Parameters)
{
    using namespace GE152; FFixture F; if (!F.Ready(*this)) return false;
    auto* E = F.Spawn<AGE152Lift>([](auto* X) { X->Configure(TEXT("Elevator"), Doors, ELiftKind::Elevator); });
    auto* C = F.Spawn<AGE152Lift>([](auto* X) { X->Configure(TEXT("Cargo"), Security, ELiftKind::CargoLift); });
    auto* P = F.Spawn<AGE152CallPanel>([](auto* X) { X->Configure(TEXT("Elevator"), Top); });
    if (!E || !C || !P) { AddError(TEXT("Actor spawn failed")); return false; }
    const FVector Start = E->GetCar()->GetComponentLocation();
    TestTrue(TEXT("Panel calls elevator"), P->Call()); TestTrue(TEXT("Panel logical presence immediate"), P->IsCarHere());
    TestFalse(TEXT("Same-stop call no effect"), P->Call()); TestTrue(TEXT("Visual trip not yet complete"), E->IsMoving());
    Prompt(*this, P->GetInteractable(), TEXT("Lift is here"), false);
    E->FrameStep(.5f); TestTrue(TEXT("Car constant-speed motion"), E->GetCar()->GetComponentLocation().Equals(Start + FVector(0, 0, 100), .01));
    P->GetInteractable()->Hack(nullptr); // inherited hack event must reach target lift
    TestTrue(TEXT("Panel hack sets lift state"), F.Facility->IsLiftHacked(TEXT("Elevator")));
    TestTrue(TEXT("Hack blocks new calls"), E->GetObstacle() == ELiftObstacle::Hacked);
    E->FrameStep(2.f); Sequence(*this, E->Events, {TEXT("native-arrived"), TEXT("bp-arrived")});
    TestFalse(TEXT("Arrival hook sees stopped car"), E->bArrivedWhileMoving);
    E->Events.Reset(); F.Facility->ResetToInitialState();
    TestTrue(TEXT("Reset snaps to bottom"), E->GetCar()->GetComponentLocation().Equals(Start));
    TestEqual(TEXT("Reset does not arrive"), E->Events.Num(), 0);
    TestTrue(TEXT("Call restored after reset"), P->Call()); E->FrameStep(.5f);
    TestTrue(TEXT("Cargo call"), C->Toggle()); C->FrameStep(.5f);
    F.Facility->SetAlarmLevel(EAlarmLevel::EAL_Lockdown);
    TestTrue(TEXT("Elevator logically parks"), E->GetStop() == Bottom);
    TestTrue(TEXT("Cargo stays at accepted logical destination"), C->GetStop() == Top);
    E->FrameStep(1.f); TestTrue(TEXT("Elevator returns bottom"), E->GetCar()->GetComponentLocation().Equals(Start));
    F.Facility->SetCircuitState(Security, ECircuitState::Cut);
    TestFalse(TEXT("Unpowered cargo refuses calls"), C->Toggle()); C->FrameStep(2.f);
    TestFalse(TEXT("In-flight cargo completes despite loss of power"), C->IsMoving());
    TestTrue(TEXT("Cargo keeps logical top"), C->GetStop() == Top);
    return true;
}

// REQUIRED: failed versus successful routing attempts, one patch, displaced home restoration.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE152Routing, "GameEngineBench.UE0164.Subsystem152.RoutingDisplacementAndUpgrade", GE152::Flags)
bool FGE152Routing::RunTest(const FString& Parameters)
{
    using namespace GE152; FFixture F; if (!F.Ready(*this)) return false;
    auto* L = F.Spawn<AGE152Lift>([](auto* X) { X->Configure(TEXT("Cargo"), Plant, ELiftKind::CargoLift); });
    auto* Fan = F.Spawn<AGE152Fan>([](auto* X) { X->Configure(TEXT("Fans"), Plant); });
    auto* A = F.Spawn<AGE152Routing>([](auto* X) { X->Configure(TEXT("Routing"), TEXT("Cargo"), Security); });
    auto* B = F.Spawn<AGE152Routing>([](auto* X) { X->Configure(TEXT("Routing"), TEXT("Fans"), Security); });
    auto* Drive = F.Spawn<AGE152Pickup>([](auto* X) { X->Configure(TEXT("Drive")); });
    if (!L || !Fan || !A || !B || !Drive) { AddError(TEXT("Actor spawn failed")); return false; }
    TestTrue(TEXT("Under-level attempts remain available"), A->CanHack());
    TestFalse(TEXT("Eligible failed-hack warning is present"), A->GetInteractable()->GetDisplayData().HackDisplayText.IsEmpty());
    TestTrue(TEXT("Failed hack reports changed alarm"), A->Hack());
    TestFalse(TEXT("Failed hack does not reroute"), A->IsOn());
    TestTrue(TEXT("Alarm alerted"), F.Facility->GetAlarmLevel() == EAlarmLevel::EAL_Alerted);
    TestTrue(TEXT("Drive pickup upgrades handheld"), Drive->Take()); TestEqual(TEXT("Handheld upgraded"), F.Facility->GetHandheldLevel(), 2);
    TestTrue(TEXT("Route cargo"), A->Hack()); TestTrue(TEXT("Cargo now powered independently of PLANT"), L->CanUse());
    TestFalse(TEXT("Fans remain stopped"), Fan->IsRunning());
    A->Events.Reset(); TestTrue(TEXT("Route fans displaces cargo"), B->Hack());
    TestFalse(TEXT("Cargo returned to cut home"), L->CanUse()); TestTrue(TEXT("Fans started on SECURITY"), Fan->IsRunning());
    Sequence(*this, A->Events, {TEXT("switch:0:0")});
    TestTrue(TEXT("Patch points to fans"), F.Facility->GetPatchedDevice() == TEXT("Fans"));
    TestTrue(TEXT("Selecting current patch undoes it"), B->Hack()); TestFalse(TEXT("Fans stopped after return home"), Fan->IsRunning());
    TestTrue(TEXT("No active patch"), F.Facility->GetPatchedDevice().IsNone());
    F.Facility->SetCircuitState(Security, ECircuitState::Cut);
    TestFalse(TEXT("Panel power gates attempt"), A->CanHack()); TestFalse(TEXT("Unpowered hack no change"), A->Hack());
    // A lever reports where the device is, not whether this panel created its placement.
    // Keep panel power and the whole routing state healthy before exercising an external change.
    F.Facility->SetCircuitState(Security, ECircuitState::Live);
    TestTrue(TEXT("External-route scenario begins with no panel patch"), F.Facility->GetPatchedDevice().IsNone());
    A->Events.Reset(); F.Facility->SetDeviceCircuit(TEXT("Cargo"), Security);
    TestTrue(TEXT("External circuit change powers cargo"), L->CanUse());
    TestFalse(TEXT("External circuit change is not a panel patch"), F.Facility->IsDevicePatched(TEXT("Cargo")));
    TestTrue(TEXT("Routing lever reflects unpatched matching circuit"), A->IsOn());
    Sequence(*this, A->Events, {TEXT("switch:1:0")});
    TestFalse(TEXT("Already-there unpatched option has no hack job"), A->CanHack());
    TestFalse(TEXT("Already-there unpatched hack makes no change"), A->Hack());
    A->Events.Reset(); F.Facility->SetDeviceCircuit(TEXT("Cargo"), Plant);
    TestFalse(TEXT("External return clears routing lever"), A->IsOn());
    Sequence(*this, A->Events, {TEXT("switch:0:0")});
    return true;
}

// REQUIRED: pump -> drainage, held-open coolant -> shorted power -> corroded exit lock.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE152FloodRules, "GameEngineBench.UE0164.Subsystem152.PumpDrainageShortingAndCorrosion", GE152::Flags)
bool FGE152FloodRules::RunTest(const FString& Parameters)
{
    using namespace GE152; FFixture F; if (!F.Ready(*this)) return false; auto* S = F.Facility;
    auto* Fan = F.Spawn<AGE152Fan>([](auto* X) { X->Configure(TEXT("Fans"), Plant); });
    auto* W = F.Spawn<AGE152Flood>([](auto* X) { X->Configure(TEXT("Fans")); });
    auto* P = F.Spawn<AGE152Routing>([](auto* X) { X->Configure(TEXT("Routing"), NAME_None, None, true); });
    auto* D = F.Spawn<AGE152Door>([](auto* X) { X->Configure(TEXT("Tunnel"), None, EDoorKind::Standard); });
    auto* LockdownDoor = F.Spawn<AGE152Door>([](auto* X) { X->Configure(TEXT("LockdownDoor"), Doors, EDoorKind::Lockdown); });
    if (!Fan || !W || !P || !D || !LockdownDoor) { AddError(TEXT("Actor spawn failed")); return false; }
    FExitState Exit; Exit.DoorId = TEXT("Tunnel"); Exit.FansId = TEXT("Fans"); S->RegisterExit(EFacilityExit::ServiceTunnel, Exit);
    S->GiveItem(EFacilityItem::ServerDrive); TestTrue(TEXT("Pump floods"), P->Hack());
    TestTrue(TEXT("Water logical lethal immediately"), W->IsLethal()); TestTrue(TEXT("Pump on-state follows water"), P->IsOn());
    TestFalse(TEXT("Pump repeat blocked while flooded"), P->Hack());
    S->SetDeviceCircuit(TEXT("Fans"), Security); TestTrue(TEXT("Running fan can drain"), S->WouldFansDrainFlood());
    S->AdvanceStep(); TestFalse(TEXT("Drain precedes shorting"), W->IsFlooded()); TestTrue(TEXT("DOORS survives drained step"), S->IsCircuitLive(Doors));
    TestTrue(TEXT("Pump eligible after drainage"), P->CanHack());
    S->GiveItem(EFacilityItem::PryBar); TestTrue(TEXT("Open valve refloods"), S->OpenCoolantValve());
    TestFalse(TEXT("Open valve prevents drainage despite powered fan"), S->WouldFansDrainFlood());
    S->AdvanceStep(); TestTrue(TEXT("First standing step below configured short threshold"), S->IsCircuitLive(Doors));
    S->AdvanceStep(); TestTrue(TEXT("Second standing step shorts DOORS"), S->GetCircuitState(Doors) == ECircuitState::Shorted);
    TestTrue(TEXT("Short makes lockdown door fail open"), LockdownDoor->IsOpen());
    TestTrue(TEXT("Failed-open obstacle"), LockdownDoor->GetObstacle() == EDoorObstacle::FailedOpen);
    TestFalse(TEXT("Water remains but is now safe"), W->IsLethal());
    TestFalse(TEXT("Tunnel not yet corroded"), S->IsDoorLockCorroded(TEXT("Tunnel")));
    S->AdvanceStep(); TestTrue(TEXT("Third step corrodes tunnel lock"), S->IsDoorLockCorroded(TEXT("Tunnel")));
    S->TakeItem(EFacilityItem::PryBar); TestTrue(TEXT("Corroded door opens without key or pry"), D->Use());
    TestTrue(TEXT("Tunnel escape now available with fans stopped"), S->CanEscape(EFacilityExit::ServiceTunnel));
    TestTrue(TEXT("Escape once"), S->Escape(EFacilityExit::ServiceTunnel)); TestFalse(TEXT("No double escape"), S->Escape(EFacilityExit::ServiceTunnel));
    S->ResetToInitialState(); TestFalse(TEXT("Escape reset"), S->HasEscaped());
    TestFalse(TEXT("Corrosion reset"), S->IsDoorLockCorroded(TEXT("Tunnel"))); TestFalse(TEXT("Water reset"), W->IsFlooded());
    return true;
}

// REQUIRED: actual damage dispatch and native overlap binding; animation is not a kill gate.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE152Fan, "GameEngineBench.UE0164.Subsystem152.FanDamageRampAndCleanup", GE152::Flags)
bool FGE152Fan::RunTest(const FString& Parameters)
{
    using namespace GE152; FFixture F; if (!F.Ready(*this)) return false;
    auto* Fan = F.Spawn<AGE152Fan>([](auto* X) { X->Configure(TEXT("Fans"), Plant, TEXT("Room")); });
    auto* Victim = F.Spawn<AGE152Victim>([](auto*) {});
    auto* Room = F.Spawn<AGE152Fan>([](auto* X) { X->Configure(TEXT("Room"), Security); });
    if (!Fan || !Victim || !Room) { AddError(TEXT("Actor spawn failed")); return false; }
    Fan->GetInteractable()->OnFocused(); // room registers after fan; focus refresh alone only updates prompt
    F.Facility->GiveItem(EFacilityItem::LockerItem); // general notification refreshes lamp
    TestFalse(TEXT("Fan lamp off in powered room"), Fan->Lamp()->IsVisible());
    const FQuat Rest = Fan->RotationHub()->GetRelativeRotation().Quaternion();
    Enter(Fan->Volume(), Victim); TestEqual(TEXT("Stopped fan harmless"), Victim->Hits, 0);
    Fan->Events.Reset(); F.Facility->SetDeviceCircuit(TEXT("Fans"), Security);
    Sequence(*this, Fan->Events, {TEXT("native-running:1:0"), TEXT("bp-running:1:0")});
    TestEqual(TEXT("Speed begins at zero before first tick"), Fan->GetSpinSpeed(), 0.f);
    Enter(Fan->Volume(), Victim); TestEqual(TEXT("Running at zero visual speed still kills"), Victim->Hits, 1);
    TestEqual(TEXT("Configured damage"), Victim->TotalDamage, 37.f);
    TestEqual(TEXT("Actor is damage causer"), Victim->LastCauser, static_cast<AActor*>(Fan)); TestNull(TEXT("No controller instigator"), Victim->LastInstigator);
    Fan->FrameStep(.5f); TestTrue(TEXT("720/1.5 acceleration"), FMath::IsNearlyEqual(Fan->GetSpinSpeed(), 240.f));
    const FQuat Expected = Rest * FQuat(FVector::UpVector, FMath::DegreesToRadians(120.f));
    TestTrue(TEXT("Rotation retains authored local axis"), Fan->RotationHub()->GetRelativeRotation().Quaternion().Equals(Expected, .001));
    F.Facility->SetDeviceCircuit(TEXT("Fans"), Plant); Enter(Fan->Volume(), Victim);
    TestEqual(TEXT("Coasting fan is harmless"), Victim->Hits, 1);
    Fan->FrameStep(.5f); TestTrue(TEXT("720/3 deceleration"), FMath::IsNearlyEqual(Fan->GetSpinSpeed(), 120.f));
    F.Facility->SetDeviceCircuit(TEXT("Fans"), Security); Fan->FrameStep(.5f);
    TestTrue(TEXT("Restart continues from current speed"), FMath::IsNearlyEqual(Fan->GetSpinSpeed(), 360.f));
    F.Facility->SetDeviceCircuit(TEXT("Room"), Plant); TestTrue(TEXT("Lamp follows rerouted dark zone"), Fan->Lamp()->IsVisible());
    F.Facility->ResetToInitialState(); TestEqual(TEXT("Reset speed snaps to stopped"), Fan->GetSpinSpeed(), 0.f);
    TestFalse(TEXT("Stopped reset disables tick"), Fan->IsActorTickEnabled());
    auto* Second = F.Spawn<AGE152Victim>([](auto*) {});
    if (!TestNotNull(TEXT("Second sweep victim"), Second)) return false;
    Victim->SetActorLocation(Fan->Volume()->GetComponentLocation() + FVector(0, 0, 10));
    Second->SetActorLocation(Fan->Volume()->GetComponentLocation() - FVector(0, 0, 10));
    Fan->Volume()->UpdateOverlaps(); Victim->Shape->UpdateOverlaps(); Second->Shape->UpdateOverlaps();
    if (!TestTrue(TEXT("Both victims actually overlap fan"), Fan->Volume()->IsOverlappingActor(Victim)
        && Fan->Volume()->IsOverlappingActor(Second))) return false;
    const int32 HitsBeforeSweep = Victim->Hits + Second->Hits;
    Victim->bResetOnHit = true; Second->bResetOnHit = true;
    F.Facility->SetDeviceCircuit(TEXT("Fans"), Security);
    TestEqual(TEXT("Activation sweep stops after first victim resets live state"), Victim->Hits + Second->Hits, HitsBeforeSweep + 1);
    TestFalse(TEXT("Damage-reset leaves fan stopped"), Fan->IsRunning());
    Fan->Destroy(); TestFalse(TEXT("EndPlay unbinds overlap"), Fan->Volume()->OnComponentBeginOverlap.IsBound());
    return true;
}

// REQUIRED: water appearance and engine damage use logical state, with real overlap sweeps.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE152Water, "GameEngineBench.UE0164.Subsystem152.FloodSurfaceAndLiveDamage", GE152::Flags)
bool FGE152Water::RunTest(const FString& Parameters)
{
    using namespace GE152; FFixture F; if (!F.Ready(*this)) return false;
    auto* W = F.Spawn<AGE152Flood>([](auto* X) { X->Configure(TEXT("Fans")); });
    auto* V = F.Spawn<AGE152Victim>([](auto*) {});
    if (!W || !V) { AddError(TEXT("Actor spawn failed")); return false; }
    if (!TestNotNull(TEXT("Engine cube available"), W->WaterMesh()->GetStaticMesh().Get())) return false;
    TestTrue(TEXT("Initial surface drained below floor"), FMath::IsNearlyEqual(W->GetSurfaceHeight(), -5.f));
    TestFalse(TEXT("Drained mesh hidden"), W->WaterMesh()->IsVisible());
    W->Events.Reset(); F.Facility->FloodPlant();
    Sequence(*this, W->Events, {TEXT("flooded:1:0"), TEXT("lethal:1:0")});
    Enter(W->Volume(), V); TestEqual(TEXT("Logical live water damage before visual rise"), V->Hits, 1);
    TestEqual(TEXT("Water configured damage"), V->TotalDamage, 53.f);
    TestEqual(TEXT("Water damage causer"), V->LastCauser, static_cast<AActor*>(W));
    W->FrameStep(1.f); TestTrue(TEXT("Five cm per second from drained depth"), FMath::IsNearlyEqual(W->GetSurfaceHeight(), 0.f));
    W->FrameStep(4.f); TestTrue(TEXT("Half flood height"), FMath::IsNearlyEqual(W->GetSurfaceHeight(), 20.f));
    const auto* Mesh = W->WaterMesh(); const auto Bounds = Mesh->GetStaticMesh()->GetBounds();
    const FVector Scale = Mesh->GetRelativeScale3D(), Location = Mesh->GetRelativeLocation();
    TestTrue(TEXT("Water keeps authored XY"), FMath::IsNearlyEqual(Location.X, 12.) && FMath::IsNearlyEqual(Location.Y, 18.)
        && FMath::IsNearlyEqual(Scale.X, 2.) && FMath::IsNearlyEqual(Scale.Y, 3.));
    TestTrue(TEXT("Cube lower bound on floor"), FMath::IsNearlyZero(Location.Z + Scale.Z * (Bounds.Origin.Z - Bounds.BoxExtent.Z), .01));
    TestTrue(TEXT("Cube upper bound at surface"), FMath::IsNearlyEqual(Location.Z + Scale.Z * (Bounds.Origin.Z + Bounds.BoxExtent.Z), 20., .01));
    TestTrue(TEXT("Kill volume hangs below surface"), FMath::IsNearlyEqual(W->Volume()->GetRelativeLocation().Z, -W->Volume()->GetUnscaledBoxExtent().Z));
    W->FrameStep(4.f); TestTrue(TEXT("Full trip takes nine seconds"), FMath::IsNearlyEqual(W->GetSurfaceHeight(), 40.f));
    TestFalse(TEXT("Arrival callback observes idle state"), W->bSettledWhileMoving);
    F.Facility->SetCircuitState(Doors, ECircuitState::Cut); F.Facility->SetCircuitState(Security, ECircuitState::Cut);
    Enter(W->Volume(), V); TestEqual(TEXT("Visible but unpowered water safe"), V->Hits, 1);
    // Populate a real overlap cache while safe. Re-powering must catch an already-inside pawn.
    V->SetActorLocation(W->Volume()->GetComponentLocation()); W->Volume()->UpdateOverlaps(); V->Shape->UpdateOverlaps();
    if (!TestTrue(TEXT("Fixture actually overlaps water"), W->Volume()->IsOverlappingActor(V))) return false;
    const int32 Before = V->Hits; V->bResetOnHit = true; F.Facility->SetCircuitState(Doors, ECircuitState::Live);
    TestEqual(TEXT("Newly live water catches occupant once"), V->Hits, Before + 1);
    TestFalse(TEXT("Damage-triggered reset makes water safe"), W->IsFlooded());
    TestTrue(TEXT("Nested reset snaps surface"), FMath::IsNearlyEqual(W->GetSurfaceHeight(), -5.f));
    W->Destroy(); TestFalse(TEXT("Flood EndPlay removes overlap handler"), W->Volume()->OnComponentBeginOverlap.IsBound());
    // A plane is a surface, not a solid box: preserve its authored scale and place it at water Z.
    UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (!TestNotNull(TEXT("Engine plane fixture available"), Plane)) return false;
    if (!TestTrue(TEXT("Plane fixture meets thin-mesh contract"), Plane->GetBounds().BoxExtent.Z * 2.f <= 1.f)) return false;
    auto* Flat = F.Spawn<AGE152Flood>([Plane](auto* X) {
        X->Configure(TEXT("Fans")); X->WaterMesh()->SetStaticMesh(Plane);
        X->WaterMesh()->SetRelativeScale3D(FVector(4, 6, 2));
    });
    if (!TestNotNull(TEXT("Flat-water actor"), Flat)) return false;
    TestTrue(TEXT("Flat-water setup begins dry after prior reset"), !Flat->IsFlooded());
    TestTrue(TEXT("Reflood with flat mesh"), F.Facility->FloodPlant()); Flat->FrameStep(9.f);
    TestTrue(TEXT("Flat water reaches logical flood height"), FMath::IsNearlyEqual(Flat->GetSurfaceHeight(), 40.f));
    TestTrue(TEXT("Flat mesh sits at surface not solid midpoint"), FMath::IsNearlyEqual(Flat->WaterMesh()->GetRelativeLocation().Z, 40., .01));
    TestTrue(TEXT("Flat mesh preserves authored scale including Z"), Flat->WaterMesh()->GetRelativeScale3D().Equals(FVector(4, 6, 2), .001));
    TestTrue(TEXT("Flat mesh preserves authored XY location"), FMath::IsNearlyEqual(Flat->WaterMesh()->GetRelativeLocation().X, 12.)
        && FMath::IsNearlyEqual(Flat->WaterMesh()->GetRelativeLocation().Y, 18.));
    return true;
}

// REQUIRED: exact notification semantics across the complete reset and ordinary no-op paths.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE152Reset, "GameEngineBench.UE0164.Subsystem152.AtomicNotificationsAndReset", GE152::Flags)
bool FGE152Reset::RunTest(const FString& Parameters)
{
    using namespace GE152; FFixture F; if (!F.Ready(*this)) return false; auto* S = F.Facility;
    auto* G = F.Spawn<AGE152Generator>([](auto*) {});
    auto* B = F.Spawn<AGE152Breaker>([](auto* X) { X->Configure(Doors); });
    auto* P = F.Spawn<AGE152Pickup>([](auto* X) { X->Configure(TEXT("Card")); });
    if (!G || !B || !P) { AddError(TEXT("Actor spawn failed")); return false; }
    F.Observer->Events.Reset(); S->RaiseAlarmLevelByOne();
    Sequence(*this, F.Observer->Events, {TEXT("state"), TEXT("alarm")});
    TestTrue(TEXT("Alarm old quiet"), F.Observer->LastOld == EAlarmLevel::EAL_Quiet);
    TestTrue(TEXT("Alarm new alerted"), F.Observer->LastNew == EAlarmLevel::EAL_Alerted);
    TestFalse(TEXT("Ordinary state callback is not marked resetting"), F.Observer->bLastStateWasReset);
    TestFalse(TEXT("Ordinary alarm callback is not marked resetting"), F.Observer->bLastAlarmWasReset);
    F.Observer->Events.Reset(); TestFalse(TEXT("Quiet bookkeeping step not visible"), S->AdvanceStep());
    TestEqual(TEXT("Quiet count progresses"), S->GetQuietSteps(), 1); TestEqual(TEXT("No bookkeeping event"), F.Observer->Events.Num(), 0);
    S->SetAlarmLevel(EAlarmLevel::EAL_Lockdown); S->AdvanceStep(); F.Observer->Events.Reset();
    TestFalse(TEXT("Noise at lockdown not a visible change"), S->RaiseAlarmLevelByOne());
    TestEqual(TEXT("Noise still restarts quiet counter"), S->GetQuietSteps(), 0); TestEqual(TEXT("No no-op event"), F.Observer->Events.Num(), 0);
    P->Take(); S->SetCircuitState(Doors, ECircuitState::Cut);
    F.Observer->Events.Reset(); F.Observer->bResetObserved = false; G->Events.Reset(); B->Events.Reset(); P->Events.Reset();
    S->ResetToInitialState(); Sequence(*this, F.Observer->Events, {TEXT("state"), TEXT("alarm")});
    TestTrue(TEXT("Reset flag held during broadcasts"), F.Observer->bResetObserved); TestFalse(TEXT("Reset flag cleared afterward"), S->IsResetting());
    TestTrue(TEXT("Reset marker is true in general-state callback"), F.Observer->bLastStateWasReset);
    TestTrue(TEXT("Reset marker remains true in alarm callback"), F.Observer->bLastAlarmWasReset);
    Sequence(*this, G->Events, {TEXT("native-running:0:1"), TEXT("bp-running:0:1"), TEXT("native-overload:0:1"), TEXT("bp-overload:0:1")});
    Sequence(*this, B->Events, {TEXT("switch:1:0")}); // breaker reset edge deliberately non-instant
    Sequence(*this, P->Events, {TEXT("native-taken:0:1"), TEXT("bp-taken:0:1")});
    G->Events.Reset(); B->Events.Reset(); F.Observer->Events.Reset(); S->ResetToInitialState();
    Sequence(*this, F.Observer->Events, {TEXT("state")}); TestEqual(TEXT("Unchanged breaker reset no callback"), B->Events.Num(), 0);
    TestEqual(TEXT("Generator forced reset callbacks even unchanged"), G->Events.Num(), 4);
    // A general listener may reset synchronously. No stale inventory survives the outer action.
    F.Observer->bResetOnItem = true; S->GiveItem(EFacilityItem::Keycard2);
    TestFalse(TEXT("Reentrant reset restores authoritative inventory"), S->HasItem(EFacilityItem::Keycard2));
    return true;
}

namespace GE152
{
class FClockCheck : public IAutomationLatentCommand
{
    TSharedPtr<FFixture> Fixture;
    FAutomationTestBase* Test;
    int32 Phase = 0;
    uint64 LastFrame = MAX_uint64;
public:
    FClockCheck(TSharedPtr<FFixture> InFixture, FAutomationTestBase* InTest) : Fixture(InFixture), Test(InTest) {}
    virtual bool Update() override
    {
        if (LastFrame == GFrameCounter) return false;
        LastFrame = GFrameCounter;
        // First tick activates pending timers; subsequent engine frames test repeated firing.
        Fixture->World->GetTimerManager().Tick(Phase == 0 ? 0.f : .26f);
        if (Phase > 0) Test->TestEqual(TEXT("World clock consumes configured steps"), Fixture->Facility->GetGeneratorStepsLeft(), 4 - Phase);
        ++Phase;
        if (Phase < 3) return false;
        Fixture.Reset(); return true;
    }
};
}

// REQUIRED: command surface plus a real timer firing on distinct automation frames.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE152Clock, "GameEngineBench.UE0164.Subsystem152.ConsoleAndWorldClock", GE152::Flags)
bool FGE152Clock::RunTest(const FString& Parameters)
{
    using namespace GE152; auto F = MakeShared<FFixture>(.25f); if (!F->Ready(*this)) return false;
    FOutputDeviceNull Out;
    auto Command = [&](const TCHAR* Line) { return IConsoleManager::Get().ProcessUserConsoleInput(Line, Out, F->World); };
    TestTrue(TEXT("GiveItem command registered"), Command(TEXT("Facility.GiveItem ServiceKey")));
    TestTrue(TEXT("Command gives named item"), F->Facility->HasItem(EFacilityItem::ServiceKey));
    TestTrue(TEXT("Alarm command registered"), Command(TEXT("Facility.SetAlarmLevel lockdown")));
    TestTrue(TEXT("Case-insensitive level parsed"), F->Facility->GetAlarmLevel() == EAlarmLevel::EAL_Lockdown);
    Command(TEXT("Facility.AdvanceStep 3")); TestTrue(TEXT("Command advances configured decay"), F->Facility->GetAlarmLevel() == EAlarmLevel::EAL_Alerted);
    TestTrue(TEXT("Flood command registered"), Command(TEXT("Facility.FloodPlant"))); TestTrue(TEXT("Command floods state"), F->Facility->IsPlantFlooded());
    F->Facility->ResetToInitialState(); TestTrue(TEXT("Start timer subject"), F->Facility->StartGenerator());
    ADD_LATENT_AUTOMATION_COMMAND(FClockCheck(F, this));
    return true;
}

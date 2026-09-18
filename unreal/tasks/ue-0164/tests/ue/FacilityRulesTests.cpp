#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Facility/FacilityRules.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace Task0158
{
using R = FFacilityRules;
using C = EFacilityCircuit;
using B = ECircuitState;
using A = EAlarmLevel;
using I = EFacilityItem;

// These fixtures seed plain input data, not a second implementation of the rules.
// Preparing independent inputs keeps a missing registration function from masking
// every other scenario. All tested transitions and observations use the public API.
static FFacilityState Base()
{
    FFacilityState S;
    S.Circuits.Add(C::EFC_Doors, B::Live);
    S.Circuits.Add(C::EFC_Security, B::Live);
    S.Circuits.Add(C::EFC_Plant, B::Cut);
    S.Config.AlarmDecaySteps = 3;
    S.Config.GeneratorRunSteps = 3;
    S.Config.FloodShortSteps = 2;
    S.Config.CorrosionSteps = 4;
    return S;
}
static void Door(FFacilityState& S, FName Id, EDoorKind Kind, C Circuit, int32 Lock = 0)
{
    FDoorState D;
    D.Kind = Kind;
    D.LockLevel = Lock;
    S.Doors.Add(Id, D);
    if (Circuit != C::EFC_None) S.DeviceCircuits.Add(Id, Circuit);
}
static void Lift(FFacilityState& S, FName Id, ELiftKind Kind, C Circuit)
{
    FLiftState L;
    L.Kind = Kind;
    L.Stop = ELiftStop::ELS_Top;
    S.Lifts.Add(Id, L);
    S.DeviceCircuits.Add(Id, Circuit);
}
static void Camera(FFacilityState& S, int32 Level = 1)
{
    FCameraState Cam;
    Cam.LightsId = TEXT("Lamp");
    S.Cameras.Add(TEXT("Camera"), Cam);
    S.DeviceCircuits.Add(TEXT("Camera"), C::EFC_Security);
    S.DeviceCircuits.Add(TEXT("Lamp"), C::EFC_Doors);
    S.HackLevels.Add(TEXT("Camera"), Level);
}
static FRoutingOption Route(FName Id, C Circuit)
{
    FRoutingOption O;
    O.DeviceId = Id;
    O.Circuit = Circuit;
    return O;
}
static void Panel(FFacilityState& S, int32 Level = 1)
{
    S.Routing.PanelId = TEXT("Panel");
    S.DeviceCircuits.Add(TEXT("Panel"), C::EFC_Security);
    S.HackLevels.Add(TEXT("Panel"), Level);
    S.DeviceCircuits.Add(TEXT("Fans"), C::EFC_Plant);
    Lift(S, TEXT("Cargo"), ELiftKind::CargoLift, C::EFC_Plant);
    S.Routing.Options.Add(Route(TEXT("Cargo"), C::EFC_Security));
    S.Routing.Options.Add(Route(TEXT("Fans"), C::EFC_Security));
    FRoutingOption Pump;
    Pump.Action = ERoutingAction::CoolantPump;
    Pump.DeviceId = NAME_None;
    Pump.Circuit = C::EFC_None;
    S.Routing.Options.Add(Pump);
}
static void Tunnel(FFacilityState& S)
{
    Door(S, TEXT("Tunnel"), EDoorKind::Standard, C::EFC_None);
    S.Doors.FindChecked(TEXT("Tunnel")).KeyItem = I::ServiceKey;
    FExitState E;
    E.DoorId = TEXT("Tunnel");
    E.FansId = TEXT("Fans");
    S.Exits.Add(EFacilityExit::ServiceTunnel, E);
    S.DeviceCircuits.Add(TEXT("Fans"), C::EFC_Plant);
    S.Flood.FansId = TEXT("Fans");
}
}
using namespace Task0158;

// REQUIRED: seed scope, first-registration wins, safe absent values and query purity.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(F0158Registration, "GameEngineBench.UE0164.Subsystem158.RegistrationAndSeeds", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool F0158Registration::RunTest(const FString&)
{
    FFacilityState S = Base();
    S.Config.HandheldLevel = 4;
    S.DeviceCircuits.Add(TEXT("Saved"), C::EFC_Plant);
    S.Inventory.Add(I::ServiceKey);
    S.EscapedThrough = EFacilityExit::ServiceTunnel;
    S.AlarmLevel = A::EAL_Lockdown;
    S.QuietSteps = 9;
    S.GeneratorStepsLeft = 8;
    S.bGeneratorOverloaded = true;
    S.Flood.bFlooded = true;
    S.Flood.Steps = 9;
    S.Flood.FansId = TEXT("SavedFans");
    S.bCoolantValveOpen = true;
    S.bServerRackOverloaded = true;
    R::SetInitialState(S);
    TestTrue(TEXT("initial breakers"), R::IsBreakerOn(S, C::EFC_Doors) && R::IsBreakerOn(S, C::EFC_Security) && !R::IsBreakerOn(S, C::EFC_Plant));
    TestTrue(TEXT("initial scalar state"), R::GetAlarmLevel(S) == A::EAL_Quiet && S.QuietSteps == 0 && !R::IsGeneratorRunning(S) && !R::IsGeneratorOverloaded(S));
    TestTrue(TEXT("initial hazards cleared"), !R::IsPlantFlooded(S) && R::GetFloodSteps(S) == 0 && !R::IsCoolantValveOpen(S) && !R::IsLabSmokeFilled(S));
    TestTrue(TEXT("not a whole-attempt reset"), S.Inventory.Contains(I::ServiceKey) && S.EscapedThrough == EFacilityExit::ServiceTunnel && S.DeviceCircuits.Contains(TEXT("Saved")) && S.Flood.FansId == FName(TEXT("SavedFans")) && S.Config.HandheldLevel == 4);
    FDoorState D; D.LockLevel = 3;
    TestTrue(TEXT("register door"), R::AddDoor(S, TEXT("Door"), D));
    TestFalse(TEXT("do not replace existing door"), R::AddDoor(S, TEXT("Door"), FDoorState()));
    TestEqual(TEXT("lock preserved"), R::GetDoorLockLevel(S, TEXT("Door")), 3);
    TestFalse(TEXT("empty door rejected"), R::AddDoor(S, NAME_None, D));
    FLiftState L; L.Stop = ELiftStop::ELS_Top;
    TestTrue(TEXT("register lift"), R::AddLift(S, TEXT("Lift"), L));
    TestFalse(TEXT("duplicate lift"), R::AddLift(S, TEXT("Lift"), FLiftState()));
    TestTrue(TEXT("lift placement preserved"), R::GetLiftStop(S, TEXT("Lift")) == ELiftStop::ELS_Top);
    TestFalse(TEXT("empty lift"), R::AddLift(S, NAME_None, L));
    TestFalse(TEXT("zero hack target"), R::AddHackTarget(S, TEXT("Hack"), 0));
    TestTrue(TEXT("positive hack target"), R::AddHackTarget(S, TEXT("Hack"), 2));
    TestFalse(TEXT("duplicate hack target"), R::AddHackTarget(S, TEXT("Hack"), 8));
    TestEqual(TEXT("first hack level"), R::GetHackLevel(S, TEXT("Hack")), 2);
    FCameraState Cam; Cam.LightsId = TEXT("Light");
    TestTrue(TEXT("register camera"), R::AddCamera(S, TEXT("Cam"), Cam));
    TestFalse(TEXT("duplicate camera"), R::AddCamera(S, TEXT("Cam"), FCameraState()));
    FCrateState Cr; Cr.LiftId = TEXT("Lift");
    TestTrue(TEXT("register crate"), R::AddCrate(S, TEXT("Crate"), Cr));
    TestFalse(TEXT("duplicate crate"), R::AddCrate(S, TEXT("Crate"), FCrateState()));
    FExitState Ex; Ex.DoorId = TEXT("Door");
    TestTrue(TEXT("register exit"), R::AddExit(S, EFacilityExit::MainGate, Ex));
    TestFalse(TEXT("duplicate exit"), R::AddExit(S, EFacilityExit::MainGate, FExitState()));
    TestFalse(TEXT("None exit"), R::AddExit(S, EFacilityExit::None, Ex));
    const FExitState* Found = R::FindExit(S, EFacilityExit::MainGate);
    TestTrue(TEXT("exit registration retained"), Found && Found->DoorId == FName(TEXT("Door")));
    FFacilityState Empty;
    TestFalse(TEXT("empty flood registration"), R::AddFlood(Empty, NAME_None));
    TestTrue(TEXT("flood registration"), R::AddFlood(Empty, TEXT("Fans")));
    TestFalse(TEXT("first flood fans win"), R::AddFlood(Empty, TEXT("Other")));
    TestEqual(TEXT("fans identity retained"), Empty.Flood.FansId, FName(TEXT("Fans")));
    const int32 Before = Empty.DeviceCircuits.Num();
    TestTrue(TEXT("unknown defaults"), R::GetCircuitState(Empty, C::EFC_Doors) == B::Cut && R::GetDeviceCircuit(Empty, TEXT("Unknown")) == C::EFC_None && !R::IsDevicePowered(Empty, TEXT("Unknown")));
    TestTrue(TEXT("unknown actor defaults"), R::GetLiftStop(Empty, TEXT("Unknown")) == ELiftStop::ELS_Bottom && R::GetLiftKind(Empty, TEXT("Unknown")) == ELiftKind::CargoLift && R::IsPickupTaken(Empty, TEXT("Unknown")) && R::GetCratePosition(Empty, TEXT("Unknown")) == ECratePosition::InPlant && R::FindExit(Empty, EFacilityExit::ServiceTunnel) == nullptr);
    TestEqual(TEXT("queries do not register"), Empty.DeviceCircuits.Num(), Before);
    TestFalse(TEXT("same implicit circuit is no-op"), R::SetDeviceCircuit(Empty, TEXT("Unknown"), C::EFC_None));
    TestTrue(TEXT("low-level closed door setter creates entry"), R::SetDoorOpen(Empty, TEXT("NewDoor"), false));
    TestFalse(TEXT("same setter is no-op"), R::SetDoorOpen(Empty, TEXT("NewDoor"), false));
    TestTrue(TEXT("unknown door is mechanically openable"), R::CanOpenDoor(Empty, TEXT("Another")) && R::ToggleDoor(Empty, TEXT("Another")) && R::IsDoorOpen(Empty, TEXT("Another")));
    return true;
}

// REQUIRED: breaker positions versus delivery and automatic settlement.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(F0158Power, "GameEngineBench.UE0164.Subsystem158.PowerAndSettlement", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool F0158Power::RunTest(const FString&)
{
    FFacilityState S = Base();
    Door(S, TEXT("Lock"), EDoorKind::Lockdown, C::EFC_Doors);
    S.Doors.FindChecked(TEXT("Lock")).bOpen = true;
    Lift(S, TEXT("Elevator"), ELiftKind::Elevator, C::EFC_Doors);
    Lift(S, TEXT("Cargo"), ELiftKind::CargoLift, C::EFC_Doors);
    TestFalse(TEXT("None is not a breaker"), R::FlipBreaker(S, C::EFC_None));
    TestTrue(TEXT("third breaker trips panel"), R::FlipBreaker(S, C::EFC_Plant));
    TestTrue(TEXT("all breaker positions cut"), !R::IsBreakerOn(S, C::EFC_Doors) && !R::IsBreakerOn(S, C::EFC_Security) && !R::IsBreakerOn(S, C::EFC_Plant));
    TestTrue(TEXT("blackout settles door and only elevator"), !R::IsDoorOpen(S, TEXT("Lock")) && R::IsDoorSealed(S, TEXT("Lock")) && R::GetLiftStop(S, TEXT("Elevator")) == ELiftStop::ELS_Bottom && R::GetLiftStop(S, TEXT("Cargo")) == ELiftStop::ELS_Top);
    TestTrue(TEXT("restore breaker"), R::FlipBreaker(S, C::EFC_Doors));
    TestFalse(TEXT("same circuit value"), R::SetCircuitState(S, C::EFC_Doors, B::Live));
    TestTrue(TEXT("release does not reopen door"), !R::IsDoorSealed(S, TEXT("Lock")) && !R::IsDoorOpen(S, TEXT("Lock")));
    R::SetAlarmLevel(S, A::EAL_Lockdown);
    TestTrue(TEXT("short transition"), R::SetCircuitState(S, C::EFC_Doors, B::Shorted));
    TestTrue(TEXT("short beats lockdown"), R::IsDoorOpen(S, TEXT("Lock")) && R::IsDoorFailedOpen(S, TEXT("Lock")) && !R::IsDoorSealed(S, TEXT("Lock")));
    TestFalse(TEXT("panel cannot repair short"), R::FlipBreaker(S, C::EFC_Doors));
    for (bool bCircuitFirst : {false, true})
    {
        FFacilityState T = Base();
        FDoorState D; D.Kind = EDoorKind::Lockdown; D.bOpen = true;
        FLiftState L; L.Kind = ELiftKind::Elevator; L.Stop = ELiftStop::ELS_Top;
        if (bCircuitFirst) { R::SetDeviceCircuit(T, TEXT("D"), C::EFC_Plant); R::SetDeviceCircuit(T, TEXT("L"), C::EFC_Plant); }
        R::AddDoor(T, TEXT("D"), D); R::AddLift(T, TEXT("L"), L);
        if (!bCircuitFirst) { R::SetDeviceCircuit(T, TEXT("D"), C::EFC_Plant); R::SetDeviceCircuit(T, TEXT("L"), C::EFC_Plant); }
        TestTrue(TEXT("registration-order settlement"), !R::IsDoorOpen(T, TEXT("D")) && R::GetLiftStop(T, TEXT("L")) == ELiftStop::ELS_Bottom);
    }
    return true;
}

// REQUIRED: access predicates, alternate credentials, force precedence and noisy gate.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(F0158Doors, "GameEngineBench.UE0164.Subsystem158.AccessAndForcing", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool F0158Doors::RunTest(const FString&)
{
    FFacilityState S = Base();
    Door(S, TEXT("Dual"), EDoorKind::Standard, C::EFC_Doors, 3);
    S.Doors.FindChecked(TEXT("Dual")).KeyItem = I::ServiceKey;
    S.HackLevels.Add(TEXT("Dual"), 1);
    TestTrue(TEXT("lock obstacle"), R::IsDoorLocked(S, TEXT("Dual")) && R::GetDoorObstacle(S, TEXT("Dual")) == EDoorObstacle::Locked);
    TestTrue(TEXT("named key requirement"), R::GetDoorKeyItem(S, TEXT("Dual")) == I::ServiceKey);
    R::GiveItem(S, I::Keycard2);
    TestFalse(TEXT("insufficient card"), R::ToggleDoor(S, TEXT("Dual")));
    R::GiveItem(S, I::ServiceKey);
    TestTrue(TEXT("either credential opens"), R::ToggleDoor(S, TEXT("Dual")));
    R::TakeItem(S, I::ServiceKey);
    TestTrue(TEXT("open door closes without credential"), R::CanCloseDoor(S, TEXT("Dual")) && R::ToggleDoor(S, TEXT("Dual")));
    TestTrue(TEXT("powered lock hack"), R::CanHackDoor(S, TEXT("Dual")) && R::HackDoor(S, TEXT("Dual")) && R::IsDoorOpen(S, TEXT("Dual")));
    R::SetDoorOpen(S, TEXT("Dual"), false);
    R::SetCircuitState(S, C::EFC_Doors, B::Cut);
    TestTrue(TEXT("dead circuit precedes lock"), R::GetDoorObstacle(S, TEXT("Dual")) == EDoorObstacle::NoPower && !R::CanHackDoor(S, TEXT("Dual")));
    R::GiveItem(S, I::PryBar); R::SetAlarmLevel(S, A::EAL_Alerted);
    TestTrue(TEXT("pry through dead circuit"), R::CanForceDoor(S, TEXT("Dual")) && R::ForceDoor(S, TEXT("Dual")));
    TestTrue(TEXT("force precedes resulting lockdown"), R::IsDoorForced(S, TEXT("Dual")) && R::IsDoorOpen(S, TEXT("Dual")) && R::IsLockdown(S) && R::GetDoorObstacle(S, TEXT("Dual")) == EDoorObstacle::Forced);
    TestFalse(TEXT("forced cannot close"), R::SetDoorOpen(S, TEXT("Dual"), false));
    TestFalse(TEXT("forced cannot toggle"), R::ToggleDoor(S, TEXT("Dual")));
    FFacilityState G = Base();
    Door(G, TEXT("Gate"), EDoorKind::Gate, C::EFC_Doors, 3);
    G.HackLevels.Add(TEXT("Gate"), 1);
    G.DeviceCircuits.Add(TEXT("Controller"), C::EFC_Security); G.HackLevels.Add(TEXT("Controller"), 2);
    G.Inventory.Add(I::PryBar);
    TestFalse(TEXT("gate cannot be forced"), R::CanForceDoor(G, TEXT("Gate")));
    TestFalse(TEXT("gate itself cannot be hacked"), R::CanHackDoor(G, TEXT("Gate")));
    TestTrue(TEXT("under-level controller changes alarm not gate"), R::HackGateController(G, TEXT("Controller")) && !R::IsDoorOpen(G, TEXT("Gate")) && R::GetAlarmLevel(G) == A::EAL_Alerted);
    R::GiveItem(G, I::ServerDrive);
    TestTrue(TEXT("opening alerted gate is an event"), R::IsDoorLoudToOpen(G, TEXT("Gate")) && R::HackGateController(G, TEXT("Controller")));
    TestTrue(TEXT("gate immediately reseals at lockdown"), R::IsLockdown(G) && !R::IsDoorOpen(G, TEXT("Gate")) && R::IsDoorSealed(G, TEXT("Gate")));
    R::SetAlarmLevel(G, A::EAL_Quiet);
    TestTrue(TEXT("quiet controller opens gate"), R::HackGateController(G, TEXT("Controller")) && R::IsDoorOpen(G, TEXT("Gate")));
    // A door's own under-level path must emit noise, independently of camera/controller hacks.
    FFacilityState H = Base();
    H.Config.ServerDriveHandheldLevel = 3;
    Door(H, TEXT("CardDoor"), EDoorKind::Standard, C::EFC_Doors, 2);
    H.HackLevels.Add(TEXT("CardDoor"), 3);
    TestTrue(TEXT("under-level door hack remains eligible"), R::CanHackDoor(H, TEXT("CardDoor")));
    TestTrue(TEXT("under-level door hack reports first alarm event"), R::HackDoor(H, TEXT("CardDoor")));
    TestTrue(TEXT("under-level door remains locked and closed"), R::IsDoorLocked(H, TEXT("CardDoor")) && !R::IsDoorOpen(H, TEXT("CardDoor")));
    TestTrue(TEXT("failed door hack raises only to alerted"), R::GetAlarmLevel(H) == A::EAL_Alerted);
    R::AdvanceStep(H);
    TestTrue(TEXT("second failed door hack reaches lockdown"), R::HackDoor(H, TEXT("CardDoor")) && R::IsLockdown(H));
    R::AdvanceStep(H); R::AdvanceStep(H);
    TestEqual(TEXT("door failure setup accumulated quiet steps"), H.QuietSteps, 2);
    TestFalse(TEXT("failed door hack at ceiling reports no visible change"), R::HackDoor(H, TEXT("CardDoor")));
    TestEqual(TEXT("failed door hack at ceiling restarts quiet time"), H.QuietSteps, 0);
    TestFalse(TEXT("failed door hack never opens"), R::IsDoorOpen(H, TEXT("CardDoor")));
    TestTrue(TEXT("drive upgrade acquired after failed attempts"), R::GiveItem(H, I::ServerDrive));
    TestTrue(TEXT("upgraded door hack opens without card"), R::HackDoor(H, TEXT("CardDoor")) && R::IsDoorOpen(H, TEXT("CardDoor")));
    return true;
}

// REQUIRED: powered lift calls, failed/successful hacks and settlement after hack.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(F0158Lifts, "GameEngineBench.UE0164.Subsystem158.LiftHackAndParking", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool F0158Lifts::RunTest(const FString&)
{
    FFacilityState S = Base();
    Lift(S, TEXT("Elevator"), ELiftKind::Elevator, C::EFC_Doors);
    Lift(S, TEXT("Cargo"), ELiftKind::CargoLift, C::EFC_Security);
    S.HackLevels.Add(TEXT("Elevator"), 2);
    TestTrue(TEXT("stop inverse"), R::OtherLiftStop(ELiftStop::ELS_Top) == ELiftStop::ELS_Bottom && R::OtherLiftStop(ELiftStop::ELS_Bottom) == ELiftStop::ELS_Top);
    TestFalse(TEXT("same stop no event"), R::CallLift(S, TEXT("Elevator"), ELiftStop::ELS_Top));
    TestTrue(TEXT("call powered lift"), R::CallLift(S, TEXT("Elevator"), ELiftStop::ELS_Bottom));
    TestTrue(TEXT("failed hack raises alarm"), R::HackLift(S, TEXT("Elevator")) && !R::IsLiftHacked(S, TEXT("Elevator")));
    R::SetAlarmLevel(S, A::EAL_Quiet); R::GiveItem(S, I::ServerDrive);
    R::CallLift(S, TEXT("Elevator"), ELiftStop::ELS_Top);
    TestTrue(TEXT("successful hack stops car in place"), R::HackLift(S, TEXT("Elevator")) && R::IsLiftHacked(S, TEXT("Elevator")) && R::GetLiftStop(S, TEXT("Elevator")) == ELiftStop::ELS_Top);
    TestFalse(TEXT("hacked car refuses calls"), R::CallLift(S, TEXT("Elevator"), ELiftStop::ELS_Bottom));
    TestFalse(TEXT("cannot hack again"), R::HackLift(S, TEXT("Elevator")));
    R::SetCircuitState(S, C::EFC_Doors, B::Cut);
    TestTrue(TEXT("power loss parks even hacked elevator, but obstacle remains hacked"), R::GetLiftStop(S, TEXT("Elevator")) == ELiftStop::ELS_Bottom && R::GetLiftObstacle(S, TEXT("Elevator")) == ELiftObstacle::Hacked);
    R::SetAlarmLevel(S, A::EAL_Lockdown);
    TestTrue(TEXT("cargo ignores lockdown"), R::CanUseLift(S, TEXT("Cargo")) && R::CallLift(S, TEXT("Cargo"), ELiftStop::ELS_Bottom));
    TestFalse(TEXT("setter does not invent lift"), R::SetLiftStop(S, TEXT("Unknown"), ELiftStop::ELS_Top));
    FFacilityState T = Base(); Lift(T, TEXT("E"), ELiftKind::Elevator, C::EFC_Doors);
    R::SetAlarmLevel(T, A::EAL_Lockdown);
    TestTrue(TEXT("lockdown obstacle"), R::GetLiftObstacle(T, TEXT("E")) == ELiftObstacle::Lockdown);
    R::SetCircuitState(T, C::EFC_Doors, B::Cut);
    TestTrue(TEXT("no power precedes lockdown"), R::GetLiftObstacle(T, TEXT("E")) == ELiftObstacle::NoPower);
    return true;
}

// REQUIRED: inventory set semantics and camera eligibility composed from light, power and drive.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(F0158Inventory, "GameEngineBench.UE0164.Subsystem158.InventoryAndCameras", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool F0158Inventory::RunTest(const FString&)
{
    FFacilityState S = Base(); Camera(S, 2);
    TestTrue(TEXT("camera initially sees"), R::IsCameraWatching(S, TEXT("Camera")) && R::AreLightsOn(S, TEXT("Lamp")));
    TestFalse(TEXT("None item cannot be given"), R::GiveItem(S, I::None));
    TestFalse(TEXT("None pickup item"), R::AddPickup(S, TEXT("Empty"), I::None));
    TestTrue(TEXT("pickup register"), R::AddPickup(S, TEXT("DriveA"), I::ServerDrive));
    TestFalse(TEXT("pickup register first wins"), R::AddPickup(S, TEXT("DriveA"), I::Keycard3));
    TestTrue(TEXT("pickup item identity"), R::GetPickupItem(S, TEXT("DriveA")) == I::ServerDrive);
    TestTrue(TEXT("second pickup register"), R::AddPickup(S, TEXT("DriveB"), I::ServerDrive));
    TestTrue(TEXT("take drive"), R::CanTakePickup(S, TEXT("DriveA")) && R::TakePickup(S, TEXT("DriveA")));
    TestTrue(TEXT("drive changes capability and camera"), R::IsServerDrivePulled(S) && R::GetHandheldLevel(S) == 2 && !R::IsCameraWatching(S, TEXT("Camera")));
    TestTrue(TEXT("duplicate pickup still empties"), R::TakePickup(S, TEXT("DriveB")) && R::IsPickupTaken(S, TEXT("DriveB")));
    TestEqual(TEXT("inventory is set"), S.Inventory.Num(), 1);
    TestFalse(TEXT("taken pickup cannot repeat"), R::TakePickup(S, TEXT("DriveA")));
    TestFalse(TEXT("duplicate give is no change"), R::GiveItem(S, I::ServerDrive));
    S.Config.HandheldLevel = 5; S.Config.ServerDriveHandheldLevel = 2;
    TestEqual(TEXT("drive never downgrades base"), R::GetHandheldLevel(S), 5);
    TestFalse(TEXT("level zero is not a target"), R::WouldHackSucceed(S, 0));
    R::TakeItem(S, I::ServerDrive);
    TestTrue(TEXT("drive removal restores sight without restoring pickups"), R::IsCameraWatching(S, TEXT("Camera")) && R::IsPickupTaken(S, TEXT("DriveA")));
    R::SetCircuitState(S, C::EFC_Doors, B::Cut);
    TestFalse(TEXT("camera cannot see in darkness"), R::CameraSighting(S, TEXT("Camera")));
    TestTrue(TEXT("powered camera still hackable in dark"), R::CanHackCamera(S, TEXT("Camera")) && R::HackCamera(S, TEXT("Camera")));
    R::SetCircuitState(S, C::EFC_Doors, B::Live);
    TestTrue(TEXT("loop persists after lights return"), R::IsCameraLooped(S, TEXT("Camera")) && !R::IsCameraWatching(S, TEXT("Camera")));
    TestFalse(TEXT("loop repeated no change"), R::HackCamera(S, TEXT("Camera")));
    R::GiveItem(S, I::Keycard2); R::GiveItem(S, I::Keycard3);
    TestEqual(TEXT("highest card"), R::GetKeycardLevel(S), 3);
    R::TakeItem(S, I::Keycard3);
    TestEqual(TEXT("remaining card"), R::GetKeycardLevel(S), 2);
    TestEqual(TEXT("noncard level"), R::KeycardLevelOf(I::ServiceKey), 0);
    return true;
}

// REQUIRED: routing registration, one-patch displacement/undo, capability failure and pump normalization.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(F0158Routing, "GameEngineBench.UE0164.Subsystem158.RoutingSinglePatch", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool F0158Routing::RunTest(const FString&)
{
    FFacilityState S = Base();
    TestFalse(TEXT("empty panel"), R::AddRoutingOption(S, NAME_None, Route(TEXT("Cargo"), C::EFC_Security)));
    TestFalse(TEXT("empty device job"), R::AddRoutingOption(S, TEXT("Panel"), Route(NAME_None, C::EFC_Security)));
    TestFalse(TEXT("None circuit job"), R::AddRoutingOption(S, TEXT("Panel"), Route(TEXT("Cargo"), C::EFC_None)));
    TestTrue(TEXT("valid job"), R::AddRoutingOption(S, TEXT("Panel"), Route(TEXT("Cargo"), C::EFC_Security)));
    TestFalse(TEXT("same job"), R::AddRoutingOption(S, TEXT("Panel"), Route(TEXT("Cargo"), C::EFC_Security)));
    TestFalse(TEXT("second panel rejected"), R::AddRoutingOption(S, TEXT("OtherPanel"), Route(TEXT("Fans"), C::EFC_Security)));
    FRoutingOption Pump; Pump.Action = ERoutingAction::CoolantPump; Pump.DeviceId = TEXT("Ignored"); Pump.Circuit = C::EFC_Plant;
    TestTrue(TEXT("pump registers"), R::AddRoutingOption(S, TEXT("Panel"), Pump));
    Pump.DeviceId = TEXT("AlsoIgnored"); Pump.Circuit = C::EFC_Doors;
    TestFalse(TEXT("pump normalized duplicate"), R::AddRoutingOption(S, TEXT("Panel"), Pump));
    TestEqual(TEXT("two jobs only"), S.Routing.Options.Num(), 2);
    FFacilityState T = Base(); Panel(T, 2);
    TestTrue(TEXT("under-level attempt eligible"), R::CanRouteDevice(T, TEXT("Panel"), TEXT("Cargo"), C::EFC_Security));
    TestTrue(TEXT("under-level reports alarm change"), R::RouteDevice(T, TEXT("Panel"), TEXT("Cargo"), C::EFC_Security));
    TestTrue(TEXT("failed routing does not patch"), R::GetPatchedDevice(T).IsNone() && R::GetDeviceCircuit(T, TEXT("Cargo")) == C::EFC_Plant);
    R::GiveItem(T, I::ServerDrive);
    TestTrue(TEXT("route cargo"), R::RouteDevice(T, TEXT("Panel"), TEXT("Cargo"), C::EFC_Security));
    TestTrue(TEXT("cargo powered without fans"), R::CanUseLift(T, TEXT("Cargo")) && !R::AreFansRunning(T, TEXT("Fans")) && R::IsDevicePatched(T, TEXT("Cargo")));
    TestTrue(TEXT("home remembered"), R::GetHomeCircuit(T, TEXT("Cargo")) == C::EFC_Plant);
    TestTrue(TEXT("route second device"), R::RouteDevice(T, TEXT("Panel"), TEXT("Fans"), C::EFC_Security));
    TestTrue(TEXT("previous device returned home"), !R::IsDevicePowered(T, TEXT("Cargo")) && R::AreFansRunning(T, TEXT("Fans")) && R::GetPatchedDevice(T) == FName(TEXT("Fans")));
    TestTrue(TEXT("same patch toggles home"), R::RouteDevice(T, TEXT("Panel"), TEXT("Fans"), C::EFC_Security));
    TestTrue(TEXT("undo clears metadata"), R::GetPatchedDevice(T).IsNone() && T.Routing.HomeCircuit == C::EFC_None && R::GetHomeCircuit(T, TEXT("Fans")) == C::EFC_Plant);
    TestFalse(TEXT("unoffered circuit rejected"), R::RouteDevice(T, TEXT("Panel"), TEXT("Cargo"), C::EFC_Doors));
    TestFalse(TEXT("unknown device rejected"), R::RouteDevice(T, TEXT("Panel"), TEXT("Unknown"), C::EFC_Security));
    R::SetDeviceCircuit(T, TEXT("Cargo"), C::EFC_Security);
    TestFalse(TEXT("unpatched already-home device no action"), R::CanRouteDevice(T, TEXT("Panel"), TEXT("Cargo"), C::EFC_Security));
    R::SetCircuitState(T, C::EFC_Security, B::Cut);
    TestFalse(TEXT("dead panel unusable"), R::CanUseRoutingPanel(T, TEXT("Panel")));
    // Switching destinations on the SAME patch must not replace its original home.
    FFacilityState Q = Base(); Panel(Q);
    TestTrue(TEXT("register another destination for cargo"), R::AddRoutingOption(Q, TEXT("Panel"), Route(TEXT("Cargo"), C::EFC_Doors)));
    TestTrue(TEXT("cargo patch first destination"), R::RouteDevice(Q, TEXT("Panel"), TEXT("Cargo"), C::EFC_Security));
    TestTrue(TEXT("cargo patch changes destination"), R::RouteDevice(Q, TEXT("Panel"), TEXT("Cargo"), C::EFC_Doors));
    TestTrue(TEXT("changed patch actually reached second destination"), R::GetDeviceCircuit(Q, TEXT("Cargo")) == C::EFC_Doors);
    TestTrue(TEXT("changed patch retains original plant home"), R::GetHomeCircuit(Q, TEXT("Cargo")) == C::EFC_Plant);
    FFacilityState Undo = Q;
    TestTrue(TEXT("undo changed patch"), R::RouteDevice(Undo, TEXT("Panel"), TEXT("Cargo"), C::EFC_Doors));
    TestTrue(TEXT("undo changed patch returns to original home"), R::GetDeviceCircuit(Undo, TEXT("Cargo")) == C::EFC_Plant && R::GetPatchedDevice(Undo).IsNone());
    TestTrue(TEXT("displace changed cargo patch with fans"), R::RouteDevice(Q, TEXT("Panel"), TEXT("Fans"), C::EFC_Security));
    TestTrue(TEXT("displacement restores original plant circuit"), R::GetDeviceCircuit(Q, TEXT("Cargo")) == C::EFC_Plant && !R::CanUseLift(Q, TEXT("Cargo")));
    TestTrue(TEXT("new fan patch remains independently powered"), R::AreFansRunning(Q, TEXT("Fans")) && R::GetHomeCircuit(Q, TEXT("Fans")) == C::EFC_Plant);
    return true;
}

// REQUIRED: duration, effective power, exhaustion, overload prerequisites and permanent disable.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(F0158Generator, "GameEngineBench.UE0164.Subsystem158.GeneratorLifecycle", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool F0158Generator::RunTest(const FString&)
{
    FFacilityState S = Base();
    R::SetCircuitState(S, C::EFC_Doors, B::Shorted);
    S.DeviceCircuits.Add(TEXT("Load"), C::EFC_Plant);
    TestTrue(TEXT("generator starts"), R::CanStartGenerator(S) && R::StartGenerator(S));
    TestEqual(TEXT("configured duration"), R::GetGeneratorStepsLeft(S), 3);
    TestTrue(TEXT("effective power differs from breaker"), R::IsDevicePowered(S, TEXT("Load")) && !R::IsBreakerOn(S, C::EFC_Plant));
    TestFalse(TEXT("short gets no backup power"), R::IsCircuitLive(S, C::EFC_Doors));
    TestFalse(TEXT("None never gets power"), R::IsCircuitLive(S, C::EFC_None));
    TestFalse(TEXT("cannot restart while running"), R::StartGenerator(S));
    TestFalse(TEXT("cannot overload without pry bar"), R::OverloadGenerator(S));
    for (int32 Expected = 2; Expected >= 0; --Expected)
    {
        TestTrue(TEXT("countdown reports change"), R::AdvanceStep(S));
        TestEqual(TEXT("countdown exact"), R::GetGeneratorStepsLeft(S), Expected);
    }
    TestTrue(TEXT("exhaustion loses backup, keeps live breaker"), !R::IsDevicePowered(S, TEXT("Load")) && R::IsCircuitLive(S, C::EFC_Security));
    TestTrue(TEXT("natural exhaustion can restart"), R::StartGenerator(S));
    R::GiveItem(S, I::PryBar);
    TestTrue(TEXT("overload available while running"), R::CanOverloadGenerator(S) && R::OverloadGenerator(S));
    TestTrue(TEXT("overloaded permanently off"), R::IsGeneratorOverloaded(S) && !R::IsGeneratorRunning(S) && !R::CanStartGenerator(S));
    TestTrue(TEXT("overload cuts live but preserves short"), !R::IsBreakerOn(S, C::EFC_Security) && R::GetCircuitState(S, C::EFC_Doors) == B::Shorted);
    TestFalse(TEXT("overload repeat no action"), R::OverloadGenerator(S));
    TestTrue(TEXT("panel usable after generator lost"), R::FlipBreaker(S, C::EFC_Plant) && R::IsCircuitLive(S, C::EFC_Plant));
    FFacilityState T = Base(); T.Config.GeneratorRunSteps = 0;
    TestTrue(TEXT("nonpositive duration clamps to one"), R::StartGenerator(T) && R::GetGeneratorStepsLeft(T) == 1);
    return true;
}

// REQUIRED: result booleans describe visible effects, not successful player intent.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(F0158Alarm, "GameEngineBench.UE0164.Subsystem158.AlarmActionResults", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool F0158Alarm::RunTest(const FString&)
{
    FFacilityState S = Base(); Camera(S, 4);
    TestFalse(TEXT("None alarm refused"), R::SetAlarmLevel(S, A::EAL_None));
    TestFalse(TEXT("quiet floor"), R::LowerAlarmLevelByOne(S));
    TestTrue(TEXT("sighting raises"), R::CameraSighting(S, TEXT("Camera")) && R::GetAlarmLevel(S) == A::EAL_Alerted);
    TestFalse(TEXT("first quiet bookkeeping is not a change"), R::AdvanceStep(S));
    TestEqual(TEXT("quiet count recorded"), S.QuietSteps, 1);
    TestFalse(TEXT("same alarm doesn't reset quiet"), R::SetAlarmLevel(S, A::EAL_Alerted));
    TestEqual(TEXT("same-level count retained"), S.QuietSteps, 1);
    TestTrue(TEXT("failed camera hack raises to lockdown"), R::HackCamera(S, TEXT("Camera")) && R::IsLockdown(S) && !R::IsCameraLooped(S, TEXT("Camera")));
    R::AdvanceStep(S); R::AdvanceStep(S);
    TestEqual(TEXT("two quiet steps"), S.QuietSteps, 2);
    TestFalse(TEXT("noise at ceiling returns false"), R::HackCamera(S, TEXT("Camera")));
    TestEqual(TEXT("noise still resets quiet"), S.QuietSteps, 0);
    TestFalse(TEXT("post-noise first step"), R::AdvanceStep(S));
    TestFalse(TEXT("post-noise second step"), R::AdvanceStep(S));
    TestTrue(TEXT("decay third step"), R::AdvanceStep(S) && R::GetAlarmLevel(S) == A::EAL_Alerted);
    S.DeviceCircuits.Add(TEXT("Console"), C::EFC_Security);
    R::SetCircuitState(S, C::EFC_Security, B::Cut);
    TestFalse(TEXT("dead console cannot silence"), R::SilenceAlarm(S, TEXT("Console")));
    R::SetCircuitState(S, C::EFC_Security, B::Live);
    TestTrue(TEXT("powered console lowers one level"), R::CanSilenceAlarm(S, TEXT("Console")) && R::SilenceAlarm(S, TEXT("Console")) && R::GetAlarmLevel(S) == A::EAL_Quiet);
    TestFalse(TEXT("silence at floor no action"), R::SilenceAlarm(S, TEXT("Console")));
    S.QuietSteps = 12;
    TestFalse(TEXT("quiet bookkeeping clearing reports no visible change"), R::AdvanceStep(S));
    TestEqual(TEXT("quiet baseline count zero"), S.QuietSteps, 0);
    S.Config.AlarmDecaySteps = 0; R::RaiseAlarmLevelByOne(S);
    TestTrue(TEXT("nonpositive decay lowers on first step"), R::AdvanceStep(S) && R::GetAlarmLevel(S) == A::EAL_Quiet);
    return true;
}

// REQUIRED: standing water, irreversible shorts, named-lock corrosion and drain precedence.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(F0158Flood, "GameEngineBench.UE0164.Subsystem158.FloodCorrosionAndDrain", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool F0158Flood::RunTest(const FString&)
{
    FFacilityState S = Base(); Tunnel(S);
    Door(S, TEXT("LockdownDoor"), EDoorKind::Lockdown, C::EFC_Doors);
    Door(S, TEXT("Unrelated"), EDoorKind::Standard, C::EFC_None, 3);
    Lift(S, TEXT("Elevator"), ELiftKind::Elevator, C::EFC_Doors);
    TestTrue(TEXT("flood starts"), R::FloodPlant(S) && R::IsPlantFlooded(S) && R::IsFloodLethal(S));
    TestFalse(TEXT("water step count alone not visible"), R::AdvanceStep(S));
    TestEqual(TEXT("first wet step"), R::GetFloodSteps(S), 1);
    TestFalse(TEXT("reflood does not reset count"), R::FloodPlant(S));
    TestEqual(TEXT("count preserved"), R::GetFloodSteps(S), 1);
    TestTrue(TEXT("second wet step shorts"), R::AdvanceStep(S));
    TestTrue(TEXT("only live circuits short"), R::GetCircuitState(S, C::EFC_Doors) == B::Shorted && R::GetCircuitState(S, C::EFC_Security) == B::Shorted && R::GetCircuitState(S, C::EFC_Plant) == B::Cut);
    TestTrue(TEXT("short settles doors/lifts and removes lethality"), R::IsDoorOpen(S, TEXT("LockdownDoor")) && R::GetLiftStop(S, TEXT("Elevator")) == ELiftStop::ELS_Bottom && !R::IsFloodLethal(S));
    TestFalse(TEXT("third step bookkeeping only"), R::AdvanceStep(S));
    TestTrue(TEXT("fourth step corrodes named lock"), R::AdvanceStep(S) && R::IsDoorLockCorroded(S, TEXT("Tunnel")) && !R::IsDoorLocked(S, TEXT("Tunnel")));
    TestFalse(TEXT("unrelated lock not corroded"), R::IsDoorLockCorroded(S, TEXT("Unrelated")));
    TestTrue(TEXT("corrosion does not open door"), !R::IsDoorOpen(S, TEXT("Tunnel")) && R::ToggleDoor(S, TEXT("Tunnel")));
    TestFalse(TEXT("already corroded no extra event"), R::AdvanceStep(S));
    TestFalse(TEXT("short not repairable by panel"), R::FlipBreaker(S, C::EFC_Doors));
    FFacilityState T = Base(); Tunnel(T); T.Circuits.Add(C::EFC_Plant, B::Live);
    T.Config.FloodShortSteps = 1; T.Config.CorrosionSteps = 1;
    R::FloodPlant(T);
    TestTrue(TEXT("fans drain first"), R::WouldFansDrainFlood(T) && R::AdvanceStep(T));
    TestTrue(TEXT("drained step neither shorts nor corrodes"), !R::IsPlantFlooded(T) && R::GetFloodSteps(T) == 0 && R::GetCircuitState(T, C::EFC_Doors) == B::Live && !R::IsDoorLockCorroded(T, TEXT("Tunnel")));
    R::GiveItem(T, I::PryBar);
    TestTrue(TEXT("valve floods"), R::CanOpenCoolantValve(T) && R::OpenCoolantValve(T));
    TestFalse(TEXT("open valve prevents fan drain"), R::WouldFansDrainFlood(T));
    TestTrue(TEXT("valve-fed step can short all live circuits"), R::AdvanceStep(T) && R::GetCircuitState(T, C::EFC_Plant) == B::Shorted);
    TestTrue(TEXT("close valve leaves water"), R::CanCloseCoolantValve(T) && R::CloseCoolantValve(T) && R::IsPlantFlooded(T));
    TestFalse(TEXT("close valve repeats no action"), R::CloseCoolantValve(T));
    return true;
}

// REQUIRED: sabotage prerequisites, once-only fire, alarm floor and repeatable pump after drain.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(F0158Sabotage, "GameEngineBench.UE0164.Subsystem158.RackSabotageAndPump", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool F0158Sabotage::RunTest(const FString&)
{
    FFacilityState S = Base(); Panel(S, 2); S.Flood.FansId = TEXT("Fans");
    S.DeviceCircuits.Add(TEXT("Rack"), C::EFC_Security);
    TestFalse(TEXT("valve requires pry bar"), R::OpenCoolantValve(S));
    TestFalse(TEXT("rack requires pry bar"), R::OverloadServerRack(S, TEXT("Rack")));
    TestTrue(TEXT("under-level pump raises alarm only"), R::RunCoolantPump(S, TEXT("Panel")) && !R::IsPlantFlooded(S) && R::GetAlarmLevel(S) == A::EAL_Alerted);
    R::GiveItem(S, I::ServerDrive);
    TestTrue(TEXT("capable pump floods"), R::CanRunCoolantPump(S, TEXT("Panel")) && R::RunCoolantPump(S, TEXT("Panel")) && R::IsPlantFlooded(S) && !R::IsCoolantValveOpen(S));
    TestFalse(TEXT("cannot pump standing water"), R::RunCoolantPump(S, TEXT("Panel")));
    R::RouteDevice(S, TEXT("Panel"), TEXT("Fans"), C::EFC_Security);
    TestTrue(TEXT("patched fans drain"), R::AdvanceStep(S) && !R::IsPlantFlooded(S));
    TestTrue(TEXT("pump can run after drain"), R::RunCoolantPump(S, TEXT("Panel")));
    R::GiveItem(S, I::PryBar); R::SetAlarmLevel(S, A::EAL_Lockdown); S.QuietSteps = 2;
    TestTrue(TEXT("rack overload available"), R::CanOverloadServerRack(S, TEXT("Rack")) && R::OverloadServerRack(S, TEXT("Rack")));
    TestTrue(TEXT("fire smoke and existing alarm retained"), R::IsServerRackOverloaded(S) && R::IsLabSmokeFilled(S) && R::IsLockdown(S) && S.QuietSteps == 0);
    TestFalse(TEXT("rack only once"), R::OverloadServerRack(S, TEXT("Rack")));
    R::SetCircuitState(S, C::EFC_Security, B::Cut);
    TestTrue(TEXT("smoke survives loss of rack power"), R::IsLabSmokeFilled(S));
    FFacilityState T = Base(); T.Inventory.Add(I::PryBar); T.DeviceCircuits.Add(TEXT("Rack"), C::EFC_Plant);
    TestFalse(TEXT("dead rack cannot overload"), R::OverloadServerRack(T, TEXT("Rack")));
    R::SetDeviceCircuit(T, TEXT("Rack"), C::EFC_Security);
    TestTrue(TEXT("quiet rack raises to alerted and floods"), R::OverloadServerRack(T, TEXT("Rack")) && R::GetAlarmLevel(T) == A::EAL_Alerted && R::IsPlantFlooded(T));
    return true;
}

// REQUIRED: move/drop versus quiet lift carriage and escape exactly once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(F0158Crates, "GameEngineBench.UE0164.Subsystem158.CratesAndEscapes", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool F0158Crates::RunTest(const FString&)
{
    FFacilityState S = Base(); Lift(S, TEXT("Cargo"), ELiftKind::CargoLift, C::EFC_Plant);
    FCrateState Cr; Cr.LiftId = TEXT("Cargo"); S.Crates.Add(TEXT("Crate"), Cr);
    TestTrue(TEXT("crate starts jammed"), R::IsCrateJammingShutter(S, TEXT("Crate")) && R::IsDockShutterJammed(S));
    TestFalse(TEXT("unpowered lift and no pry bar cannot move"), R::MoveCrate(S, TEXT("Crate")));
    R::SetDeviceCircuit(S, TEXT("Cargo"), C::EFC_Security);
    TestTrue(TEXT("powered lift permits movement"), R::CanMoveCrate(S, TEXT("Crate")) && R::MoveCrate(S, TEXT("Crate")) && !R::IsDockShutterJammed(S));
    TestFalse(TEXT("top car blocks dropping"), R::DropCrate(S, TEXT("Crate")));
    TestTrue(TEXT("car carries crate down"), R::CallLift(S, TEXT("Cargo"), ELiftStop::ELS_Bottom) && R::GetCratePosition(S, TEXT("Crate")) == ECratePosition::InPlant);
    TestTrue(TEXT("ride is not a noisy drop"), !R::WasCrateDropped(S, TEXT("Crate")) && R::GetAlarmLevel(S) == A::EAL_Quiet);
    TestFalse(TEXT("crate no longer movable"), R::MoveCrate(S, TEXT("Crate")));
    FFacilityState T = Base(); Lift(T, TEXT("Cargo"), ELiftKind::CargoLift, C::EFC_Plant);
    T.Lifts.FindChecked(TEXT("Cargo")).Stop = ELiftStop::ELS_Bottom;
    T.Crates.Add(TEXT("Crate"), Cr); T.Inventory.Add(I::PryBar);
    TestTrue(TEXT("pry bar moves without lift power"), R::MoveCrate(T, TEXT("Crate")));
    TestTrue(TEXT("open shaft allows drop"), R::CanDropCrate(T, TEXT("Crate")) && R::DropCrate(T, TEXT("Crate")) && R::WasCrateDropped(T, TEXT("Crate")) && R::GetAlarmLevel(T) == A::EAL_Alerted);
    TestFalse(TEXT("cannot drop twice"), R::DropCrate(T, TEXT("Crate")));
    Tunnel(T);
    TestFalse(TEXT("closed tunnel blocks escape"), R::Escape(T, EFacilityExit::ServiceTunnel));
    R::GiveItem(T, I::ServiceKey); R::ToggleDoor(T, TEXT("Tunnel"));
    R::SetCircuitState(T, C::EFC_Plant, B::Live);
    TestFalse(TEXT("running tunnel fans block escape"), R::CanEscape(T, EFacilityExit::ServiceTunnel));
    R::SetCircuitState(T, C::EFC_Plant, B::Cut);
    TestFalse(TEXT("loading dock is not implemented in this source version"), R::CanEscape(T, EFacilityExit::LoadingDock));
    TestTrue(TEXT("tunnel escape once"), R::CanEscape(T, EFacilityExit::ServiceTunnel) && R::Escape(T, EFacilityExit::ServiceTunnel) && R::HasEscaped(T) && R::GetEscapeExit(T) == EFacilityExit::ServiceTunnel);
    Door(T, TEXT("Gate"), EDoorKind::Gate, C::EFC_Doors); T.Doors.FindChecked(TEXT("Gate")).bOpen = true;
    TestFalse(TEXT("escaped state blocks other exit"), R::Escape(T, EFacilityExit::MainGate));
    FFacilityState G = Base(); Door(G, TEXT("Gate"), EDoorKind::Gate, C::EFC_Doors); G.Doors.FindChecked(TEXT("Gate")).bOpen = true;
    TestTrue(TEXT("open gate needs no exit registration"), R::Escape(G, EFacilityExit::MainGate));
    // Multiple shafts expose cross-device movement that a single-crate fixture cannot.
    FFacilityState Many = Base();
    Lift(Many, TEXT("LiftA"), ELiftKind::CargoLift, C::EFC_Security);
    Lift(Many, TEXT("LiftB"), ELiftKind::CargoLift, C::EFC_Security);
    FCrateState AHead; AHead.LiftId = TEXT("LiftA"); AHead.Position = ECratePosition::AtShaftHead;
    Many.Crates.Add(TEXT("AHead1"), AHead); Many.Crates.Add(TEXT("AHead2"), AHead);
    FCrateState BHead = AHead; BHead.LiftId = TEXT("LiftB"); Many.Crates.Add(TEXT("BHead"), BHead);
    FCrateState Jam = AHead; Jam.Position = ECratePosition::JammingShutter; Many.Crates.Add(TEXT("AJam"), Jam);
    TestTrue(TEXT("first independent lift descends"), R::CallLift(Many, TEXT("LiftA"), ELiftStop::ELS_Bottom));
    TestTrue(TEXT("all matching shaft-head crates ride down"), R::GetCratePosition(Many, TEXT("AHead1")) == ECratePosition::InPlant && R::GetCratePosition(Many, TEXT("AHead2")) == ECratePosition::InPlant);
    TestTrue(TEXT("other shaft crate is untouched"), R::GetCratePosition(Many, TEXT("BHead")) == ECratePosition::AtShaftHead && R::GetLiftStop(Many, TEXT("LiftB")) == ELiftStop::ELS_Top);
    TestTrue(TEXT("jamming crate is not aboard"), R::IsCrateJammingShutter(Many, TEXT("AJam")) && R::IsDockShutterJammed(Many));
    TestTrue(TEXT("independent rides stay quiet and not dropped"), R::GetAlarmLevel(Many) == A::EAL_Quiet && !R::WasCrateDropped(Many, TEXT("AHead1")) && !R::WasCrateDropped(Many, TEXT("AHead2")));
    TestFalse(TEXT("same-stop call cannot sweep another shaft"), R::CallLift(Many, TEXT("LiftA"), ELiftStop::ELS_Bottom));
    TestTrue(TEXT("second independent lift descends"), R::CallLift(Many, TEXT("LiftB"), ELiftStop::ELS_Bottom));
    TestTrue(TEXT("second shaft crate now arrives"), R::GetCratePosition(Many, TEXT("BHead")) == ECratePosition::InPlant);
    return true;
}

// REQUIRED: same-tick ordering and copied hypothetical states are independent of live state.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(F0158Integrated, "GameEngineBench.UE0164.Subsystem158.IntegratedRouteAndClock", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool F0158Integrated::RunTest(const FString&)
{
    FFacilityState Live = Base(); Panel(Live); Tunnel(Live);
    Door(Live, TEXT("SecurityDoor"), EDoorKind::Lockdown, C::EFC_Doors);
    Lift(Live, TEXT("Elevator"), ELiftKind::Elevator, C::EFC_Doors);
    Live.Config.GeneratorRunSteps = 2; Live.Config.FloodShortSteps = 1; Live.Config.CorrosionSteps = 2;
    TestTrue(TEXT("route cargo while fans stay off"), R::RouteDevice(Live, TEXT("Panel"), TEXT("Cargo"), C::EFC_Security));
    FFacilityState Hypothetical = Live;
    TestTrue(TEXT("start generator in copied state"), R::StartGenerator(Hypothetical));
    TestTrue(TEXT("copy has power independently"), R::AreFansRunning(Hypothetical, TEXT("Fans")) && !R::AreFansRunning(Live, TEXT("Fans")) && R::GetGeneratorStepsLeft(Live) == 0);
    R::GiveItem(Hypothetical, I::PryBar); R::OpenCoolantValve(Hypothetical);
    TestTrue(TEXT("running backup carries cut plant into flood short"), R::AdvanceStep(Hypothetical) && R::GetCircuitState(Hypothetical, C::EFC_Plant) == B::Shorted);
    TestTrue(TEXT("short composition settles access and transport"), R::IsDoorFailedOpen(Hypothetical, TEXT("SecurityDoor")) && R::GetLiftStop(Hypothetical, TEXT("Elevator")) == ELiftStop::ELS_Bottom && !R::IsFloodLethal(Hypothetical));
    TestTrue(TEXT("next step exhausts generator and corrodes"), R::AdvanceStep(Hypothetical) && !R::IsGeneratorRunning(Hypothetical) && R::IsDoorLockCorroded(Hypothetical, TEXT("Tunnel")));
    TestTrue(TEXT("corroded tunnel permits a composed escape"), R::ToggleDoor(Hypothetical, TEXT("Tunnel")) && R::Escape(Hypothetical, EFacilityExit::ServiceTunnel));
    TestTrue(TEXT("original branch remains unchanged"), !R::HasEscaped(Live) && !R::IsPlantFlooded(Live) && R::GetCircuitState(Live, C::EFC_Doors) == B::Live && R::GetPatchedDevice(Live) == FName(TEXT("Cargo")));
    // Order sensitivity: backup expiration happens BEFORE the fans/flood decision.
    FFacilityState Expiring = Base(); Tunnel(Expiring);
    Expiring.Circuits.Add(C::EFC_Doors, B::Cut); Expiring.Circuits.Add(C::EFC_Security, B::Cut);
    Expiring.GeneratorStepsLeft = 1; Expiring.Config.FloodShortSteps = 1; Expiring.Config.CorrosionSteps = 1;
    Expiring.Flood.bFlooded = true;
    TestTrue(TEXT("before step backup fans would drain"), R::WouldFansDrainFlood(Expiring));
    TestTrue(TEXT("expiry is visible"), R::AdvanceStep(Expiring));
    TestTrue(TEXT("expired backup cannot drain or short cut circuits, but flood corrodes"), R::IsPlantFlooded(Expiring) && R::GetFloodSteps(Expiring) == 1 && R::GetCircuitState(Expiring, C::EFC_Plant) == B::Cut && R::IsDoorLockCorroded(Expiring, TEXT("Tunnel")));
    return true;
}
#endif

#include "Facility/FacilityRules.h"

/** The circuits on the breaker panel. None is not a breaker. */
static constexpr EFacilityCircuit BreakerCircuits[] =
{
	EFacilityCircuit::EFC_Doors,
	EFacilityCircuit::EFC_Security,
	EFacilityCircuit::EFC_Plant,
};

/**
 * Puts a lockdown door or the gate where its circuit and the alarm leave it, and returns true if it
 * moved. A lockdown door is closed while its circuit carries no power or lockdown is called, and open
 * while its circuit is shorted: the short wins, since a shorted circuit has no power to hold the door shut,
 * lockdown or not. The gate is closed while lockdown is called, whatever its circuit is doing, and
 * never falls open. While nothing holds it, a door stays wherever it was, and a door the pry bar
 * forced stays open whatever would hold the others. Every mutation that changes what a circuit
 * delivers or moves the alarm runs this over the doors it affects, so sealing and failing open are
 * never something a listener has to remember to do. IsDoorSealed and IsDoorFailedOpen answer from
 * the same facts, so a door reads as sealed exactly when this shut it.
 */
static bool SettleDoor(FFacilityState& State, const FName DoorId, FDoorState& Door)
{
	if (Door.Kind == EDoorKind::Standard)
	{
		return false;
	}

	// Pried open. No circuit and no alarm level shuts it again.
	if (Door.bForced)
	{
		return false;
	}

	// Set once something holds the door. Left unset, the door stays where it is.
	TOptional<bool> bWantOpen;

	if (Door.Kind == EDoorKind::Lockdown)
	{
		// A door whose device has not registered yet has no circuit to follow; SetDeviceCircuit settles
		// it when the device does. A device wired to None registers as None, which carries nothing below.
		if (const EFacilityCircuit* Circuit = State.DeviceCircuits.Find(DoorId))
		{
			if (FFacilityRules::GetCircuitState(State, *Circuit) == ECircuitState::Shorted)
			{
				bWantOpen = true;
			}
			else if (FFacilityRules::IsCircuitLive(State, *Circuit) == false)
			{
				// Cut, and the generator not carrying it.
				bWantOpen = false;
			}
		}
	}

	// Lockdown closes whatever the circuit left alone: a lockdown door on a live circuit, and the gate.
	if (bWantOpen.IsSet() == false && FFacilityRules::IsLockdown(State))
	{
		bWantOpen = false;
	}

	if (bWantOpen.IsSet() == false || Door.bOpen == bWantOpen.GetValue())
	{
		return false;
	}

	Door.bOpen = bWantOpen.GetValue();
	return true;
}

/** SettleDoor over every door. Returns true if any moved. */
static bool SettleDoors(FFacilityState& State)
{
	bool bChanged = false;
	for (TPair<FName, FDoorState>& Entry : State.Doors)
	{
		if (SettleDoor(State, Entry.Key, Entry.Value))
		{
			bChanged = true;
		}
	}
	return bChanged;
}

/**
 * Parks the elevator on Level 1, its bottom stop, while its circuit is dead or lockdown is called, and
 * returns true if the car moved. The cargo lift has no such rule: unpowered, it stays where it is. A
 * lift whose device has not registered yet has no circuit to follow; SetDeviceCircuit settles it when
 * the device does. Every mutation that changes what a circuit delivers or moves the alarm runs this
 * over the lifts it affects, the way SettleDoor keeps the lockdown doors honest.
 */
static bool SettleLift(FFacilityState& State, const FName LiftId, FLiftState& Lift)
{
	if (Lift.Kind != ELiftKind::Elevator || Lift.Stop == ELiftStop::ELS_Bottom)
	{
		return false;
	}

	const EFacilityCircuit* Circuit = State.DeviceCircuits.Find(LiftId);
	const bool bCircuitDead = Circuit != nullptr && FFacilityRules::IsCircuitLive(State, *Circuit) == false;
	if (bCircuitDead == false && FFacilityRules::IsLockdown(State) == false)
	{
		return false;
	}

	Lift.Stop = ELiftStop::ELS_Bottom;
	return true;
}

/** SettleLift over every lift. Returns true if any moved. */
static bool SettleLifts(FFacilityState& State)
{
	bool bChanged = false;
	for (TPair<FName, FLiftState>& Entry : State.Lifts)
	{
		if (SettleLift(State, Entry.Key, Entry.Value))
		{
			bChanged = true;
		}
	}
	return bChanged;
}

void FFacilityRules::SetInitialState(FFacilityState& State)
{
	State.Circuits.Reset();
	State.Circuits.Add(EFacilityCircuit::EFC_Doors, ECircuitState::Live);
	State.Circuits.Add(EFacilityCircuit::EFC_Security, ECircuitState::Live);
	State.Circuits.Add(EFacilityCircuit::EFC_Plant, ECircuitState::Cut);
	State.AlarmLevel = EAlarmLevel::EAL_Quiet;
	State.QuietSteps = 0;
	State.GeneratorStepsLeft = 0;
	State.bGeneratorOverloaded = false;
	State.Flood.bFlooded = false;
	State.Flood.Steps = 0;
	State.bCoolantValveOpen = false;
	State.bServerRackOverloaded = false;
}

ECircuitState FFacilityRules::GetCircuitState(const FFacilityState& State, const EFacilityCircuit Circuit)
{
	const ECircuitState* Found = State.Circuits.Find(Circuit);
	return Found != nullptr ? *Found : ECircuitState::Cut;
}

bool FFacilityRules::IsBreakerOn(const FFacilityState& State, const EFacilityCircuit Circuit)
{
	return Circuit != EFacilityCircuit::EFC_None && GetCircuitState(State, Circuit) == ECircuitState::Live;
}

bool FFacilityRules::IsCircuitLive(const FFacilityState& State, const EFacilityCircuit Circuit)
{
	if (Circuit == EFacilityCircuit::EFC_None)
	{
		return false;
	}

	// The generator carries a cut circuit while it runs. A shorted one it cannot: the fault is in the
	// wiring, not the supply.
	switch (GetCircuitState(State, Circuit))
	{
	case ECircuitState::Live:
		return true;

	case ECircuitState::Cut:
		return IsGeneratorRunning(State);

	default:
		return false;
	}
}

EFacilityCircuit FFacilityRules::GetDeviceCircuit(const FFacilityState& State, const FName DeviceId)
{
	const EFacilityCircuit* Found = State.DeviceCircuits.Find(DeviceId);
	return Found != nullptr ? *Found : EFacilityCircuit::EFC_None;
}

bool FFacilityRules::IsDevicePowered(const FFacilityState& State, const FName DeviceId)
{
	return IsCircuitLive(State, GetDeviceCircuit(State, DeviceId));
}

bool FFacilityRules::IsGeneratorRunning(const FFacilityState& State)
{
	return State.GeneratorStepsLeft > 0;
}

int32 FFacilityRules::GetGeneratorStepsLeft(const FFacilityState& State)
{
	return State.GeneratorStepsLeft;
}

bool FFacilityRules::IsGeneratorOverloaded(const FFacilityState& State)
{
	return State.bGeneratorOverloaded;
}

bool FFacilityRules::CanStartGenerator(const FFacilityState& State)
{
	return IsGeneratorRunning(State) == false && IsGeneratorOverloaded(State) == false;
}

bool FFacilityRules::CanOverloadGenerator(const FFacilityState& State)
{
	// Rigged while it runs: a generator that is off has nothing to overload.
	return IsGeneratorRunning(State) && IsGeneratorOverloaded(State) == false && HasItem(State, EFacilityItem::PryBar);
}

ELiftStop FFacilityRules::GetLiftStop(const FFacilityState& State, const FName LiftId)
{
	const FLiftState* Found = State.Lifts.Find(LiftId);
	return Found != nullptr ? Found->Stop : ELiftStop::ELS_Bottom;
}

ELiftStop FFacilityRules::OtherLiftStop(const ELiftStop Stop)
{
	return Stop == ELiftStop::ELS_Top ? ELiftStop::ELS_Bottom : ELiftStop::ELS_Top;
}

ELiftKind FFacilityRules::GetLiftKind(const FFacilityState& State, const FName LiftId)
{
	const FLiftState* Found = State.Lifts.Find(LiftId);
	return Found != nullptr ? Found->Kind : ELiftKind::CargoLift;
}

bool FFacilityRules::IsLiftHacked(const FFacilityState& State, const FName LiftId)
{
	const FLiftState* Found = State.Lifts.Find(LiftId);
	return Found != nullptr && Found->bHacked;
}

ELiftObstacle FFacilityRules::GetLiftObstacle(const FFacilityState& State, const FName LiftId)
{
	if (IsLiftHacked(State, LiftId))
	{
		return ELiftObstacle::Hacked;
	}

	if (IsDevicePowered(State, LiftId) == false)
	{
		return ELiftObstacle::NoPower;
	}

	// Lockdown stops the elevator, the way SettleLift parks it. The cargo lift runs on.
	if (GetLiftKind(State, LiftId) == ELiftKind::Elevator && IsLockdown(State))
	{
		return ELiftObstacle::Lockdown;
	}

	return ELiftObstacle::None;
}

bool FFacilityRules::CanUseLift(const FFacilityState& State, const FName LiftId)
{
	return GetLiftObstacle(State, LiftId) == ELiftObstacle::None;
}

bool FFacilityRules::CanHackLift(const FFacilityState& State, const FName LiftId)
{
	// A target needs power to be hackable, and a stopped lift has nothing left to stop.
	return State.Lifts.Contains(LiftId)
		&& GetHackLevel(State, LiftId) > 0
		&& IsDevicePowered(State, LiftId)
		&& IsLiftHacked(State, LiftId) == false;
}

int32 FFacilityRules::KeycardLevelOf(const EFacilityItem Item)
{
	switch (Item)
	{
	case EFacilityItem::Keycard2:
		return 2;

	case EFacilityItem::Keycard3:
		return 3;

	default:
		return 0;
	}
}

bool FFacilityRules::HasItem(const FFacilityState& State, const EFacilityItem Item)
{
	return Item != EFacilityItem::None && State.Inventory.Contains(Item);
}

int32 FFacilityRules::GetKeycardLevel(const FFacilityState& State)
{
	int32 Highest = 0;
	for (const EFacilityItem Item : State.Inventory)
	{
		Highest = FMath::Max(Highest, KeycardLevelOf(Item));
	}
	return Highest;
}

EFacilityItem FFacilityRules::GetPickupItem(const FFacilityState& State, const FName PickupId)
{
	const FPickupState* Found = State.Pickups.Find(PickupId);
	return Found != nullptr ? Found->Item : EFacilityItem::None;
}

bool FFacilityRules::IsPickupTaken(const FFacilityState& State, const FName PickupId)
{
	const FPickupState* Found = State.Pickups.Find(PickupId);
	return Found == nullptr || Found->bTaken;
}

bool FFacilityRules::CanTakePickup(const FFacilityState& State, const FName PickupId)
{
	return IsPickupTaken(State, PickupId) == false && GetPickupItem(State, PickupId) != EFacilityItem::None;
}

int32 FFacilityRules::GetHackLevel(const FFacilityState& State, const FName DeviceId)
{
	const int32* Found = State.HackLevels.Find(DeviceId);
	return Found != nullptr ? *Found : 0;
}

int32 FFacilityRules::GetHandheldLevel(const FFacilityState& State)
{
	// The drive raises the handheld; it never lowers it, whatever the config says the two levels are.
	const int32 Base = State.Config.HandheldLevel;
	return IsServerDrivePulled(State) ? FMath::Max(Base, State.Config.ServerDriveHandheldLevel) : Base;
}

bool FFacilityRules::WouldHackSucceed(const FFacilityState& State, const int32 TargetLevel)
{
	return TargetLevel > 0 && GetHandheldLevel(State) >= TargetLevel;
}

bool FFacilityRules::IsServerDrivePulled(const FFacilityState& State)
{
	return HasItem(State, EFacilityItem::ServerDrive);
}

bool FFacilityRules::IsDoorOpen(const FFacilityState& State, const FName DoorId)
{
	const FDoorState* Found = State.Doors.Find(DoorId);
	return Found != nullptr && Found->bOpen;
}

FName FFacilityRules::FindDoorOfKind(const FFacilityState& State, const EDoorKind Kind)
{
	for (const TPair<FName, FDoorState>& Entry : State.Doors)
	{
		if (Entry.Value.Kind == Kind)
		{
			return Entry.Key;
		}
	}
	return NAME_None;
}

bool FFacilityRules::IsDoorForced(const FFacilityState& State, const FName DoorId)
{
	const FDoorState* Found = State.Doors.Find(DoorId);
	return Found != nullptr && Found->bForced;
}

bool FFacilityRules::IsDoorSealed(const FFacilityState& State, const FName DoorId)
{
	const FDoorState* Door = State.Doors.Find(DoorId);
	if (Door == nullptr)
	{
		return false;
	}

	// Pried open, and nothing holds a forced door shut, the way SettleDoor leaves it.
	if (Door->bForced)
	{
		return false;
	}

	if (Door->Kind == EDoorKind::Gate)
	{
		// Sealed under lockdown whatever card is swiped, and whatever its circuit is doing.
		return IsLockdown(State);
	}

	if (Door->Kind != EDoorKind::Lockdown)
	{
		return false;
	}

	// Its circuit first, the way SettleDoor moves it: a short fails it open with nothing left to hold
	// it, lockdown or not, and a circuit carrying no power seals it. On a powered circuit lockdown is
	// what seals it.
	const EFacilityCircuit Circuit = GetDeviceCircuit(State, DoorId);
	if (GetCircuitState(State, Circuit) == ECircuitState::Shorted)
	{
		return false;
	}

	if (IsCircuitLive(State, Circuit) == false)
	{
		return true;
	}

	return IsLockdown(State);
}

bool FFacilityRules::IsDoorFailedOpen(const FFacilityState& State, const FName DoorId)
{
	const FDoorState* Door = State.Doors.Find(DoorId);
	if (Door == nullptr || Door->Kind != EDoorKind::Lockdown)
	{
		return false;
	}

	return GetCircuitState(State, GetDeviceCircuit(State, DoorId)) == ECircuitState::Shorted;
}

int32 FFacilityRules::GetDoorLockLevel(const FFacilityState& State, const FName DoorId)
{
	const FDoorState* Door = State.Doors.Find(DoorId);
	return Door != nullptr ? Door->LockLevel : 0;
}

EFacilityItem FFacilityRules::GetDoorKeyItem(const FFacilityState& State, const FName DoorId)
{
	const FDoorState* Door = State.Doors.Find(DoorId);
	return Door != nullptr ? Door->KeyItem : EFacilityItem::None;
}

bool FFacilityRules::IsDoorLockCorroded(const FFacilityState& State, const FName DoorId)
{
	const FDoorState* Door = State.Doors.Find(DoorId);
	return Door != nullptr && Door->bLockCorroded;
}

bool FFacilityRules::IsDoorLocked(const FFacilityState& State, const FName DoorId)
{
	// The lock holds a closed door only. Closing never needs a card or a key.
	const FDoorState* Door = State.Doors.Find(DoorId);
	if (Door == nullptr || Door->bOpen)
	{
		return false;
	}

	// A lock the flood ate through holds nothing, whatever it took before.
	if (Door->bLockCorroded)
	{
		return false;
	}

	const bool bCardLock = Door->LockLevel > 0;
	const bool bKeyLock = Door->KeyItem != EFacilityItem::None;
	if (bCardLock == false && bKeyLock == false)
	{
		return false;
	}

	// A door that takes both opens to either.
	const bool bCardOpens = bCardLock && GetKeycardLevel(State) >= Door->LockLevel;
	const bool bKeyOpens = bKeyLock && HasItem(State, Door->KeyItem);
	return bCardOpens == false && bKeyOpens == false;
}

EDoorObstacle FFacilityRules::GetDoorObstacle(const FFacilityState& State, const FName DoorId)
{
	if (IsDoorForced(State, DoorId))
	{
		return EDoorObstacle::Forced;
	}

	if (IsDoorSealed(State, DoorId))
	{
		return EDoorObstacle::Sealed;
	}

	if (IsDoorFailedOpen(State, DoorId))
	{
		return EDoorObstacle::FailedOpen;
	}

	// A door on a circuit moves on that circuit's power. A door on none is mechanical.
	const EFacilityCircuit Circuit = GetDeviceCircuit(State, DoorId);
	if (Circuit != EFacilityCircuit::EFC_None && IsCircuitLive(State, Circuit) == false)
	{
		return EDoorObstacle::NoPower;
	}

	if (IsDoorLocked(State, DoorId))
	{
		return EDoorObstacle::Locked;
	}

	return EDoorObstacle::None;
}

bool FFacilityRules::CanOpenDoor(const FFacilityState& State, const FName DoorId)
{
	return IsDoorOpen(State, DoorId) == false && GetDoorObstacle(State, DoorId) == EDoorObstacle::None;
}

bool FFacilityRules::CanCloseDoor(const FFacilityState& State, const FName DoorId)
{
	return IsDoorOpen(State, DoorId) && GetDoorObstacle(State, DoorId) == EDoorObstacle::None;
}

bool FFacilityRules::CanForceDoor(const FFacilityState& State, const FName DoorId)
{
	const FDoorState* Door = State.Doors.Find(DoorId);
	if (Door == nullptr || Door->bOpen || Door->Kind == EDoorKind::Gate || HasItem(State, EFacilityItem::PryBar) == false)
	{
		return false;
	}

	// The pry bar gets past whatever holds a closed door. A door nothing holds is opened by hand, quietly.
	switch (GetDoorObstacle(State, DoorId))
	{
	case EDoorObstacle::Sealed:
	case EDoorObstacle::NoPower:
	case EDoorObstacle::Locked:
		return true;

	default:
		return false;
	}
}

bool FFacilityRules::CanHackDoor(const FFacilityState& State, const FName DoorId)
{
	// The gate is not a handheld target; its controller is. See CanHackGateController.
	const FDoorState* Door = State.Doors.Find(DoorId);
	if (Door == nullptr || Door->Kind == EDoorKind::Gate)
	{
		return false;
	}

	// A target needs power to be hackable, and the hack stands in for the card or key alone: only the
	// lock may be what holds the door. A mechanical door on no circuit is never powered, so never hacked.
	return GetHackLevel(State, DoorId) > 0
		&& IsDevicePowered(State, DoorId)
		&& GetDoorObstacle(State, DoorId) == EDoorObstacle::Locked;
}

bool FFacilityRules::IsDoorLoudToOpen(const FFacilityState& State, const FName DoorId)
{
	const FDoorState* Door = State.Doors.Find(DoorId);
	return Door != nullptr && Door->Kind == EDoorKind::Gate && State.AlarmLevel >= EAlarmLevel::EAL_Alerted;
}

bool FFacilityRules::AreFansRunning(const FFacilityState& State, const FName FansId)
{
	return IsDevicePowered(State, FansId);
}

bool FFacilityRules::AreLightsOn(const FFacilityState& State, const FName LightsId)
{
	return IsDevicePowered(State, LightsId);
}

bool FFacilityRules::IsCameraLooped(const FFacilityState& State, const FName CameraId)
{
	const FCameraState* Found = State.Cameras.Find(CameraId);
	return Found != nullptr && Found->bLooped;
}

bool FFacilityRules::IsCameraWatching(const FFacilityState& State, const FName CameraId)
{
	const FCameraState* Camera = State.Cameras.Find(CameraId);
	if (Camera == nullptr || Camera->bLooped)
	{
		return false;
	}

	// Pulling the server drive blinds the camera while the alarm stays armed; the rack stays on SECURITY.
	if (IsServerDrivePulled(State))
	{
		return false;
	}

	// SECURITY powers it, and it sees nothing in the dark.
	return IsDevicePowered(State, CameraId) && AreLightsOn(State, Camera->LightsId);
}

bool FFacilityRules::CanHackCamera(const FFacilityState& State, const FName CameraId)
{
	// A target needs power to be hackable, and a looped camera has nothing left to loop.
	return State.Cameras.Contains(CameraId)
		&& GetHackLevel(State, CameraId) > 0
		&& IsDevicePowered(State, CameraId)
		&& IsCameraLooped(State, CameraId) == false;
}

bool FFacilityRules::CanHackGateController(const FFacilityState& State, const FName ControllerId)
{
	if (GetHackLevel(State, ControllerId) <= 0 || IsDevicePowered(State, ControllerId) == false)
	{
		return false;
	}

	// The controller stands in for the card and nothing else: only the lock may hold the gate. A sealed
	// or unpowered gate stays shut, and an open one needs nothing.
	const FName GateId = FindDoorOfKind(State, EDoorKind::Gate);
	return GateId.IsNone() == false && GetDoorObstacle(State, GateId) == EDoorObstacle::Locked;
}

/** The job as the panel stores it: the pump names no device and no circuit, whatever the switch had left in them. */
static FRoutingOption NormalizeRoutingOption(const FRoutingOption& Option)
{
	FRoutingOption Stored = Option;
	if (Stored.Action == ERoutingAction::CoolantPump)
	{
		Stored.DeviceId = NAME_None;
		Stored.Circuit = EFacilityCircuit::EFC_None;
	}
	return Stored;
}

/** True when a switch registered the job. */
static bool HasRoutingOption(const FFacilityState& State, const FRoutingOption& Option)
{
	return State.Routing.Options.Contains(NormalizeRoutingOption(Option));
}

bool FFacilityRules::CanUseRoutingPanel(const FFacilityState& State, const FName PanelId)
{
	// A target needs power to be hackable, and only the panel the switches registered is a target at all.
	return PanelId.IsNone() == false
		&& State.Routing.PanelId == PanelId
		&& GetHackLevel(State, PanelId) > 0
		&& IsDevicePowered(State, PanelId);
}

FName FFacilityRules::GetPatchedDevice(const FFacilityState& State)
{
	return State.Routing.PatchedDevice;
}

bool FFacilityRules::IsDevicePatched(const FFacilityState& State, const FName DeviceId)
{
	return DeviceId.IsNone() == false && State.Routing.PatchedDevice == DeviceId;
}

EFacilityCircuit FFacilityRules::GetHomeCircuit(const FFacilityState& State, const FName DeviceId)
{
	return IsDevicePatched(State, DeviceId) ? State.Routing.HomeCircuit : GetDeviceCircuit(State, DeviceId);
}

bool FFacilityRules::CanRouteDevice(const FFacilityState& State, const FName PanelId, const FName DeviceId, const EFacilityCircuit Circuit)
{
	if (CanUseRoutingPanel(State, PanelId) == false)
	{
		return false;
	}

	// Only a job a switch registered, on a device the facility knows.
	FRoutingOption Job;
	Job.Action = ERoutingAction::RouteDevice;
	Job.DeviceId = DeviceId;
	Job.Circuit = Circuit;
	if (HasRoutingOption(State, Job) == false || State.DeviceCircuits.Contains(DeviceId) == false)
	{
		return false;
	}

	// A device on the circuit already is only something to do when the panel put it there: then the
	// switch sends it home.
	return GetDeviceCircuit(State, DeviceId) != Circuit || IsDevicePatched(State, DeviceId);
}

bool FFacilityRules::CanRunCoolantPump(const FFacilityState& State, const FName PanelId)
{
	FRoutingOption Job;
	Job.Action = ERoutingAction::CoolantPump;

	// More water on standing water changes nothing, so a flooded floor leaves the pump nothing to do.
	return CanUseRoutingPanel(State, PanelId)
		&& HasRoutingOption(State, Job)
		&& IsPlantFlooded(State) == false;
}

bool FFacilityRules::IsPlantFlooded(const FFacilityState& State)
{
	return State.Flood.bFlooded;
}

int32 FFacilityRules::GetFloodSteps(const FFacilityState& State)
{
	return State.Flood.Steps;
}

bool FFacilityRules::IsFloodLethal(const FFacilityState& State)
{
	if (State.Flood.bFlooded == false)
	{
		return false;
	}

	// Conductive: any circuit carrying power makes the water lethal, the generator's power included.
	for (const EFacilityCircuit Circuit : BreakerCircuits)
	{
		if (IsCircuitLive(State, Circuit))
		{
			return true;
		}
	}
	return false;
}

bool FFacilityRules::WouldFansDrainFlood(const FFacilityState& State)
{
	// The fans drain standing water, but not while the valve pours more in.
	return State.Flood.bFlooded && State.bCoolantValveOpen == false && AreFansRunning(State, State.Flood.FansId);
}

bool FFacilityRules::IsCoolantValveOpen(const FFacilityState& State)
{
	return State.bCoolantValveOpen;
}

bool FFacilityRules::CanOpenCoolantValve(const FFacilityState& State)
{
	// Seized: it takes the pry bar every time.
	return State.bCoolantValveOpen == false && HasItem(State, EFacilityItem::PryBar);
}

bool FFacilityRules::CanCloseCoolantValve(const FFacilityState& State)
{
	return State.bCoolantValveOpen;
}

bool FFacilityRules::IsServerRackOverloaded(const FFacilityState& State)
{
	return State.bServerRackOverloaded;
}

bool FFacilityRules::IsLabSmokeFilled(const FFacilityState& State)
{
	// The smoke is the fire's, and the fire burns for the attempt.
	return State.bServerRackOverloaded;
}

bool FFacilityRules::CanOverloadServerRack(const FFacilityState& State, const FName RackId)
{
	// A rack with no power has nothing to overload, and a burning one is done.
	return State.bServerRackOverloaded == false
		&& HasItem(State, EFacilityItem::PryBar)
		&& IsDevicePowered(State, RackId);
}

ECratePosition FFacilityRules::GetCratePosition(const FFacilityState& State, const FName CrateId)
{
	const FCrateState* Found = State.Crates.Find(CrateId);
	return Found != nullptr ? Found->Position : ECratePosition::InPlant;
}

bool FFacilityRules::IsCrateJammingShutter(const FFacilityState& State, const FName CrateId)
{
	return GetCratePosition(State, CrateId) == ECratePosition::JammingShutter;
}

bool FFacilityRules::IsDockShutterJammed(const FFacilityState& State)
{
	for (const TPair<FName, FCrateState>& Entry : State.Crates)
	{
		if (Entry.Value.Position == ECratePosition::JammingShutter)
		{
			return true;
		}
	}
	return false;
}

bool FFacilityRules::WasCrateDropped(const FFacilityState& State, const FName CrateId)
{
	const FCrateState* Found = State.Crates.Find(CrateId);
	return Found != nullptr && Found->bDropped;
}

bool FFacilityRules::CanMoveCrate(const FFacilityState& State, const FName CrateId)
{
	const FCrateState* Crate = State.Crates.Find(CrateId);
	if (Crate == nullptr || Crate->Position != ECratePosition::JammingShutter)
	{
		return false;
	}

	// The pry bar levers it, or the running cargo lift shifts it. The neutral character as a second
	// pair of hands joins here when they exist.
	return HasItem(State, EFacilityItem::PryBar) || CanUseLift(State, Crate->LiftId);
}

bool FFacilityRules::CanDropCrate(const FFacilityState& State, const FName CrateId)
{
	const FCrateState* Crate = State.Crates.Find(CrateId);
	if (Crate == nullptr || Crate->Position != ECratePosition::AtShaftHead)
	{
		return false;
	}

	// The shaft is open below only while the car is at the bottom. With the car up, the crate is on it.
	return State.Lifts.Contains(Crate->LiftId) && GetLiftStop(State, Crate->LiftId) == ELiftStop::ELS_Bottom;
}

bool FFacilityRules::HasEscaped(const FFacilityState& State)
{
	return State.EscapedThrough != EFacilityExit::None;
}

EFacilityExit FFacilityRules::GetEscapeExit(const FFacilityState& State)
{
	return State.EscapedThrough;
}

const FExitState* FFacilityRules::FindExit(const FFacilityState& State, const EFacilityExit Exit)
{
	return State.Exits.Find(Exit);
}

bool FFacilityRules::CanEscape(const FFacilityState& State, const EFacilityExit Exit)
{
	// The win happens once. Nothing lets the player out twice.
	if (HasEscaped(State))
	{
		return false;
	}

	switch (Exit)
	{
	case EFacilityExit::MainGate:
		// A card, the console or a hack opened it; either way the player walks out through an open gate.
		return IsDoorOpen(State, FindDoorOfKind(State, EDoorKind::Gate));

	case EFacilityExit::LoadingDock:
		// The shutter, its motor and the crate that jams it are not in the state yet.
		return false;

	case EFacilityExit::ServiceTunnel:
	{
		// Through the tunnel door, however it came open: the service key, the pry bar, later the flood.
		// And only while the fans are still, since they make the tunnel lethal while they turn.
		const FExitState* Tunnel = FindExit(State, Exit);
		return Tunnel != nullptr && IsDoorOpen(State, Tunnel->DoorId) && AreFansRunning(State, Tunnel->FansId) == false;
	}

	default:
		return false;
	}
}

EAlarmLevel FFacilityRules::GetAlarmLevel(const FFacilityState& State)
{
	return State.AlarmLevel;
}

bool FFacilityRules::IsLockdown(const FFacilityState& State)
{
	return State.AlarmLevel == EAlarmLevel::EAL_Lockdown;
}

bool FFacilityRules::CanSilenceAlarm(const FFacilityState& State, const FName ConsoleId)
{
	return IsDevicePowered(State, ConsoleId) && State.AlarmLevel > EAlarmLevel::EAL_Quiet;
}

bool FFacilityRules::SetCircuitState(FFacilityState& State, const EFacilityCircuit Circuit, const ECircuitState NewState)
{
	if (Circuit == EFacilityCircuit::EFC_None || GetCircuitState(State, Circuit) == NewState)
	{
		return false;
	}

	State.Circuits.Add(Circuit, NewState);
	SettleDoors(State);
	SettleLifts(State);
	return true;
}

bool FFacilityRules::SetDeviceCircuit(FFacilityState& State, const FName DeviceId, const EFacilityCircuit NewCircuit)
{
	if (DeviceId.IsNone() || GetDeviceCircuit(State, DeviceId) == NewCircuit)
	{
		return false;
	}

	State.DeviceCircuits.Add(DeviceId, NewCircuit);

	// A lockdown door rerouted onto a dead circuit seals there and then, and one just registered
	// follows its circuit for the first time here. The elevator parks the same way.
	if (FDoorState* Door = State.Doors.Find(DeviceId))
	{
		SettleDoor(State, DeviceId, *Door);
	}
	if (FLiftState* Lift = State.Lifts.Find(DeviceId))
	{
		SettleLift(State, DeviceId, *Lift);
	}
	return true;
}

bool FFacilityRules::FlipBreaker(FFacilityState& State, const EFacilityCircuit Circuit)
{
	if (Circuit == EFacilityCircuit::EFC_None)
	{
		return false;
	}

	// A shorted breaker will not stay thrown. The fault is in the flooded wiring, not the panel, and
	// it is there for the attempt.
	if (GetCircuitState(State, Circuit) == ECircuitState::Shorted)
	{
		return false;
	}

	// The panel works on breaker positions. What the generator carries meanwhile is delivery, and no
	// business of the panel's: a breaker thrown while the generator runs is thrown all the same.
	if (IsBreakerOn(State, Circuit))
	{
		// Throwing an on breaker just cuts it.
		State.Circuits.Add(Circuit, ECircuitState::Cut);
	}
	else
	{
		int32 OnCount = 0;
		for (const EFacilityCircuit Breaker : BreakerCircuits)
		{
			OnCount += IsBreakerOn(State, Breaker) ? 1 : 0;
		}

		// Two of three can be on at once. Asking for a third trips the whole panel.
		if (OnCount >= MaxLiveCircuits)
		{
			for (const EFacilityCircuit Breaker : BreakerCircuits)
			{
				if (IsBreakerOn(State, Breaker))
				{
					State.Circuits.Add(Breaker, ECircuitState::Cut);
				}
			}
		}
		else
		{
			State.Circuits.Add(Circuit, ECircuitState::Live);
		}
	}

	SettleDoors(State);
	SettleLifts(State);
	return true;
}

bool FFacilityRules::StartGenerator(FFacilityState& State)
{
	if (CanStartGenerator(State) == false)
	{
		return false;
	}

	// Every cut circuit carries power from here until the steps run out. The breakers stay where they
	// are, so what they say comes back when the generator stops.
	State.GeneratorStepsLeft = FMath::Max(1, State.Config.GeneratorRunSteps);
	SettleDoors(State);
	SettleLifts(State);

	// Starting it is loud: the alarm rises one level.
	RaiseAlarmLevelByOne(State);
	return true;
}

bool FFacilityRules::OverloadGenerator(FFacilityState& State)
{
	if (CanOverloadGenerator(State) == false)
	{
		return false;
	}

	// The generator is finished, and it takes every breaker with it: the lockdown doors seal and the
	// elevator parks with the rest of the blackout. The breakers can be thrown again afterwards; the
	// generator cannot. The guards' answer to the blackout is theirs to give.
	State.bGeneratorOverloaded = true;
	State.GeneratorStepsLeft = 0;
	for (const EFacilityCircuit Breaker : BreakerCircuits)
	{
		if (IsBreakerOn(State, Breaker))
		{
			State.Circuits.Add(Breaker, ECircuitState::Cut);
		}
	}

	SettleDoors(State);
	SettleLifts(State);
	return true;
}

bool FFacilityRules::AddLift(FFacilityState& State, const FName LiftId, const FLiftState& Lift)
{
	if (LiftId.IsNone() || State.Lifts.Contains(LiftId))
	{
		return false;
	}

	FLiftState& Added = State.Lifts.Add(LiftId, Lift);
	SettleLift(State, LiftId, Added);
	return true;
}

bool FFacilityRules::AddHackTarget(FFacilityState& State, const FName DeviceId, const int32 HackLevel)
{
	if (DeviceId.IsNone() || HackLevel <= 0 || State.HackLevels.Contains(DeviceId))
	{
		return false;
	}

	State.HackLevels.Add(DeviceId, HackLevel);
	return true;
}

bool FFacilityRules::SetLiftStop(FFacilityState& State, const FName LiftId, const ELiftStop Stop)
{
	FLiftState* Lift = State.Lifts.Find(LiftId);
	if (Lift == nullptr || Lift->Stop == Stop)
	{
		return false;
	}

	Lift->Stop = Stop;
	return true;
}

bool FFacilityRules::CallLift(FFacilityState& State, const FName LiftId, const ELiftStop Stop)
{
	if (CanUseLift(State, LiftId) == false)
	{
		return false;
	}

	if (SetLiftStop(State, LiftId, Stop) == false)
	{
		return false;
	}

	// The cargo lift moves the dock crate: a crate at the head of this lift's shaft rides the car down
	// and lies in Plant when it arrives. Quietly, and it lands on nobody.
	if (Stop == ELiftStop::ELS_Bottom)
	{
		for (TPair<FName, FCrateState>& Entry : State.Crates)
		{
			if (Entry.Value.LiftId == LiftId && Entry.Value.Position == ECratePosition::AtShaftHead)
			{
				Entry.Value.Position = ECratePosition::InPlant;
			}
		}
	}
	return true;
}

bool FFacilityRules::HackLift(FFacilityState& State, const FName LiftId)
{
	if (CanHackLift(State, LiftId) == false)
	{
		return false;
	}

	FLiftState& Lift = State.Lifts.FindChecked(LiftId);
	if (WouldHackSucceed(State, GetHackLevel(State, LiftId)) == false)
	{
		// A target above the handheld's level: the hack fails, and a failed hack raises the alarm one level.
		return RaiseAlarmLevelByOne(State);
	}

	// At or above the target's level the hack succeeds silently. The car stops where it is.
	Lift.bHacked = true;
	return true;
}

bool FFacilityRules::AddDoor(FFacilityState& State, const FName DoorId, const FDoorState& Door)
{
	if (DoorId.IsNone() || State.Doors.Contains(DoorId))
	{
		return false;
	}

	FDoorState& Added = State.Doors.Add(DoorId, Door);
	SettleDoor(State, DoorId, Added);
	return true;
}

bool FFacilityRules::SetDoorOpen(FFacilityState& State, const FName DoorId, const bool bOpen)
{
	if (DoorId.IsNone())
	{
		return false;
	}

	// Checked against the entry, not IsDoorOpen: an unknown door reads as closed but is not yet
	// known, and putting it closed has to count as a change so the entry exists.
	const FDoorState* Found = State.Doors.Find(DoorId);
	if (Found != nullptr && Found->bOpen == bOpen)
	{
		return false;
	}

	// A forced door is open, so the only thing left to ask of it is closing, and nothing closes it.
	if (Found != nullptr && Found->bForced)
	{
		return false;
	}

	State.Doors.FindOrAdd(DoorId).bOpen = bOpen;
	return true;
}

/**
 * Opens a closed door the rules have already cleared, and makes the noise the GDD gives the gate under
 * an alert. Asked before the door moves: the noise is the level the gate opened under, and the raise
 * it causes settles the gate again, so under Alerted the gate seals as it opens. Returns true if the
 * door moved, whatever happened to it after.
 */
static bool OpenDoorWithNoise(FFacilityState& State, const FName DoorId)
{
	const bool bLoud = FFacilityRules::IsDoorLoudToOpen(State, DoorId);
	if (FFacilityRules::SetDoorOpen(State, DoorId, true) == false)
	{
		return false;
	}

	if (bLoud)
	{
		FFacilityRules::RaiseAlarmLevelByOne(State);
	}
	return true;
}

bool FFacilityRules::ToggleDoor(FFacilityState& State, const FName DoorId)
{
	if (GetDoorObstacle(State, DoorId) != EDoorObstacle::None)
	{
		return false;
	}

	// Closing is always quiet. Opening the gate under an alert is not.
	if (IsDoorOpen(State, DoorId))
	{
		return SetDoorOpen(State, DoorId, false);
	}

	return OpenDoorWithNoise(State, DoorId);
}

bool FFacilityRules::HackDoor(FFacilityState& State, const FName DoorId)
{
	if (CanHackDoor(State, DoorId) == false)
	{
		return false;
	}

	if (WouldHackSucceed(State, GetHackLevel(State, DoorId)) == false)
	{
		// A target above the handheld's level: the hack fails, and a failed hack raises the alarm one level.
		return RaiseAlarmLevelByOne(State);
	}

	// At or above the target's level the hack succeeds silently. The door itself may still be loud.
	return OpenDoorWithNoise(State, DoorId);
}

bool FFacilityRules::AddCamera(FFacilityState& State, const FName CameraId, const FCameraState& Camera)
{
	if (CameraId.IsNone() || State.Cameras.Contains(CameraId))
	{
		return false;
	}

	State.Cameras.Add(CameraId, Camera);
	return true;
}

bool FFacilityRules::HackCamera(FFacilityState& State, const FName CameraId)
{
	if (CanHackCamera(State, CameraId) == false)
	{
		return false;
	}

	if (WouldHackSucceed(State, GetHackLevel(State, CameraId)) == false)
	{
		// A target above the handheld's level: the hack fails, and a failed hack raises the alarm one level.
		return RaiseAlarmLevelByOne(State);
	}

	// At or above the target's level the hack succeeds silently. The camera loops and sees nothing more.
	State.Cameras.FindChecked(CameraId).bLooped = true;
	return true;
}

bool FFacilityRules::CameraSighting(FFacilityState& State, const FName CameraId)
{
	// Only a watching camera sees anyone. A sighting raises the alarm one level.
	return IsCameraWatching(State, CameraId) && RaiseAlarmLevelByOne(State);
}

bool FFacilityRules::HackGateController(FFacilityState& State, const FName ControllerId)
{
	if (CanHackGateController(State, ControllerId) == false)
	{
		return false;
	}

	if (WouldHackSucceed(State, GetHackLevel(State, ControllerId)) == false)
	{
		// A target above the handheld's level: the hack fails, and a failed hack raises the alarm one level.
		return RaiseAlarmLevelByOne(State);
	}

	// At or above the target's level the hack succeeds silently. The gate itself may still be loud to open.
	return OpenDoorWithNoise(State, FindDoorOfKind(State, EDoorKind::Gate));
}

bool FFacilityRules::AddRoutingOption(FFacilityState& State, const FName PanelId, const FRoutingOption& Option)
{
	if (PanelId.IsNone())
	{
		return false;
	}

	// A routing job has to name what it moves and where to. The pump names nothing.
	if (Option.Action == ERoutingAction::RouteDevice && (Option.DeviceId.IsNone() || Option.Circuit == EFacilityCircuit::EFC_None))
	{
		return false;
	}

	// One panel. The first switch names it, and a switch under another id is refused.
	FRoutingState& Routing = State.Routing;
	if (Routing.PanelId.IsNone())
	{
		Routing.PanelId = PanelId;
	}
	else if (Routing.PanelId != PanelId)
	{
		return false;
	}

	const FRoutingOption Stored = NormalizeRoutingOption(Option);
	if (Routing.Options.Contains(Stored))
	{
		return false;
	}

	Routing.Options.Add(Stored);
	return true;
}

bool FFacilityRules::RouteDevice(FFacilityState& State, const FName PanelId, const FName DeviceId, const EFacilityCircuit Circuit)
{
	if (CanRouteDevice(State, PanelId, DeviceId, Circuit) == false)
	{
		return false;
	}

	if (WouldHackSucceed(State, GetHackLevel(State, PanelId)) == false)
	{
		// A target above the handheld's level: the hack fails, and a failed hack raises the alarm one level.
		return RaiseAlarmLevelByOne(State);
	}

	// At or above the panel's level the hack succeeds silently.
	FRoutingState& Routing = State.Routing;
	const bool bPatchedAlready = IsDevicePatched(State, DeviceId);
	const EFacilityCircuit Home = bPatchedAlready ? Routing.HomeCircuit : GetDeviceCircuit(State, DeviceId);

	// Where the device ends up: home again when this is the panel's own patch being undone, otherwise
	// on the switch's circuit.
	const bool bUndo = bPatchedAlready && GetDeviceCircuit(State, DeviceId) == Circuit;
	const EFacilityCircuit Target = bUndo ? Home : Circuit;

	// One patch at a time: whatever else the panel moved goes back to its own circuit first.
	if (Routing.PatchedDevice.IsNone() == false && bPatchedAlready == false)
	{
		SetDeviceCircuit(State, Routing.PatchedDevice, Routing.HomeCircuit);
	}

	if (Target == Home)
	{
		Routing.PatchedDevice = NAME_None;
		Routing.HomeCircuit = EFacilityCircuit::EFC_None;
	}
	else
	{
		Routing.PatchedDevice = DeviceId;
		Routing.HomeCircuit = Home;
	}

	// The device follows its new circuit at once: SetDeviceCircuit settles a door or a lift on it, and
	// power reaches the rest through IsDevicePowered.
	SetDeviceCircuit(State, DeviceId, Target);
	return true;
}

bool FFacilityRules::RunCoolantPump(FFacilityState& State, const FName PanelId)
{
	if (CanRunCoolantPump(State, PanelId) == false)
	{
		return false;
	}

	if (WouldHackSucceed(State, GetHackLevel(State, PanelId)) == false)
	{
		// A target above the handheld's level: the hack fails, and a failed hack raises the alarm one level.
		return RaiseAlarmLevelByOne(State);
	}

	// At or above the panel's level the pump runs silently. The valve stays closed, so the fans can drain the water.
	return FloodPlant(State);
}

bool FFacilityRules::ForceDoor(FFacilityState& State, const FName DoorId)
{
	if (CanForceDoor(State, DoorId) == false)
	{
		return false;
	}

	FDoorState& Door = State.Doors.FindChecked(DoorId);
	Door.bOpen = true;
	Door.bForced = true;

	// Forcing is loud, and noise raises the alarm one level. The door is forced first, so a raise that
	// calls lockdown settles every other door and leaves this one open.
	RaiseAlarmLevelByOne(State);
	return true;
}

bool FFacilityRules::AddPickup(FFacilityState& State, const FName PickupId, const EFacilityItem Item)
{
	if (PickupId.IsNone() || Item == EFacilityItem::None || State.Pickups.Contains(PickupId))
	{
		return false;
	}

	State.Pickups.Add(PickupId).Item = Item;
	return true;
}

bool FFacilityRules::TakePickup(FFacilityState& State, const FName PickupId)
{
	if (CanTakePickup(State, PickupId) == false)
	{
		return false;
	}

	FPickupState* Pickup = State.Pickups.Find(PickupId);
	Pickup->bTaken = true;

	// The pickup is emptied whether or not the player needed what was in it.
	GiveItem(State, Pickup->Item);
	return true;
}

bool FFacilityRules::GiveItem(FFacilityState& State, const EFacilityItem Item)
{
	if (Item == EFacilityItem::None || State.Inventory.Contains(Item))
	{
		return false;
	}

	State.Inventory.Add(Item);
	return true;
}

bool FFacilityRules::TakeItem(FFacilityState& State, const EFacilityItem Item)
{
	return State.Inventory.Remove(Item) > 0;
}

bool FFacilityRules::AddFlood(FFacilityState& State, const FName FansId)
{
	if (FansId.IsNone() || State.Flood.FansId.IsNone() == false)
	{
		return false;
	}

	State.Flood.FansId = FansId;
	return true;
}

bool FFacilityRules::FloodPlant(FFacilityState& State)
{
	// More water on standing water changes nothing, and the count keeps going.
	if (State.Flood.bFlooded)
	{
		return false;
	}

	State.Flood.bFlooded = true;
	State.Flood.Steps = 0;
	return true;
}

bool FFacilityRules::OpenCoolantValve(FFacilityState& State)
{
	if (CanOpenCoolantValve(State) == false)
	{
		return false;
	}

	// Open, and Plant floods. Quietly: nothing here is noise.
	State.bCoolantValveOpen = true;
	FloodPlant(State);
	return true;
}

bool FFacilityRules::CloseCoolantValve(FFacilityState& State)
{
	if (CanCloseCoolantValve(State) == false)
	{
		return false;
	}

	// The water already down stays. The fans can drain it now.
	State.bCoolantValveOpen = false;
	return true;
}

bool FFacilityRules::OverloadServerRack(FFacilityState& State, const FName RackId)
{
	if (CanOverloadServerRack(State, RackId) == false)
	{
		return false;
	}

	State.bServerRackOverloaded = true;

	// The smoke alarm: the alarm goes to Alerted, and stays where it is if it is higher already. An
	// event all the same, so the quiet count starts over either way.
	State.QuietSteps = 0;
	if (State.AlarmLevel < EAlarmLevel::EAL_Alerted)
	{
		SetAlarmLevel(State, EAlarmLevel::EAL_Alerted);
	}

	// The sprinklers dump water into Plant through the vent. Water counts as flood.
	FloodPlant(State);
	return true;
}

bool FFacilityRules::AddCrate(FFacilityState& State, const FName CrateId, const FCrateState& Crate)
{
	if (CrateId.IsNone() || State.Crates.Contains(CrateId))
	{
		return false;
	}

	State.Crates.Add(CrateId, Crate);
	return true;
}

bool FFacilityRules::MoveCrate(FFacilityState& State, const FName CrateId)
{
	if (CanMoveCrate(State, CrateId) == false)
	{
		return false;
	}

	// Off the shutter and to the shaft head. Quiet, whichever did the moving.
	State.Crates.FindChecked(CrateId).Position = ECratePosition::AtShaftHead;
	return true;
}

bool FFacilityRules::DropCrate(FFacilityState& State, const FName CrateId)
{
	if (CanDropCrate(State, CrateId) == false)
	{
		return false;
	}

	FCrateState& Crate = State.Crates.FindChecked(CrateId);
	Crate.Position = ECratePosition::InPlant;
	Crate.bDropped = true;

	// It lands on whatever stands below, which its actor sees to, and the landing is loud: the alarm
	// rises one level.
	RaiseAlarmLevelByOne(State);
	return true;
}

bool FFacilityRules::AddExit(FFacilityState& State, const EFacilityExit Exit, const FExitState& ExitState)
{
	if (Exit == EFacilityExit::None || State.Exits.Contains(Exit))
	{
		return false;
	}

	State.Exits.Add(Exit, ExitState);
	return true;
}

bool FFacilityRules::Escape(FFacilityState& State, const EFacilityExit Exit)
{
	if (CanEscape(State, Exit) == false)
	{
		return false;
	}

	State.EscapedThrough = Exit;
	return true;
}

bool FFacilityRules::SetAlarmLevel(FFacilityState& State, const EAlarmLevel NewLevel)
{
	if (NewLevel == EAlarmLevel::EAL_None || State.AlarmLevel == NewLevel)
	{
		return false;
	}

	State.AlarmLevel = NewLevel;

	// A new level starts a new count toward the next one down.
	State.QuietSteps = 0;

	// Calling lockdown shuts the lockdown doors and the gate there and then, and parks the elevator.
	// Lifting it holds nothing shut any more but moves nothing either: a door nothing holds stays
	// where it is.
	SettleDoors(State);
	SettleLifts(State);
	return true;
}

bool FFacilityRules::RaiseAlarmLevelByOne(FFacilityState& State)
{
	// Noise starts the quiet count over even at the top, so a lockdown holds while noise keeps coming.
	State.QuietSteps = 0;

	// The enum order is the escalation order, and Lockdown is the top.
	if (State.AlarmLevel >= EAlarmLevel::EAL_Lockdown)
	{
		return false;
	}

	return SetAlarmLevel(State, static_cast<EAlarmLevel>(static_cast<uint8>(State.AlarmLevel) + 1));
}

bool FFacilityRules::LowerAlarmLevelByOne(FFacilityState& State)
{
	// Quiet is the bottom. None sits below it in the enum but is not a level.
	if (State.AlarmLevel <= EAlarmLevel::EAL_Quiet)
	{
		return false;
	}

	return SetAlarmLevel(State, static_cast<EAlarmLevel>(static_cast<uint8>(State.AlarmLevel) - 1));
}

bool FFacilityRules::SilenceAlarm(FFacilityState& State, const FName ConsoleId)
{
	return CanSilenceAlarm(State, ConsoleId) && LowerAlarmLevelByOne(State);
}

/**
 * The flood stands one more step, and returns true if that changed anything the player can see. A step
 * that finds the fans running drains the water, unless the valve keeps it fed: drained water shorts
 * nothing and corrodes nothing more. Otherwise the water is conductive: once it has stood
 * Config.FloodShortSteps, every circuit carrying power shorts, the generator's power included. A short
 * is for the attempt, since the panel cannot throw a shorted breaker back and the generator carries
 * nothing through it; DOORS shorting fails the lockdown doors open, and any dead circuit parks the
 * elevator. Once the water has stood Config.CorrosionSteps it eats through the service tunnel lock,
 * the door the tunnel exit names; a level with no tunnel exit registered has nothing to corrode.
 */
static bool StepFlood(FFacilityState& State)
{
	if (State.Flood.bFlooded == false)
	{
		return false;
	}

	if (FFacilityRules::WouldFansDrainFlood(State))
	{
		State.Flood.bFlooded = false;
		State.Flood.Steps = 0;
		return true;
	}

	++State.Flood.Steps;
	bool bChanged = false;

	if (State.Flood.Steps >= State.Config.FloodShortSteps)
	{
		bool bShorted = false;
		for (const EFacilityCircuit Circuit : BreakerCircuits)
		{
			if (FFacilityRules::IsCircuitLive(State, Circuit))
			{
				State.Circuits.Add(Circuit, ECircuitState::Shorted);
				bShorted = true;
			}
		}

		if (bShorted)
		{
			SettleDoors(State);
			SettleLifts(State);
			bChanged = true;
		}
	}

	if (State.Flood.Steps >= State.Config.CorrosionSteps)
	{
		const FExitState* Tunnel = State.Exits.Find(EFacilityExit::ServiceTunnel);
		FDoorState* Door = Tunnel != nullptr ? State.Doors.Find(Tunnel->DoorId) : nullptr;
		if (Door != nullptr && Door->bLockCorroded == false)
		{
			Door->bLockCorroded = true;
			bChanged = true;
		}
	}

	return bChanged;
}

bool FFacilityRules::AdvanceStep(FFacilityState& State)
{
	bool bChanged = false;

	// The generator burns a step. When it runs out, power falls back to the breakers, and whatever they
	// leave dead seals and parks now.
	if (State.GeneratorStepsLeft > 0)
	{
		--State.GeneratorStepsLeft;
		if (State.GeneratorStepsLeft == 0)
		{
			SettleDoors(State);
			SettleLifts(State);
		}
		bChanged = true;
	}

	// The flood stands another step: drained by the fans, or shorting what is live and eating at the
	// tunnel lock.
	if (StepFlood(State))
	{
		bChanged = true;
	}

	// Only a raised alarm has anything to count down. At Quiet the count means nothing and stays at zero.
	if (State.AlarmLevel <= EAlarmLevel::EAL_Quiet)
	{
		State.QuietSteps = 0;
		return bChanged;
	}

	++State.QuietSteps;
	if (State.QuietSteps < State.Config.AlarmDecaySteps)
	{
		return bChanged;
	}

	// Enough quiet. The alarm comes down one level, which starts the count again for the next one.
	return LowerAlarmLevelByOne(State) || bChanged;
}

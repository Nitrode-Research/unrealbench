#pragma once

#include "CoreMinimal.h"
#include "Facility/FacilityTypes.h"

/**
 * The rules of the facility, written over a bare FFacilityState.
 *
 * UFacilityStateSubsystem forwards to these for the live game, and the route solver will call them
 * directly on hypothetical states, so the two can never disagree. Predicates read. Mutations write
 * and return whether anything changed, which is what the subsystem uses to decide whether to
 * broadcast. Nothing in here touches an actor, a world or a log.
 */
struct SCIFISIMESCAPE_API FFacilityRules
{
	/** Two of three circuits can be live at once. Asking for a third trips the whole panel. */
	static constexpr int32 MaxLiveCircuits = 2;

	/** Puts the breakers in the GDD's initial state, DOORS and SECURITY live and PLANT off, and the alarm at Quiet. Leaves device routing alone. */
	static void SetInitialState(FFacilityState& State);

	//
	// Predicates
	//

	/** What the circuit's breaker is doing, Cut when unknown. Shorted is the flood's doing, not the panel's. */
	static ECircuitState GetCircuitState(const FFacilityState& State, EFacilityCircuit Circuit);

	/** True while the breaker is on. What the panel's switch shows; whether power arrives is IsCircuitLive. */
	static bool IsBreakerOn(const FFacilityState& State, EFacilityCircuit Circuit);

	/**
	 * True while the circuit carries power: its breaker is on, or the backup generator is running and
	 * the circuit is merely cut. A shorted circuit carries nothing, generator or not. Every device
	 * reads this through IsDevicePowered; nothing asks the breaker itself except the panel.
	 */
	static bool IsCircuitLive(const FFacilityState& State, EFacilityCircuit Circuit);

	/** Circuit the device is routed to. None if the device is unknown or wired to nothing. */
	static EFacilityCircuit GetDeviceCircuit(const FFacilityState& State, FName DeviceId);

	/** True when the device is routed to a live circuit. Unknown devices are never powered. */
	static bool IsDevicePowered(const FFacilityState& State, FName DeviceId);

	//
	// The backup generator in Plant. Running, it carries every cut circuit for a configured number of
	// steps, the one way around the two-of-three rule. Starting it is loud. Rigged to overload with the
	// pry bar, it trips every breaker and is finished for the attempt.
	//

	static bool IsGeneratorRunning(const FFacilityState& State);
	static int32 GetGeneratorStepsLeft(const FFacilityState& State);
	static bool IsGeneratorOverloaded(const FFacilityState& State);

	/** True when the generator can be started: it is not running and has not been overloaded. */
	static bool CanStartGenerator(const FFacilityState& State);

	/**
	 * True when the pry bar would rig the generator to overload right now: the player carries it, and
	 * the generator is running and not overloaded already. A generator that is off has nothing to overload.
	 */
	static bool CanOverloadGenerator(const FFacilityState& State);

	//
	// Lifts. The elevator and the cargo lift are both a car that is at one of two stops.
	//

	/** Stop the lift's car is at. Bottom if the lift is unknown. */
	static ELiftStop GetLiftStop(const FFacilityState& State, FName LiftId);

	/** The stop that is not this one. */
	static ELiftStop OtherLiftStop(ELiftStop Stop);

	/** Which lift it is. Cargo lift if unknown. */
	static ELiftKind GetLiftKind(const FFacilityState& State, FName LiftId);

	/** True once the handheld has stopped the lift. It stays stopped for the rest of the attempt. */
	static bool IsLiftHacked(const FFacilityState& State, FName LiftId);

	/**
	 * Why the lift will not answer a call right now, None when it will. The one place the lift rules
	 * are put in order: stopped by the handheld first, then power, then lockdown for the elevator.
	 * CanUseLift, CallLift and every lift prompt read this, so nothing else asks those questions on
	 * its own.
	 */
	static ELiftObstacle GetLiftObstacle(const FFacilityState& State, FName LiftId);

	/** True when the lift answers a call: GetLiftObstacle finds nothing in the way. */
	static bool CanUseLift(const FFacilityState& State, FName LiftId);

	/**
	 * True when the handheld has something to work on: the lift has a hack level, has power, and is not
	 * stopped already. Whether the hack would succeed is WouldHackSucceed over its level; a hack the
	 * handheld cannot reach can still be attempted, and fails loudly.
	 */
	static bool CanHackLift(const FFacilityState& State, FName LiftId);

	//
	// Items. What the player carries, and where the rest still lies. A card is its level: whoever
	// dropped it, a level 2 card is a level 2 card.
	//

	/** Lock level a keycard opens: 2 for the level 2 card, 3 for the level 3 card, 0 for anything else. */
	static int32 KeycardLevelOf(EFacilityItem Item);

	/** True when the player carries the item. */
	static bool HasItem(const FFacilityState& State, EFacilityItem Item);

	/** Highest keycard level the player carries. 0 without a card. */
	static int32 GetKeycardLevel(const FFacilityState& State);

	/** What lies at the pickup, taken or not. None if the pickup is unknown. */
	static EFacilityItem GetPickupItem(const FFacilityState& State, FName PickupId);

	/**
	 * True once the player has taken the pickup. An unknown pickup reads as taken: there is nothing
	 * there, which is also what makes a pickup spawned mid-attempt vanish when a reset forgets it.
	 */
	static bool IsPickupTaken(const FFacilityState& State, FName PickupId);

	/** True when the pickup still lies where it was placed and holds something. */
	static bool CanTakePickup(const FFacilityState& State, FName PickupId);

	//
	// The handheld. The player always has it; what changes is its level. It works on doors, the
	// elevator, the camera, the routing panel, the alarm console and the gate controller, and only on
	// a target that has power.
	//

	/** Handheld level the device needs, 0 when the handheld has nothing to work on there or the device is unknown. */
	static int32 GetHackLevel(const FFacilityState& State, FName DeviceId);

	/** Hack level of the handheld: where it starts, or where the server drive raised it to once the player holds the drive. */
	static int32 GetHandheldLevel(const FFacilityState& State);

	/** True when a hack on a target of that level succeeds: the handheld's level reaches it. A target at level 0 is nothing to hack. */
	static bool WouldHackSucceed(const FFacilityState& State, int32 TargetLevel);

	/** True once the player has pulled the drive from the Lab server rack. This is what blinds the camera; the rack itself stays on SECURITY. */
	static bool IsServerDrivePulled(const FFacilityState& State);

	//
	// Doors. The keycard doors, the lockers, the gate and the hatch are all doors here. What sets
	// them apart is their FDoorState and whether their id is on a circuit: a door on one moves only
	// while it is live, a door on none is mechanical.
	//

	/** True when the door is open. Unknown doors are closed. */
	static bool IsDoorOpen(const FFacilityState& State, FName DoorId);

	/** Id of the first door of that kind, None if there is none. This is how the rules find the gate. */
	static FName FindDoorOfKind(const FFacilityState& State, EDoorKind Kind);

	/** True once the pry bar has forced the door. It stays open for the rest of the attempt, whatever holds the others. */
	static bool IsDoorForced(const FFacilityState& State, FName DoorId);

	/**
	 * True while the door is held shut: a lockdown door whose circuit is cut, and a lockdown door on
	 * a live circuit or the gate while lockdown is called. A lockdown door on a shorted circuit is
	 * never sealed, lockdown or not; it has failed open. Nor is a door the pry bar forced. A sealed
	 * door is closed: the circuit and alarm mutations shut it as they seal it.
	 */
	static bool IsDoorSealed(const FFacilityState& State, FName DoorId);

	/** True while a lockdown door's circuit is shorted. Such a door is open and stays open until the short ends. */
	static bool IsDoorFailedOpen(const FFacilityState& State, FName DoorId);

	/** Keycard level the door's lock takes, 0 for none or an unknown door. What the prompt names. */
	static int32 GetDoorLockLevel(const FFacilityState& State, FName DoorId);

	/** Item the door's lock takes, None for none or an unknown door. What the prompt names. */
	static EFacilityItem GetDoorKeyItem(const FFacilityState& State, FName DoorId);

	/** True once the flood has eaten through the door's lock. It takes no card and no key from then on. */
	static bool IsDoorLockCorroded(const FFacilityState& State, FName DoorId);

	/**
	 * True while a closed door's lock holds it: the door takes a keycard level or a key, the player
	 * carries neither a card of that level nor the key, and the flood has not corroded the lock through.
	 */
	static bool IsDoorLocked(const FFacilityState& State, FName DoorId);

	/**
	 * Why the door will not move right now, None when it will. The one place the door rules are put
	 * in order: forced first, then sealed and failed open, then power for a door on a circuit, then the
	 * lock against the player's cards and keys for a closed door. A console override joins here.
	 * CanOpenDoor, CanCloseDoor, ToggleDoor, CanForceDoor, CanHackDoor and every door prompt read
	 * this, so nothing else asks those questions on its own.
	 */
	static EDoorObstacle GetDoorObstacle(const FFacilityState& State, FName DoorId);

	/**
	 * True when the handheld has something to work on: the door has a hack level, has power, and only
	 * its lock holds it. A hack stands in for the card or key and nothing else: a seal keeps out the
	 * handheld the way it keeps out the card. Never the gate, which opens through its controller.
	 * Whether the hack would succeed is WouldHackSucceed over the door's level; a hack the handheld
	 * cannot reach can still be attempted, and fails loudly.
	 */
	static bool CanHackDoor(const FFacilityState& State, FName DoorId);

	/**
	 * True when opening the door is noise: the gate while the alarm is at Alerted or higher. The noise
	 * raises the alarm one level, and if that calls lockdown the gate seals again as it opens.
	 */
	static bool IsDoorLoudToOpen(const FFacilityState& State, FName DoorId);

	/** True when the door is closed and nothing stops it opening. */
	static bool CanOpenDoor(const FFacilityState& State, FName DoorId);

	/** True when the door is open and nothing stops it closing. */
	static bool CanCloseDoor(const FFacilityState& State, FName DoorId);

	/**
	 * True when the pry bar would force the door open right now: the player carries it, and the door
	 * is closed, is not the gate, and is held by something the pry bar gets past, a seal, a dead
	 * circuit or the lock. A door nothing holds is opened by hand instead. The gate is electronic and
	 * opens on its own terms only: a card, the console or a hack.
	 */
	static bool CanForceDoor(const FFacilityState& State, FName DoorId);

	//
	// Fans. The vent fans in Plant, which make the Lab vent and the service tunnel lethal while they turn.
	//

	/**
	 * True when the fans are turning, which is when they kill. Today that is power alone. Anything else
	 * that starts or stops the fans gets checked here, so everything that asks whether the fans are
	 * dangerous asks this and never IsDevicePowered directly.
	 */
	static bool AreFansRunning(const FFacilityState& State, FName FansId);

	//
	// Lights. The lights of one zone, Level 1 and Storage on DOORS and Plant on PLANT, are one device:
	// every fixture in the zone is placed under the zone's id.
	//

	/**
	 * True when the zone's lights are on, which is when a guard or the camera can see by them. Today that
	 * is power alone. Anything else that puts the lights out gets checked here, so everything that asks
	 * whether a zone is lit asks this and never IsDevicePowered directly.
	 */
	static bool AreLightsOn(const FFacilityState& State, FName LightsId);

	//
	// The camera in the Corridor. It sees the whole Corridor while it has power and the lights it sees
	// by are on, and a sighting raises the alarm. The handheld loops it, the server drive blinds it,
	// and cutting SECURITY or the lights kills it.
	//

	/** True once the handheld has looped the camera. It stays looped for the rest of the attempt. */
	static bool IsCameraLooped(const FFacilityState& State, FName CameraId);

	/**
	 * True while the camera sees: it has power, the lights zone it sees by is on, the handheld has not
	 * looped it and the server drive is still in its rack. This is the one question a sighting asks.
	 */
	static bool IsCameraWatching(const FFacilityState& State, FName CameraId);

	/** True when the handheld has something to work on: the camera has a hack level, has power, and is not looped already. */
	static bool CanHackCamera(const FFacilityState& State, FName CameraId);

	//
	// The gate controller in Security. The gate itself is not a handheld target; the controller is,
	// and hacking it opens the gate the way a level 3 card does.
	//

	/**
	 * True when the handheld has something to work on: the controller has a hack level and power, and
	 * only the lock holds the gate. The controller stands in for the card and nothing else: a sealed or
	 * unpowered gate stays shut.
	 */
	static bool CanHackGateController(const FFacilityState& State, FName ControllerId);

	//
	// The circuit-routing panel in the Lab. A hack target, at level 1 in the GDD, with a switch per
	// job: it moves one device onto another circuit, the cargo lift or the fans onto SECURITY, or runs
	// the coolant pump remotely, which floods Plant without the valve. One patch at a time: routing a
	// second device sends the first back to the circuit it was placed on. Each switch registers its job
	// as an FRoutingOption, so the rules and the route solver know what the panel offers.
	//

	/** True when the handheld has the panel to work on at all: a switch registered it, it has a hack level and it has power. */
	static bool CanUseRoutingPanel(const FFacilityState& State, FName PanelId);

	/** The one device the panel has moved off the circuit it was placed on, None while every device is home. */
	static FName GetPatchedDevice(const FFacilityState& State);

	/** True while the panel has this device on another circuit. */
	static bool IsDevicePatched(const FFacilityState& State, FName DeviceId);

	/** Circuit the device was placed on: where it is now, unless the panel moved it, in which case where it goes back to. */
	static EFacilityCircuit GetHomeCircuit(const FFacilityState& State, FName DeviceId);

	/**
	 * True when the switch that routes this device onto this circuit has something to do: the panel is
	 * usable, a switch offers the job, the device is known, and the device is not on that circuit
	 * already, unless the panel put it there, in which case the switch sends it home. Whether the hack
	 * would succeed is WouldHackSucceed over the panel's level; a hack the handheld cannot reach can
	 * still be attempted, and fails loudly.
	 */
	static bool CanRouteDevice(const FFacilityState& State, FName PanelId, FName DeviceId, EFacilityCircuit Circuit);

	/** True when the pump switch has something to do: the panel is usable, a switch offers the pump, and the Plant floor is dry. */
	static bool CanRunCoolantPump(const FFacilityState& State, FName PanelId);

	//
	// The flood. The coolant valve, the routing panel's pump and the server rack's sprinklers all put
	// water on the Plant floor, and it is one flood however it got there. Standing, it is lethal while
	// any circuit carries power, shorts every live circuit once it has stood Config.FloodShortSteps,
	// and eats through the service tunnel lock once it has stood Config.CorrosionSteps. A step that
	// finds the vent fans running drains it instead, unless the valve is open and keeps it fed.
	//

	/** True while water stands on the Plant floor. */
	static bool IsPlantFlooded(const FFacilityState& State);

	/** Steps the water has stood, 0 while the floor is dry. What the corrosion and the shorts count against. */
	static int32 GetFloodSteps(const FFacilityState& State);

	/**
	 * True while standing in the water kills: it is flooded and any circuit carries power, the
	 * generator's included. Every flood volume asks this and never the circuits directly.
	 */
	static bool IsFloodLethal(const FFacilityState& State);

	/** True when the next step drains the water: it is flooded, the fans it names are running, and the coolant valve is closed. */
	static bool WouldFansDrainFlood(const FFacilityState& State);

	static bool IsCoolantValveOpen(const FFacilityState& State);

	/** True when the pry bar would open the seized valve right now: the player carries it and the valve is closed. */
	static bool CanOpenCoolantValve(const FFacilityState& State);

	/** True when the valve is open. Closing takes nothing. */
	static bool CanCloseCoolantValve(const FFacilityState& State);

	//
	// The server rack in the Lab, on SECURITY. Its drive is a pickup; what is here is the fire. Rigged
	// to overload with the pry bar while it has power, it burns for the rest of the attempt: the Lab
	// fills with smoke guards will not enter, the alarm goes to Alerted, and the sprinklers flood Plant.
	//

	static bool IsServerRackOverloaded(const FFacilityState& State);

	/** True while the Lab is full of smoke, which is for as long as the rack burns. What keeps guards out of the Lab. */
	static bool IsLabSmokeFilled(const FFacilityState& State);

	/** True when the pry bar would rig the rack right now: the player carries it, the rack has power and is not burning already. */
	static bool CanOverloadServerRack(const FFacilityState& State, FName RackId);

	//
	// The dock crate in Storage. It jams the loading dock shutter until it is moved, which takes the pry
	// bar or the cargo lift running, and once moved it sits at the head of the lift shaft. A car called
	// down from there carries it to Plant quietly; pushed down the open shaft it lands on whatever stands
	// below, loudly. The neutral character as a second pair of hands joins CanMoveCrate when they exist.
	//

	/** Where the crate is. An unknown crate reads as in Plant: out of the way, jamming nothing. */
	static ECratePosition GetCratePosition(const FFacilityState& State, FName CrateId);

	/** True while the crate is wedged in the dock shutter. */
	static bool IsCrateJammingShutter(const FFacilityState& State, FName CrateId);

	/** True while any crate jams the dock shutter. What the loading dock exit asks. */
	static bool IsDockShutterJammed(const FFacilityState& State);

	/** True once the crate was pushed down the shaft rather than ridden down. */
	static bool WasCrateDropped(const FFacilityState& State, FName CrateId);

	/** True when the crate can be moved off the shutter right now: it jams it, and the player carries the pry bar or its lift answers calls. */
	static bool CanMoveCrate(const FFacilityState& State, FName CrateId);

	/** True when the crate can be pushed down the shaft right now: it sits at the shaft head and its lift's car is at the bottom, leaving the shaft open. */
	static bool CanDropCrate(const FFacilityState& State, FName CrateId);

	//
	// Exits. Passing through any of the three is the win.
	//

	/** True once the player has passed through an exit. */
	static bool HasEscaped(const FFacilityState& State);

	/** The exit the player passed through, None while they are still inside. */
	static EFacilityExit GetEscapeExit(const FFacilityState& State);

	/** What guards the exit, as its actor registered it. Null until an actor has. */
	static const FExitState* FindExit(const FFacilityState& State, EFacilityExit Exit);

	/**
	 * True when the exit lets the player through right now. The gate: the door of kind Gate is open,
	 * whoever opened it. The tunnel: the door its exit names is open, however that happened, and the
	 * fans it names are still, since they make the tunnel lethal while they turn. The dock shutter is
	 * not in the state yet, so it never does. Always false once the player has escaped, which is what
	 * makes the win happen once.
	 */
	static bool CanEscape(const FFacilityState& State, EFacilityExit Exit);

	//
	// Alarm. Quiet, Alerted and Lockdown, in that order. Guard-side state: it is here and it moves
	// whether or not SECURITY is live, since that circuit powers the camera and the console, not
	// the alarm itself.
	//

	/** How alarmed the facility is. Quiet when an attempt starts. */
	static EAlarmLevel GetAlarmLevel(const FFacilityState& State);

	/**
	 * True while lockdown is called. It seals the two lockdown doors and the main gate whatever card
	 * is swiped, which IsDoorSealed reads, and parks the elevator on Level 1, which GetLiftObstacle
	 * reads.
	 */
	static bool IsLockdown(const FFacilityState& State);

	/**
	 * True when using the alarm console brings the alarm down: the console has power and the alarm is
	 * above Quiet. The console is a device on SECURITY, so cutting SECURITY takes this away.
	 */
	static bool CanSilenceAlarm(const FFacilityState& State, FName ConsoleId);

	//
	// Mutations. Each returns true if the state changed. Anything that changes what a circuit
	// delivers, or moves the alarm, also settles the lockdown doors, the gate and the elevator: a
	// lockdown door is closed while its circuit is cut or lockdown is called and open while its circuit
	// is shorted, the gate is closed while lockdown is called, and the elevator is parked on Level 1
	// while its circuit is dead or lockdown is called. A door the pry bar forced stays open through all
	// of it. The quiet-step count is bookkeeping and never counts as a change on its own.
	//

	static bool SetCircuitState(FFacilityState& State, EFacilityCircuit Circuit, ECircuitState NewState);
	static bool SetDeviceCircuit(FFacilityState& State, FName DeviceId, EFacilityCircuit NewCircuit);

	/**
	 * Throws one breaker on the panel in Plant. A live circuit is cut. A dead one comes up live, unless
	 * MaxLiveCircuits others already are, in which case every live circuit trips to Cut instead. A
	 * shorted breaker refuses: the fault is in the flooded wiring, not the panel, and it is there for
	 * the attempt. A trip leaves shorted circuits as they are too.
	 */
	static bool FlipBreaker(FFacilityState& State, EFacilityCircuit Circuit);

	/**
	 * Starts the backup generator, if CanStartGenerator allows. Every cut circuit carries power for
	 * Config.GeneratorRunSteps steps; the breakers stay where they are, and what they say comes back
	 * when the run ends. Starting it is loud: the alarm rises one level.
	 */
	static bool StartGenerator(FFacilityState& State);

	/**
	 * Rigs the running generator to overload with the pry bar, if CanOverloadGenerator allows. It is
	 * finished for the attempt and trips every breaker as it goes, which seals the lockdown doors and
	 * parks the elevator with the rest of the blackout. The breakers can be thrown again afterwards.
	 */
	static bool OverloadGenerator(FFacilityState& State);

	/**
	 * Adds a lift with its configuration and where its car starts, then settles it against its circuit
	 * and the alarm. Nothing happens if the lift is already known. Registration is the only caller.
	 */
	static bool AddLift(FFacilityState& State, FName LiftId, const FLiftState& Lift);

	/**
	 * Records the handheld level a device needs. Nothing happens for a level of 0, which is no target
	 * at all, or for a device already known. Registration is the only caller.
	 */
	static bool AddHackTarget(FFacilityState& State, FName DeviceId, int32 HackLevel);

	/**
	 * Puts the car at a stop with no questions asked. For rules that park a car, like the elevator
	 * dropping to Level 1 when DOORS is cut. Unknown lifts are refused; AddLift is how a lift becomes
	 * known. The player goes through CallLift.
	 */
	static bool SetLiftStop(FFacilityState& State, FName LiftId, ELiftStop Stop);

	/** Sends the car to a stop. Nothing happens when GetLiftObstacle finds something in the way or the car is already there. */
	static bool CallLift(FFacilityState& State, FName LiftId, ELiftStop Stop);

	/**
	 * The player works the handheld on the lift, if CanHackLift allows. At or above the lift's level the
	 * hack succeeds silently: the car stops where it is and answers no more calls this attempt. Below it
	 * the hack fails and the alarm rises one level.
	 */
	static bool HackLift(FFacilityState& State, FName LiftId);

	/**
	 * Adds a door with its configuration and starting position, then settles it against its circuit.
	 * Nothing happens if the door is already known. Registration is the only caller.
	 */
	static bool AddDoor(FFacilityState& State, FName DoorId, const FDoorState& Door);

	/**
	 * Puts the door open or closed with no questions asked, except that nothing closes a door the pry
	 * bar forced. For rules that move a door on their own, like a seal; the player goes through
	 * ToggleDoor and ForceDoor.
	 */
	static bool SetDoorOpen(FFacilityState& State, FName DoorId, bool bOpen);

	/**
	 * Opens a closed door or closes an open one, if GetDoorObstacle finds nothing in the way. Never
	 * forces. Opening the gate under an alert is loud, see IsDoorLoudToOpen: the alarm rises one level.
	 */
	static bool ToggleDoor(FFacilityState& State, FName DoorId);

	/**
	 * The player works the handheld on the door, if CanHackDoor allows. At or above the door's level
	 * the hack succeeds silently and the door opens, loudly if it is the gate under an alert. Below it
	 * the hack fails and the alarm rises one level.
	 */
	static bool HackDoor(FFacilityState& State, FName DoorId);

	/** Adds a camera with the lights it sees by. Nothing happens if the camera is already known. Registration is the only caller. */
	static bool AddCamera(FFacilityState& State, FName CameraId, const FCameraState& Camera);

	/**
	 * The player works the handheld on the camera, if CanHackCamera allows. At or above the camera's
	 * level the hack succeeds silently and the camera loops for the rest of the attempt. Below it the
	 * hack fails and the alarm rises one level.
	 */
	static bool HackCamera(FFacilityState& State, FName CameraId);

	/** The camera saw the player. Nothing happens unless it is watching; otherwise the alarm rises one level. */
	static bool CameraSighting(FFacilityState& State, FName CameraId);

	/**
	 * The player works the handheld on the gate controller, if CanHackGateController allows. At or
	 * above the controller's level the hack succeeds silently and the gate opens, loudly if the alarm
	 * is up. Below it the hack fails and the alarm rises one level.
	 */
	static bool HackGateController(FFacilityState& State, FName ControllerId);

	/**
	 * Adds a switch's job to the routing panel and names the panel the first time. Nothing happens for
	 * a None panel, a second panel id, a routing job that names no device or no circuit, or a job the
	 * panel offers already. Registration is the only caller.
	 */
	static bool AddRoutingOption(FFacilityState& State, FName PanelId, const FRoutingOption& Option);

	/**
	 * The player works the handheld on a routing switch, if CanRouteDevice allows. At or above the
	 * panel's level the hack succeeds silently: the device moves onto the circuit and follows it at
	 * once, so a lift comes up or stops and the fans start or stop; whatever the panel had moved before
	 * goes back to its own circuit first; and a device the panel moved onto this circuit already goes
	 * home instead. Below the panel's level the hack fails and the alarm rises one level.
	 */
	static bool RouteDevice(FFacilityState& State, FName PanelId, FName DeviceId, EFacilityCircuit Circuit);

	/**
	 * The player works the handheld on the pump switch, if CanRunCoolantPump allows. At or above the
	 * panel's level the pump runs and Plant floods, quietly and with the valve untouched, so the fans
	 * can drain it. Below the panel's level the hack fails and the alarm rises one level.
	 */
	static bool RunCoolantPump(FFacilityState& State, FName PanelId);

	/**
	 * The player pries the door open with the pry bar, if CanForceDoor allows. The door opens and stays
	 * open for the rest of the attempt, and forcing is loud: the alarm rises one level. A door forced as
	 * that noise calls lockdown stays open, since lockdown cannot seal a forced door.
	 */
	static bool ForceDoor(FFacilityState& State, FName DoorId);

	/**
	 * Adds a pickup holding an item, lying untaken. Nothing happens if the pickup is already known or
	 * holds nothing. Registration is the only caller.
	 */
	static bool AddPickup(FFacilityState& State, FName PickupId, EFacilityItem Item);

	/**
	 * The player takes what lies at the pickup. Nothing happens if there is nothing there. Taking a
	 * second of a type the player already carries empties the pickup and changes nothing else.
	 */
	static bool TakePickup(FFacilityState& State, FName PickupId);

	/**
	 * Puts an item in the player's hands with no questions asked. For debugging and for rules that
	 * hand things over; the player goes through TakePickup.
	 */
	static bool GiveItem(FFacilityState& State, EFacilityItem Item);

	/** Takes an item out of the player's hands. For the neutral character taking a card or the locker item. */
	static bool TakeItem(FFacilityState& State, EFacilityItem Item);

	/**
	 * Names the fans that drain the flood. Nothing happens for None or once fans are named; the water
	 * itself is left alone. Registration is the only caller.
	 */
	static bool AddFlood(FFacilityState& State, FName FansId);

	/**
	 * Puts water on the Plant floor, from whatever source: the valve, the pump or the sprinklers. Nothing
	 * happens while it is flooded already; more water on standing water changes nothing, and the count
	 * toward the shorts and the corrosion keeps going.
	 */
	static bool FloodPlant(FFacilityState& State);

	/** The player forces the seized valve with the pry bar, if CanOpenCoolantValve allows. Plant floods, quietly. */
	static bool OpenCoolantValve(FFacilityState& State);

	/** The player closes the valve, if CanCloseCoolantValve allows. The water already down stays until the fans drain it. */
	static bool CloseCoolantValve(FFacilityState& State);

	/**
	 * The player rigs the server rack to overload with the pry bar, if CanOverloadServerRack allows. It
	 * burns for the rest of the attempt: the alarm goes to Alerted, or stays where it is if higher, and
	 * the sprinklers flood Plant. An event all the same, so the quiet count starts over.
	 */
	static bool OverloadServerRack(FFacilityState& State, FName RackId);

	/**
	 * Adds a crate where it starts and with the lift whose shaft it goes down. Nothing happens if the
	 * crate is already known. Registration is the only caller.
	 */
	static bool AddCrate(FFacilityState& State, FName CrateId, const FCrateState& Crate);

	/** The player moves the crate off the shutter to the shaft head, if CanMoveCrate allows. Quiet. */
	static bool MoveCrate(FFacilityState& State, FName CrateId);

	/**
	 * The player pushes the crate down the open shaft, if CanDropCrate allows. It lands in Plant on
	 * whatever stands below, which its actor kills, and the noise raises the alarm one level.
	 */
	static bool DropCrate(FFacilityState& State, FName CrateId);

	/**
	 * Adds what guards an exit: the door the player leaves through and the fans that make it lethal.
	 * Nothing happens if the exit is None or already known. Registration is the only caller.
	 */
	static bool AddExit(FFacilityState& State, EFacilityExit Exit, const FExitState& ExitState);

	/** The player passes through an exit. Nothing happens unless CanEscape allows. This is the win. */
	static bool Escape(FFacilityState& State, EFacilityExit Exit);

	/**
	 * Puts the alarm at a level and settles the lockdown doors and the gate against it. None is not
	 * a level and is refused. For the console and for debugging; events go through
	 * RaiseAlarmLevelByOne.
	 */
	static bool SetAlarmLevel(FFacilityState& State, EAlarmLevel NewLevel);

	/**
	 * Raises the alarm one level. Noise, a sighting, a failed hack and loud sabotage each call this
	 * once per event. Nothing happens at Lockdown, which is the top, except that the noise still
	 * starts the quiet count over, so a lockdown holds while noise keeps coming.
	 */
	static bool RaiseAlarmLevelByOne(FFacilityState& State);

	/**
	 * Lowers the alarm one level. Quiet steps and silencing at a powered console call this. Nothing
	 * happens at Quiet, which is the bottom.
	 */
	static bool LowerAlarmLevelByOne(FFacilityState& State);

	/** The player silences the alarm at the console, if CanSilenceAlarm allows: it comes down one level. */
	static bool SilenceAlarm(FFacilityState& State, FName ConsoleId);

	/**
	 * One step of the facility's clock. A step is the unit the GDD counts in: the alarm comes down one
	 * level after Config.AlarmDecaySteps quiet steps, the generator burns one of its steps and stops
	 * when it runs out, and the flood stands one more: drained if the fans are running, otherwise
	 * shorting every live circuit once it has stood long enough and eating through the tunnel lock
	 * after that. The winch will count here too. Returns true when the step changed something the
	 * player can see: the alarm coming down, the generator's count moving, which its prompt shows, or
	 * the flood draining, shorting or corroding. The quiet count and the flood's count are bookkeeping.
	 */
	static bool AdvanceStep(FFacilityState& State);
};

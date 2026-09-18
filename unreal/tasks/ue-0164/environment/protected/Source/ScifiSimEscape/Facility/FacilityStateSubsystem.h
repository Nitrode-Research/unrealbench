#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "Facility/FacilityTypes.h"
#include "FacilityStateSubsystem.generated.h"

class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFacilityStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAlarmLevelChanged, EAlarmLevel, OldLevel, EAlarmLevel, NewLevel);

/**
 * Owns the facility's world state and is the only thing that writes it.
 *
 * Everything gameplay-relevant about the facility lives in the one FFacilityState here: circuit
 * states, which circuit each device is routed to, where each lift's car is, which doors are open and
 * what holds them, what the player carries and which pickups still lie where they were placed, what
 * guards each exit and which one the player escaped through, and how alarmed the facility is. Actors
 * keep an id into it and ask predicates like IsDevicePowered instead of caching answers. The rules
 * themselves live in FFacilityRules, written over a bare state, so the route solver can run the same
 * code over hypothetical states without a world.
 *
 * Every mutation goes through a method here and, if it changed anything, ends in exactly one
 * OnStateChanged broadcast. The broadcast carries no payload on purpose: listeners re-read what
 * they care about, so adding state never means adding delegates. The alarm is the one exception:
 * moving it is an event as much as a value, and what reacts to it wants to know which way it went,
 * so OnAlarmLevelChanged follows OnStateChanged with the level it left and the one it reached.
 *
 * Where an attempt starts is kept as a value, InitialState: the GDD defaults from
 * FFacilityRules::SetInitialState plus the circuit every device was placed on, the stop every lift
 * was parked at, every door as it was placed and every pickup lying untaken. ResetToInitialState
 * puts the live state back to it, which is what a player death does, and the route solver starts
 * its search from the same value.
 *
 * The values the rules are tuned with come from UFacilitySettings, which is DefaultGame.ini: the
 * scalars go into the state as FFacilityConfig when it is initialized, and the per-id tables, what
 * each door's lock takes, what lies at each pickup and the handheld level each device needs, are
 * looked up as the actors register their ids. So the tuning is text the repository can review, and
 * the route graph is built under the same numbers as the game. Once play begins the subsystem checks
 * the tables against what registered and warns about an entry no actor claimed. The facility also
 * keeps a clock: every StepSeconds it advances the state one step, the unit the GDD counts alarm
 * decay, the generator and the flood in.
 */
UCLASS()
class SCIFISIMESCAPE_API UFacilityStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Fires after any change to the facility state. */
	UPROPERTY(BlueprintAssignable, Category = "Facility")
	FOnFacilityStateChanged OnStateChanged;

	/**
	 * Fires after the alarm moves to another level, after the OnStateChanged of the same change, so
	 * every door has already followed the new level by the time a listener hears it. Fires for a
	 * reset too, with IsResetting set, when the reset brought the alarm back down. A siren, the
	 * guards' placement and the HUD bind here; anything that only reads the level binds OnStateChanged.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Facility|Alarm")
	FOnAlarmLevelChanged OnAlarmLevelChanged;

	//
	// Predicates. Read-only and safe to call from anywhere.
	//

	UFUNCTION(BlueprintPure, Category = "Facility|Power")
	ECircuitState GetCircuitState(EFacilityCircuit Circuit) const;

	UFUNCTION(BlueprintPure, Category = "Facility|Power")
	bool IsCircuitLive(EFacilityCircuit Circuit) const;

	/** Circuit the device is routed to. None if the device is unknown or wired to nothing. */
	UFUNCTION(BlueprintPure, Category = "Facility|Power")
	EFacilityCircuit GetDeviceCircuit(FName DeviceId) const;

	/** True when the device is routed to a live circuit. Unknown devices are never powered. */
	UFUNCTION(BlueprintPure, Category = "Facility|Power")
	bool IsDevicePowered(FName DeviceId) const;

	/** True while the breaker is on, what the panel's switch shows. See FFacilityRules::IsBreakerOn. */
	UFUNCTION(BlueprintPure, Category = "Facility|Power")
	bool IsBreakerOn(EFacilityCircuit Circuit) const;

	/** True while the backup generator runs and carries every cut circuit. */
	UFUNCTION(BlueprintPure, Category = "Facility|Generator")
	bool IsGeneratorRunning() const;

	/** Steps the generator has left, 0 while it is off. */
	UFUNCTION(BlueprintPure, Category = "Facility|Generator")
	int32 GetGeneratorStepsLeft() const;

	/** True once the generator has been rigged to overload. It is finished for the attempt. */
	UFUNCTION(BlueprintPure, Category = "Facility|Generator")
	bool IsGeneratorOverloaded() const;

	/** True when the generator can be started. See FFacilityRules::CanStartGenerator. */
	UFUNCTION(BlueprintPure, Category = "Facility|Generator")
	bool CanStartGenerator() const;

	/** True when the pry bar would rig the generator to overload right now. See FFacilityRules::CanOverloadGenerator. */
	UFUNCTION(BlueprintPure, Category = "Facility|Generator")
	bool CanOverloadGenerator() const;

	/** Stop the lift's car is at. Bottom if the lift is unknown. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lifts")
	ELiftStop GetLiftStop(FName LiftId) const;

	/** True when the lift answers a call. See FFacilityRules::CanUseLift for what that means. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lifts")
	bool CanUseLift(FName LiftId) const;

	/** Why the lift will not answer a call right now, None when it will. See FFacilityRules::GetLiftObstacle for the order. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lifts")
	ELiftObstacle GetLiftObstacle(FName LiftId) const;

	/** True once the handheld has stopped the lift. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lifts")
	bool IsLiftHacked(FName LiftId) const;

	/** True when the handheld has something to work on in the lift. See FFacilityRules::CanHackLift for what that means. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lifts")
	bool CanHackLift(FName LiftId) const;

	/** True when the door is open. Unknown doors are closed. */
	UFUNCTION(BlueprintPure, Category = "Facility|Doors")
	bool IsDoorOpen(FName DoorId) const;

	/** Why the door will not move right now, None when it will. See FFacilityRules::GetDoorObstacle for the order. */
	UFUNCTION(BlueprintPure, Category = "Facility|Doors")
	EDoorObstacle GetDoorObstacle(FName DoorId) const;

	/** True when the door is closed and nothing stops it opening. */
	UFUNCTION(BlueprintPure, Category = "Facility|Doors")
	bool CanOpenDoor(FName DoorId) const;

	/** True when the door is open and nothing stops it closing. */
	UFUNCTION(BlueprintPure, Category = "Facility|Doors")
	bool CanCloseDoor(FName DoorId) const;

	/** True when the pry bar would force the door open right now. See FFacilityRules::CanForceDoor for what that means. */
	UFUNCTION(BlueprintPure, Category = "Facility|Doors")
	bool CanForceDoor(FName DoorId) const;

	/** True when the handheld has something to work on in the door. See FFacilityRules::CanHackDoor for what that means. */
	UFUNCTION(BlueprintPure, Category = "Facility|Doors")
	bool CanHackDoor(FName DoorId) const;

	/** True when opening the door raises the alarm: the gate under an alert. See FFacilityRules::IsDoorLoudToOpen. */
	UFUNCTION(BlueprintPure, Category = "Facility|Doors")
	bool IsDoorLoudToOpen(FName DoorId) const;

	/** True when the player carries the item. */
	UFUNCTION(BlueprintPure, Category = "Facility|Items")
	bool HasItem(EFacilityItem Item) const;

	/** Everything the player carries, in no particular order. */
	UFUNCTION(BlueprintPure, Category = "Facility|Items")
	TArray<EFacilityItem> GetInventory() const;

	/** Highest keycard level the player carries. 0 without a card. */
	UFUNCTION(BlueprintPure, Category = "Facility|Items")
	int32 GetKeycardLevel() const;

	/** Handheld level the device needs, 0 when the handheld has nothing to work on there. */
	UFUNCTION(BlueprintPure, Category = "Facility|Hacking")
	int32 GetHackLevel(FName DeviceId) const;

	/** Hack level of the handheld. See FFacilityRules::GetHandheldLevel. */
	UFUNCTION(BlueprintPure, Category = "Facility|Hacking")
	int32 GetHandheldLevel() const;

	/** True when a hack on a target of that level would succeed. See FFacilityRules::WouldHackSucceed. */
	UFUNCTION(BlueprintPure, Category = "Facility|Hacking")
	bool WouldHackSucceed(int32 TargetLevel) const;

	/** True once the player has pulled the drive from the Lab server rack, which is what blinds the camera. */
	UFUNCTION(BlueprintPure, Category = "Facility|Hacking")
	bool IsServerDrivePulled() const;

	/** What lies at the pickup, taken or not. None if the pickup is unknown. */
	UFUNCTION(BlueprintPure, Category = "Facility|Items")
	EFacilityItem GetPickupItem(FName PickupId) const;

	/** True once the pickup has been taken, or when the state does not know it. */
	UFUNCTION(BlueprintPure, Category = "Facility|Items")
	bool IsPickupTaken(FName PickupId) const;

	/** True when the pickup still lies where it was placed and holds something. */
	UFUNCTION(BlueprintPure, Category = "Facility|Items")
	bool CanTakePickup(FName PickupId) const;

	/** True when the fans are turning, which is when they kill. See FFacilityRules::AreFansRunning for what that means. */
	UFUNCTION(BlueprintPure, Category = "Facility|Fans")
	bool AreFansRunning(FName FansId) const;

	/** True when the zone's lights are on. See FFacilityRules::AreLightsOn for what that means. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lights")
	bool AreLightsOn(FName LightsId) const;

	/** True once the handheld has looped the camera. */
	UFUNCTION(BlueprintPure, Category = "Facility|Camera")
	bool IsCameraLooped(FName CameraId) const;

	/** True while the camera sees. See FFacilityRules::IsCameraWatching for what that means. */
	UFUNCTION(BlueprintPure, Category = "Facility|Camera")
	bool IsCameraWatching(FName CameraId) const;

	/** True when the handheld has something to work on in the camera. See FFacilityRules::CanHackCamera. */
	UFUNCTION(BlueprintPure, Category = "Facility|Camera")
	bool CanHackCamera(FName CameraId) const;

	/** True when the handheld has something to work on in the gate controller. See FFacilityRules::CanHackGateController. */
	UFUNCTION(BlueprintPure, Category = "Facility|Doors")
	bool CanHackGateController(FName ControllerId) const;

	/** True once the flood has eaten through the door's lock. See FFacilityRules::IsDoorLockCorroded. */
	UFUNCTION(BlueprintPure, Category = "Facility|Doors")
	bool IsDoorLockCorroded(FName DoorId) const;

	/** True when the handheld has the routing panel to work on at all: it is registered, has a hack level and has power. See FFacilityRules::CanUseRoutingPanel. */
	UFUNCTION(BlueprintPure, Category = "Facility|Routing")
	bool CanUseRoutingPanel(FName PanelId) const;

	/** The one device the routing panel has moved off the circuit it was placed on, None while every device is home. */
	UFUNCTION(BlueprintPure, Category = "Facility|Routing")
	FName GetPatchedDevice() const;

	/** True while the routing panel has this device on another circuit. */
	UFUNCTION(BlueprintPure, Category = "Facility|Routing")
	bool IsDevicePatched(FName DeviceId) const;

	/** Circuit the device was placed on, whatever the routing panel has done since. See FFacilityRules::GetHomeCircuit. */
	UFUNCTION(BlueprintPure, Category = "Facility|Routing")
	EFacilityCircuit GetHomeCircuit(FName DeviceId) const;

	/** True when the switch that routes this device onto this circuit has something to do. See FFacilityRules::CanRouteDevice. */
	UFUNCTION(BlueprintPure, Category = "Facility|Routing")
	bool CanRouteDevice(FName PanelId, FName DeviceId, EFacilityCircuit Circuit) const;

	/** True when the pump switch has something to do: the panel is usable and the Plant floor is dry. See FFacilityRules::CanRunCoolantPump. */
	UFUNCTION(BlueprintPure, Category = "Facility|Routing")
	bool CanRunCoolantPump(FName PanelId) const;

	/** True while water stands on the Plant floor. */
	UFUNCTION(BlueprintPure, Category = "Facility|Flood")
	bool IsPlantFlooded() const;

	/** Steps the water has stood, 0 while the floor is dry. */
	UFUNCTION(BlueprintPure, Category = "Facility|Flood")
	int32 GetFloodSteps() const;

	/** True while standing in the water kills: flooded, and any circuit carries power. See FFacilityRules::IsFloodLethal. */
	UFUNCTION(BlueprintPure, Category = "Facility|Flood")
	bool IsFloodLethal() const;

	/** True when the next step drains the water: the fans are running and the valve is closed. See FFacilityRules::WouldFansDrainFlood. */
	UFUNCTION(BlueprintPure, Category = "Facility|Flood")
	bool WouldFansDrainFlood() const;

	UFUNCTION(BlueprintPure, Category = "Facility|Flood")
	bool IsCoolantValveOpen() const;

	/** True when the pry bar would open the seized valve right now. See FFacilityRules::CanOpenCoolantValve. */
	UFUNCTION(BlueprintPure, Category = "Facility|Flood")
	bool CanOpenCoolantValve() const;

	/** True when the valve is open and can be closed by hand. */
	UFUNCTION(BlueprintPure, Category = "Facility|Flood")
	bool CanCloseCoolantValve() const;

	/** True once the server rack was rigged to overload and burns. */
	UFUNCTION(BlueprintPure, Category = "Facility|Server Rack")
	bool IsServerRackOverloaded() const;

	/** True while the Lab is full of smoke guards will not enter, which is for as long as the rack burns. */
	UFUNCTION(BlueprintPure, Category = "Facility|Server Rack")
	bool IsLabSmokeFilled() const;

	/** True when the pry bar would rig the rack right now. See FFacilityRules::CanOverloadServerRack. */
	UFUNCTION(BlueprintPure, Category = "Facility|Server Rack")
	bool CanOverloadServerRack(FName RackId) const;

	/** Where the crate is. An unknown crate reads as in Plant. */
	UFUNCTION(BlueprintPure, Category = "Facility|Crates")
	ECratePosition GetCratePosition(FName CrateId) const;

	/** True while the crate is wedged in the dock shutter. */
	UFUNCTION(BlueprintPure, Category = "Facility|Crates")
	bool IsCrateJammingShutter(FName CrateId) const;

	/** True while any crate jams the dock shutter. */
	UFUNCTION(BlueprintPure, Category = "Facility|Crates")
	bool IsDockShutterJammed() const;

	/** True once the crate was pushed down the shaft rather than ridden down. */
	UFUNCTION(BlueprintPure, Category = "Facility|Crates")
	bool WasCrateDropped(FName CrateId) const;

	/** True when the crate can be moved off the shutter right now. See FFacilityRules::CanMoveCrate. */
	UFUNCTION(BlueprintPure, Category = "Facility|Crates")
	bool CanMoveCrate(FName CrateId) const;

	/** True when the crate can be pushed down the open shaft right now. See FFacilityRules::CanDropCrate. */
	UFUNCTION(BlueprintPure, Category = "Facility|Crates")
	bool CanDropCrate(FName CrateId) const;

	/** True once the player has passed through an exit. The win. */
	UFUNCTION(BlueprintPure, Category = "Facility|Exits")
	bool HasEscaped() const;

	/** The exit the player passed through, None while they are still inside. */
	UFUNCTION(BlueprintPure, Category = "Facility|Exits")
	EFacilityExit GetEscapeExit() const;

	/** True when the exit lets the player through right now. See FFacilityRules::CanEscape for what that means. */
	UFUNCTION(BlueprintPure, Category = "Facility|Exits")
	bool CanEscape(EFacilityExit Exit) const;

	/** How alarmed the facility is. Quiet when an attempt starts. */
	UFUNCTION(BlueprintPure, Category = "Facility|Alarm")
	EAlarmLevel GetAlarmLevel() const;

	/** True while lockdown is called, which seals the lockdown doors and the main gate. See FFacilityRules::IsLockdown for what else it does. */
	UFUNCTION(BlueprintPure, Category = "Facility|Alarm")
	bool IsLockdown() const;

	/** True when using the alarm console brings the alarm down. See FFacilityRules::CanSilenceAlarm for what that means. */
	UFUNCTION(BlueprintPure, Category = "Facility|Alarm")
	bool CanSilenceAlarm(FName ConsoleId) const;

	/** Quiet steps counted so far toward the alarm coming down a level, and how many it takes. For the HUD and the log. */
	UFUNCTION(BlueprintPure, Category = "Facility|Alarm")
	int32 GetQuietSteps() const { return State.QuietSteps; }

	UFUNCTION(BlueprintPure, Category = "Facility|Alarm")
	int32 GetAlarmDecaySteps() const { return State.Config.AlarmDecaySteps; }

	/**
	 * An actor registered under this id that is alive in this world, null when there is none. When
	 * several actors share the id, like the lights of one zone, it is the first of them to register
	 * that is still alive.
	 */
	UFUNCTION(BlueprintPure, Category = "Facility")
	AActor* FindDeviceActor(FName DeviceId) const;

	/**
	 * True only while the OnStateChanged broadcast of a reset is running. A listener that normally
	 * animates toward the state snaps to it instead when this is set: a reset is a new attempt, not
	 * something that happened in the world.
	 */
	UFUNCTION(BlueprintPure, Category = "Facility")
	bool IsResetting() const { return bResetting; }

	const FFacilityState& GetState() const { return State; }

	/** Where an attempt starts. What ResetToInitialState restores and what the route solver starts from. */
	const FFacilityState& GetInitialState() const { return InitialState; }

	//
	// Mutations. Each broadcasts OnStateChanged if it changed anything.
	//

	/**
	 * Records the actor under its id, and seeds the device's routing the first time the id is seen, in
	 * the live state and in the initial state, along with the handheld level the settings give the id,
	 * if any. If the id is already known the live state wins: a device the panel rerouted keeps that
	 * routing across being destroyed and re-created, and only ResetToInitialState puts it back.
	 *
	 * Several actors may hold one id when every one of them shares it, which bSharesId says for this
	 * one, from AFacilityDeviceBase::SharesDeviceId: every light in a zone, or the two vent fans. They
	 * are one device with one routing entry, which keeps the circuit the id was first placed on, so a
	 * sharing actor placed on a different circuit logs a warning. Two actors on one id where either does
	 * not share log an error, since they share one state all the same.
	 *
	 * Does not broadcast. Registration is set-up during BeginPlay, not something the world reacts to.
	 */
	void RegisterDevice(AActor* DeviceActor, FName DeviceId, EFacilityCircuit DefaultCircuit, bool bSharesId);

	/**
	 * Forgets this actor under the id. Other actors sharing the id stay registered, and the routing
	 * entry stays either way; that is state, not registration.
	 */
	void UnregisterDevice(AActor* DeviceActor, FName DeviceId);

	/**
	 * Seeds a lift the first time its id is seen, in the live state and in the initial state, the way
	 * RegisterDevice seeds routing: which lift it is, its hack level and where its car starts. If the id
	 * is already known the live state wins. Does not broadcast.
	 */
	void RegisterLift(FName LiftId, const FLiftState& Lift);

	/**
	 * Seeds a door the first time its id is seen, in the live state and in the initial state, the way
	 * RegisterDevice seeds routing: its kind and whether it starts open from the actor, and what its
	 * lock takes from the settings by id, no lock when the settings name none. If the id is already
	 * known the live state wins. Does not broadcast.
	 */
	void RegisterDoor(FName DoorId, EDoorKind Kind, bool bInitiallyOpen);

	/**
	 * Seeds a pickup the first time its id is seen, in the live state and in the initial state, the
	 * way RegisterDevice seeds routing, with what the settings say lies there; a pickup the settings do
	 * not name holds nothing and logs a warning. If the id is already known the live state wins, so a
	 * taken pickup stays taken across its actor being re-created. Does not broadcast.
	 */
	void RegisterPickup(FName PickupId);

	/**
	 * Seeds what guards an exit the first time it is seen, in the live state and in the initial state,
	 * the way RegisterDoor seeds a door: the door the player leaves through and the fans that make it
	 * lethal. If the exit is already known it is left alone. Does not broadcast.
	 */
	void RegisterExit(EFacilityExit Exit, const FExitState& ExitState);

	/**
	 * Seeds a camera the first time its id is seen, in the live state and in the initial state, the way
	 * RegisterDoor seeds a door: the lights it sees by. If the id is already known the live state wins,
	 * so a looped camera stays looped across its actor being re-created. Does not broadcast.
	 */
	void RegisterCamera(FName CameraId, const FCameraState& Camera);

	/**
	 * Names the fans that drain the flood, in the live state and in the initial state, the first time
	 * any are named. The water itself is left alone, so a standing flood survives its actor being
	 * re-created. Does not broadcast.
	 */
	void RegisterFlood(FName FansId);

	/**
	 * Seeds a crate the first time its id is seen, in the live state and in the initial state, the way
	 * RegisterDoor seeds a door: jamming the shutter, with the lift whose shaft it goes down. If the id
	 * is already known the live state wins. Does not broadcast.
	 */
	void RegisterCrate(FName CrateId, FName LiftId);

	/**
	 * Seeds one job of the routing panel, in the live state and in the initial state, the way
	 * RegisterExit seeds an exit: the switch's job under the panel's id, which names the panel the first
	 * time. A job the panel offers already is left alone; a switch under a second panel id or with a job
	 * that names nothing is refused and logged. Does not broadcast.
	 */
	void RegisterRoutingOption(FName PanelId, const FRoutingOption& Option);

	UFUNCTION(BlueprintCallable, Category = "Facility|Power")
	void SetCircuitState(EFacilityCircuit Circuit, ECircuitState NewState);

	/** Moves a device to another circuit with no questions asked. For Blueprint and debugging; the routing panel goes through RouteDevice, which keeps its one patch. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Power")
	void SetDeviceCircuit(FName DeviceId, EFacilityCircuit NewCircuit);

	/**
	 * Throws one breaker on the panel in Plant. A live circuit is cut. A dead one comes up live, unless
	 * two others already are, in which case the whole panel trips. See FFacilityRules::FlipBreaker.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Power")
	void FlipBreaker(EFacilityCircuit Circuit);

	/**
	 * Starts the backup generator. This is what the generator does when used. Every cut circuit carries
	 * power for the configured steps, and the noise raises the alarm one level. Returns whether anything
	 * changed; when nothing did, the log says why.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Generator")
	bool StartGenerator();

	/**
	 * Rigs the running generator to overload with the pry bar. This is what the generator does when used
	 * while it runs and the player carries the pry bar. Every breaker trips and the generator is finished
	 * for the attempt. Returns whether anything changed; when nothing did, the log says why.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Generator")
	bool OverloadGenerator();

	/**
	 * Sends a lift's car to a stop. This is what the car and its call panels do. Returns whether
	 * anything changed: false when the rules refused or the car was already there.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Lifts")
	bool CallLift(FName LiftId, ELiftStop Stop);

	/**
	 * The player works the handheld on a lift. This is what the car and its call panels do on the hack
	 * input. A hack the handheld reaches stops the car silently; one it cannot reach fails and raises
	 * the alarm. Returns whether anything changed; the log says which it was.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Lifts")
	bool HackLift(FName LiftId);

	/**
	 * Opens a closed door or closes an open one if the rules allow, never by force. This is what a door
	 * does when used and nothing holds it. Returns whether anything changed; when nothing did, the log
	 * says what stood in the way.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Doors")
	bool ToggleDoor(FName DoorId);

	/**
	 * Pries the door open with the pry bar if the rules allow. This is what a door does when used while
	 * a seal, a dead circuit or the lock holds it and the player carries the pry bar. The door stays open
	 * for the rest of the attempt, and the noise raises the alarm one level. Returns whether anything
	 * changed; when nothing did, the log says why.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Doors")
	bool ForceDoor(FName DoorId);

	/**
	 * The player works the handheld on a door. This is what a door does on the hack input while only its
	 * lock holds it. A hack the handheld reaches opens the door silently; one it cannot reach fails and
	 * raises the alarm. Returns whether anything changed; the log says which it was.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Doors")
	bool HackDoor(FName DoorId);

	/**
	 * The player works the handheld on the gate controller. This is what the controller does on the hack
	 * input while only the lock holds the gate. A hack the handheld reaches opens the gate silently; one
	 * it cannot reach fails and raises the alarm. Returns whether anything changed; the log says which.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Doors")
	bool HackGateController(FName ControllerId);

	/**
	 * The player works the handheld on a camera. This is what the camera does on the hack input. A hack
	 * the handheld reaches loops the camera silently; one it cannot reach fails and raises the alarm.
	 * Returns whether anything changed; the log says which.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Camera")
	bool HackCamera(FName CameraId);

	/**
	 * A camera saw the player. This is what the camera reports when the player is in its view while it
	 * watches. The alarm rises one level. Returns whether anything changed: false when the camera was
	 * not watching after all, or the alarm is already at the top.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Camera")
	bool CameraSighting(FName CameraId);

	/**
	 * The player works the handheld on a routing switch. This is what the switch does on the hack input.
	 * A hack the handheld reaches moves the device onto the circuit silently, sending whatever the panel
	 * moved before back home, or sends this device home if the panel put it there already; one it cannot
	 * reach fails and raises the alarm. Returns whether anything changed; the log says which.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Routing")
	bool RouteDevice(FName PanelId, FName DeviceId, EFacilityCircuit Circuit);

	/**
	 * The player works the handheld on the routing panel's pump switch. This is what the switch does on
	 * the hack input. A hack the handheld reaches runs the pump and floods Plant silently; one it cannot
	 * reach fails and raises the alarm. Returns whether anything changed; the log says which.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Routing")
	bool RunCoolantPump(FName PanelId);

	/**
	 * Puts water on the Plant floor. This is what the coolant valve, the routing panel's pump and the
	 * server rack's sprinklers do, and the console command Facility.FloodPlant. Returns whether anything
	 * changed: false while the floor is flooded already.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Flood")
	bool FloodPlant();

	/**
	 * The player forces the seized coolant valve open with the pry bar. This is what the valve does when
	 * used while closed. Plant floods. Returns whether anything changed; when nothing did, the log says why.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Flood")
	bool OpenCoolantValve();

	/** The player closes the valve by hand. This is what the valve does when used while open. Returns whether anything changed. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Flood")
	bool CloseCoolantValve();

	/**
	 * The player rigs the server rack to overload with the pry bar. This is what the rack does when used.
	 * It burns for the attempt, the alarm goes to Alerted and the sprinklers flood Plant. Returns whether
	 * anything changed; when nothing did, the log says why.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Server Rack")
	bool OverloadServerRack(FName RackId);

	/**
	 * The player moves the crate off the dock shutter to the shaft head. This is what the crate does when
	 * used while it jams the shutter. Returns whether anything changed; when nothing did, the log says why.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Crates")
	bool MoveCrate(FName CrateId);

	/**
	 * The player pushes the crate down the open shaft. This is what the crate does when used at the shaft
	 * head with the car at the bottom. It lands in Plant, and the noise raises the alarm one level. Returns
	 * whether anything changed; when nothing did, the log says why.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Crates")
	bool DropCrate(FName CrateId);

	/**
	 * The player takes what lies at the pickup. This is what a pickup does when used. Returns whether
	 * anything changed: false when there was nothing there.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Items")
	bool TakePickup(FName PickupId);

	/**
	 * Puts an item in the player's hands without a pickup. For the console command Facility.GiveItem
	 * and for rules that hand things over. Returns whether anything changed: false for an item the
	 * player already carries.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Items")
	bool GiveItem(EFacilityItem Item);

	/** Takes an item out of the player's hands. For the neutral character. Returns whether anything changed. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Items")
	bool TakeItem(EFacilityItem Item);

	/**
	 * The player passes through an exit. This is what an AFacilityExit calls when the player is on
	 * its threshold, and it is the win: the game mode reads HasEscaped from the broadcast. Returns
	 * whether anything changed, false when the exit is not open or the player has already escaped.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Exits")
	bool Escape(EFacilityExit Exit);

	/**
	 * Puts the alarm at a level. For the console command Facility.SetAlarmLevel and for anything
	 * that names a level outright; events go through RaiseAlarmLevelByOne. Returns whether anything
	 * changed: false for the level it is already at, and for None, which is not a level.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Alarm")
	bool SetAlarmLevel(EAlarmLevel NewLevel);

	/**
	 * Raises the alarm one level. This is what noise, a sighting, a failed hack and loud sabotage
	 * call, once per event. Returns whether anything changed: false at Lockdown, which is the top.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Alarm")
	bool RaiseAlarmLevelByOne();

	/**
	 * Lowers the alarm one level. This is what a run of quiet steps and silencing at a powered
	 * console call. Returns whether anything changed: false at Quiet, which is the bottom.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Alarm")
	bool LowerAlarmLevelByOne();

	/** Puts the alarm straight back to Quiet, as it was when the attempt started. Returns whether anything changed. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Alarm")
	bool ResetAlarmLevel();

	/**
	 * The player silences the alarm at a console. This is what the alarm console does when used. The
	 * alarm comes down one level while the console has power. Returns whether anything changed; when
	 * nothing did, the log says why.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Alarm")
	bool SilenceAlarm(FName ConsoleId);

	/**
	 * Advances the facility one step. The clock calls this every StepSeconds, and the console command
	 * Facility.AdvanceStep calls it by hand. Returns whether the step changed anything visible, which
	 * today is the alarm coming down a level after enough quiet steps.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility")
	bool AdvanceStep();

	/**
	 * Puts the facility back to where the attempt started: breakers to the GDD defaults, every device
	 * on the circuit it was placed with, every lift's car at the stop it was parked at, every door as
	 * it was placed, every pickup back and the player's hands empty, the player back inside and the
	 * alarm quiet. This is what a player death calls. Always broadcasts OnStateChanged with
	 * IsResetting set, because every listener has to re-read after a reset even when its own values
	 * happen to match, and OnAlarmLevelChanged as well when the alarm came down.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility")
	void ResetToInitialState();

protected:

	/** Game and PIE worlds only. The editor world places devices but never powers them. */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Seconds of play per step, from UFacilitySettings at Initialize. Zero stops the clock, for stepping by hand with Facility.AdvanceStep. */
	float StepSeconds = 5.f;

private:

	void BroadcastIfChanged(bool bChanged);

	/**
	 * Warns about every settings entry no actor claimed: a door lock, a pickup or a hack target whose
	 * id nothing registered under. Such an entry is tuning for something that is not in the world,
	 * which is either a typo or a placement still to do. Runs one tick after play begins, since the
	 * subsystem is told the world has begun play before the actors are, and they register in BeginPlay.
	 */
	void ReportUnclaimedSettings();

	/** The clock. Advances the state one step. */
	void HandleStepTimer();

	/** Broadcasts OnAlarmLevelChanged if the alarm is no longer at OldLevel. Called after BroadcastIfChanged by everything that can move it. */
	void BroadcastAlarmLevelIfChanged(EAlarmLevel OldLevel);

	UPROPERTY(VisibleAnywhere, Category = "Facility")
	FFacilityState State;

	/**
	 * Where an attempt starts. Circuits come from FFacilityRules::SetInitialState at Initialize;
	 * device routing, lift stops, doors and pickups are seeded as their actors register. Nothing
	 * writes it after that.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Facility")
	FFacilityState InitialState;

	/** One actor registered under a device id, and whether it lets other actors hold that id too. */
	struct FRegisteredDevice
	{
		TWeakObjectPtr<AActor> Actor;

		/** What the actor's SharesDeviceId said when it registered. */
		bool bSharesId = false;
	};

	/**
	 * Live actors by device id, in the order they registered, each with whether it shares the id. Not
	 * world state. It exists to catch an id claimed by two actors that do not both share it, and to let
	 * panels and debug tools find an actor behind an id. An id holds several actors only when they share
	 * it, like the lights of one zone. An actor's entry goes when it unregisters, and the id goes with
	 * its last one.
	 */
	TMap<FName, TArray<FRegisteredDevice>> DeviceActors;

	/** Set for the duration of the broadcast inside ResetToInitialState. See IsResetting. */
	bool bResetting = false;

	/** The clock, started when the world begins play and looping every StepSeconds. */
	FTimerHandle StepTimerHandle;
};

#pragma once

#include "CoreMinimal.h"
#include "FacilityTypes.generated.h"

/**
 * The three breaker circuits in Plant. Every powered device draws from exactly one of them.
 * None is a device that is not wired to anything and can never be powered.
 */
UENUM(BlueprintType)
enum class EFacilityCircuit : uint8
{
	EFC_None		UMETA(DisplayName = "None"),
	EFC_Doors		UMETA(DisplayName = "DOORS"),
	EFC_Security	UMETA(DisplayName = "SECURITY"),
	EFC_Plant		UMETA(DisplayName = "PLANT"),
};

/**
 * What a circuit's breaker is doing. Deliberately not a bool: Cut and Shorted are both dead at the
 * panel, but the lockdown doors seal on Cut and fail open on Shorted, so devices have to tell them
 * apart. Whether the circuit carries power is FFacilityRules::IsCircuitLive, which also asks the
 * backup generator: it carries a cut circuit while it runs, and nothing carries a shorted one.
 */
UENUM(BlueprintType)
enum class ECircuitState : uint8
{
	/** Breaker off. Dead unless the generator runs. Lockdown doors seal. */
	Cut			UMETA(DisplayName = "Cut"),
	/** Breaker on, delivering power. */
	Live		UMETA(DisplayName = "Live"),
	/** Killed by flood. Dead, generator or not. Lockdown doors fail open. */
	Shorted		UMETA(DisplayName = "Shorted"),
};

/**
 * The two ends of a lift shaft. Named by direction, not by room: the elevator runs Corridor to
 * Storage and the cargo lift Storage to Plant, and neither the state nor the lift class knows which.
 */
UENUM(BlueprintType)
enum class ELiftStop : uint8
{
	ELS_Bottom	UMETA(DisplayName = "Bottom"),
	ELS_Top		UMETA(DisplayName = "Top"),
};

/**
 * Which of the two lifts a car is. The elevator has rules of its own: it parks on Level 1, its bottom
 * stop, while its circuit is dead or lockdown is called, and the handheld can stop it. The cargo lift
 * only needs power.
 */
UENUM(BlueprintType)
enum class ELiftKind : uint8
{
	CargoLift	UMETA(DisplayName = "Cargo lift"),
	Elevator	UMETA(DisplayName = "Elevator"),
};

/**
 * Why a lift will not answer a call right now. It is what the car and its call panels show, and what
 * the route solver reads to tell a lift that wants power from one the handheld stopped.
 */
UENUM(BlueprintType)
enum class ELiftObstacle : uint8
{
	/** Nothing. The car goes where it is called. */
	None		UMETA(DisplayName = "None"),
	/** The handheld stopped it. It stays stopped for the rest of the attempt. */
	Hacked		UMETA(DisplayName = "Hacked"),
	/** On a circuit that is not live. */
	NoPower		UMETA(DisplayName = "No power"),
	/** The elevator while lockdown is called. */
	Lockdown	UMETA(DisplayName = "Lockdown"),
};

/**
 * One lift in the facility state. Stop and bHacked are the parts that change. The rest is
 * configuration, seeded by the lift's actor when it registers and never written after. As far as the
 * rules are concerned the car is always at exactly one stop; the actor animating between them is
 * presentation, and the route solver never sees it.
 */
USTRUCT(BlueprintType)
struct SCIFISIMESCAPE_API FLiftState
{
	GENERATED_BODY()

	/** Where the car is. Moved by CallLift, and by the rules that park the elevator on Level 1. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lifts")
	ELiftStop Stop = ELiftStop::ELS_Bottom;

	/**
	 * Stopped by the handheld. The car stays where it is and answers no call for the rest of the
	 * attempt, which is what makes its shaft climbable. Only a reset puts it back.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lifts")
	bool bHacked = false;

	/** Which lift it is, for the rules that only the elevator has. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lifts")
	ELiftKind Kind = ELiftKind::CargoLift;
};

/**
 * What a door does when its circuit dies or lockdown is called. Everything else about a door, its
 * lock level and which circuit it is on, works the same way for every kind.
 */
UENUM(BlueprintType)
enum class EDoorKind : uint8
{
	/**
	 * Any other door, the lockers included. Its lock is the only thing that holds it. On a circuit it
	 * moves only while that circuit is live; on no circuit it is mechanical and moves whenever unlocked.
	 */
	Standard	UMETA(DisplayName = "Standard"),

	/**
	 * One of the two lockdown doors, Lab to Corridor and Corridor to Security. Seals when its circuit
	 * is cut or lockdown is called, falls open when its circuit is shorted.
	 */
	Lockdown	UMETA(DisplayName = "Lockdown door"),

	/**
	 * The main gate. Moves only while its circuit is live, like a standard door, and stays sealed
	 * under lockdown whatever card is swiped. It never falls open. The rules find the gate by this
	 * kind, so it needs no particular id.
	 */
	Gate		UMETA(DisplayName = "Main gate"),
};

/**
 * Why a door will not move right now. It is what a door's prompt shows, and what the route solver
 * reads to tell a door that wants a card from one that wants power.
 */
UENUM(BlueprintType)
enum class EDoorObstacle : uint8
{
	/** Nothing. The door opens if it is closed and closes if it is open. */
	None		UMETA(DisplayName = "None"),
	/** Open and cannot be closed: pried open with the pry bar. Nothing shuts it again, not the player, a cut circuit or lockdown. */
	Forced		UMETA(DisplayName = "Pried open"),
	/** Closed and held: a lockdown door whose circuit is cut, or a lockdown door or the gate while lockdown is called. */
	Sealed		UMETA(DisplayName = "Sealed"),
	/** Open and cannot be closed: a lockdown door whose circuit is shorted. */
	FailedOpen	UMETA(DisplayName = "Failed open"),
	/** On a circuit that is not live. */
	NoPower		UMETA(DisplayName = "No power"),
	/** Closed, and the player carries neither a keycard of its level nor its key. */
	Locked		UMETA(DisplayName = "Locked"),
};

/**
 * Everything the player can pick up and carry, as the GDD lists it. An item is its type and nothing
 * more: the level 2 card the Corridor guard drops and one placed for testing are the same item, and
 * the player never carries two of a type. The handheld is not here; it is something the player
 * always has, with a hack level, not something carried.
 */
UENUM(BlueprintType)
enum class EFacilityItem : uint8
{
	None				UMETA(DisplayName = "None"),
	/** Plant tool rack. Forces doors, turns the coolant valve and the dock winch, climbs dead shafts, moves the crate, and is a melee weapon. */
	PryBar				UMETA(DisplayName = "Pry bar"),
	/** In the Lab server rack. Pulling it blinds the camera and raises the handheld to hack level 2. */
	ServerDrive			UMETA(DisplayName = "Server drive"),
	/** Carried by the Corridor guard. Opens level 2 locks. */
	Keycard2			UMETA(DisplayName = "Level 2 keycard"),
	/** Carried by the Storage guard. */
	ServiceKey			UMETA(DisplayName = "Service key"),
	/** Storage, in the open. A melee weapon. */
	ImprovisedWeapon	UMETA(DisplayName = "Improvised weapon"),
	/** Security weapons locker. The only ranged weapon in the level. */
	Firearm				UMETA(DisplayName = "Firearm"),
	/** Carried by the neutral character. Opens level 3 locks, which is the gate. */
	Keycard3			UMETA(DisplayName = "Level 3 keycard"),
	/** Storage, inside the level 2 locker. The neutral character wants it; what it is does not matter to the rules. */
	LockerItem			UMETA(DisplayName = "Locker item"),
};

/**
 * One place an item lies in the world, by pickup id. Item is configuration, seeded by the pickup
 * actor when it registers; bTaken is the one thing that changes. A taken pickup's actor hides
 * itself, and a reset puts every pickup back.
 */
USTRUCT(BlueprintType)
struct SCIFISIMESCAPE_API FPickupState
{
	GENERATED_BODY()

	/** What lies here. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Items")
	EFacilityItem Item = EFacilityItem::None;

	/** Whether the player has taken it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Items")
	bool bTaken = false;
};

/**
 * One door in the facility state. bOpen and bForced are the parts that change. The rest is
 * configuration, seeded by the door's actor when it registers and never written after. It lives here
 * rather than on the actor because the rules have to answer for a door without one: the route solver
 * only has the state.
 */
USTRUCT(BlueprintType)
struct SCIFISIMESCAPE_API FDoorState
{
	GENERATED_BODY()

	/** Open or closed. Moved by hand, by a seal or a short on its circuit, and by the pry bar. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Doors")
	bool bOpen = false;

	/**
	 * Pried open with the pry bar. A forced door is open for the rest of the attempt: nothing closes it
	 * again, not the player, a cut circuit or lockdown, and its lock no longer matters. Only a reset
	 * puts it back.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Doors")
	bool bForced = false;

	/**
	 * The flood ate through the lock. The door takes no card and no key any more, for the rest of the
	 * attempt; everything else about it, a seal or a dead circuit, still holds. Only the service tunnel
	 * lock corrodes, since it is the one lock standing in the water. Only a reset puts it back.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Doors")
	bool bLockCorroded = false;

	/** Keycard level that opens it. A card of this level or higher does; 0 is no card lock. Set from the facility settings by id. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Doors")
	int32 LockLevel = 0;

	/**
	 * Item that opens the lock, like the service key on the tunnel door. None when no item does. A door
	 * with a card level as well opens to either.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Doors")
	EFacilityItem KeyItem = EFacilityItem::None;

	/** What it does when its circuit dies or lockdown is called. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Doors")
	EDoorKind Kind = EDoorKind::Standard;
};

/**
 * The three ways out of the facility. Passing through any of them is the win. Each opens on its
 * own terms: the gate on DOORS and a level 3 card, the console or a hack; the dock shutter on PLANT
 * power or the hand winch, once the crate is moved; the tunnel on the service key, the pry bar or
 * the flood corroding its lock, and only while the fans are off.
 */
UENUM(BlueprintType)
enum class EFacilityExit : uint8
{
	None			UMETA(DisplayName = "None"),
	/** The main gate in Security. */
	MainGate		UMETA(DisplayName = "Main gate"),
	/** The loading dock shutter in Storage. */
	LoadingDock		UMETA(DisplayName = "Loading dock"),
	/** The service tunnel in Plant. */
	ServiceTunnel	UMETA(DisplayName = "Service tunnel"),
};

/**
 * What guards one way out. Configuration, seeded by the exit's actor when it registers and never
 * written after, so the rules can tell whether the exit lets the player through without the actor.
 * The gate needs none of it: it is the door of kind Gate.
 */
USTRUCT(BlueprintType)
struct SCIFISIMESCAPE_API FExitState
{
	GENERATED_BODY()

	/** Door the player leaves through, like the tunnel door. None when the exit has no door of its own. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Exits")
	FName DoorId;

	/** Fans that make the way out lethal while they turn, like the vent fans in the service tunnel. None when no fans do. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Exits")
	FName FansId;
};

/**
 * One camera in the facility state. bLooped is the part that changes. LightsId is configuration,
 * seeded by the camera's actor when it registers: the lights zone the camera sees by, since a camera
 * in the dark sees nothing. Whether the camera is watching right now is FFacilityRules::IsCameraWatching.
 */
USTRUCT(BlueprintType)
struct SCIFISIMESCAPE_API FCameraState
{
	GENERATED_BODY()

	/** Looped by the handheld. A looped camera sees nothing for the rest of the attempt. Only a reset puts it back. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cameras")
	bool bLooped = false;

	/** Lights zone the camera sees by, like Level1_Light for the Corridor camera. None leaves it blind. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cameras")
	FName LightsId;
};

/**
 * Water on the Plant floor. bFlooded and Steps are the parts that change. FansId is configuration,
 * seeded by the flood's actor when it registers: the vent fans that drain the water when a step finds
 * them running. It is one flood however the water got there, the coolant valve, the routing panel's
 * pump or the server rack's sprinklers: standing, it is lethal while any circuit carries power, shorts
 * every live circuit once it has stood Config.FloodShortSteps, and eats through the service tunnel
 * lock once it has stood Config.CorrosionSteps.
 */
USTRUCT(BlueprintType)
struct SCIFISIMESCAPE_API FFloodState
{
	GENERATED_BODY()

	/** Water standing on the floor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flood")
	bool bFlooded = false;

	/** Steps the water has stood since it arrived, counted by AdvanceStep. Zero while the floor is dry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flood")
	int32 Steps = 0;

	/** Device id of the fans that drain it, the vent fans. None leaves the water standing until a reset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flood")
	FName FansId;
};

/**
 * Where the dock crate is. It starts wedged in the loading dock shutter and has to be moved before the
 * shutter can open; moved, it sits at the head of the cargo lift shaft, and from there it goes down
 * to Plant, quietly on the car or dropped down the open shaft onto whatever stands below.
 */
UENUM(BlueprintType)
enum class ECratePosition : uint8
{
	/** Wedged in the dock shutter, which cannot open around it. Where the attempt starts. */
	JammingShutter	UMETA(DisplayName = "Jamming the shutter"),
	/** Moved off the shutter to the head of the cargo lift shaft: on the car while the car is up, at the edge of the open shaft while it is down. */
	AtShaftHead		UMETA(DisplayName = "At the shaft head"),
	/** At the bottom of the shaft, in Plant, out of everyone's way for good. */
	InPlant			UMETA(DisplayName = "In Plant"),
};

/**
 * One crate in the facility state. Position and bDropped are the parts that change. LiftId is
 * configuration, seeded by the crate's actor when it registers: the cargo lift whose shaft it goes
 * down, which is also the lift that moves it when the pry bar is not at hand.
 */
USTRUCT(BlueprintType)
struct SCIFISIMESCAPE_API FCrateState
{
	GENERATED_BODY()

	/** Where the crate is. Moved by MoveCrate, DropCrate and the cargo lift riding down with it aboard. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crates")
	ECratePosition Position = ECratePosition::JammingShutter;

	/** Fell down the shaft rather than riding the car. Whatever stood in Plant below it is dead, and it was loud. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crates")
	bool bDropped = false;

	/** Device id of the cargo lift whose shaft the crate goes down. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crates")
	FName LiftId;
};

/**
 * What one switch on the circuit-routing panel does. The panel in the Lab has a switch per job, as
 * the GDD lists them: put the cargo lift on SECURITY so it runs without the fans, put the fans on
 * SECURITY so PLANT can be live without them, or run the coolant pump remotely.
 */
UENUM(BlueprintType)
enum class ERoutingAction : uint8
{
	/** Moves a device onto another circuit, and back again. */
	RouteDevice		UMETA(DisplayName = "Route a device"),
	/** Runs the coolant pump remotely, which floods Plant without touching the valve. */
	CoolantPump		UMETA(DisplayName = "Run the coolant pump"),
};

/**
 * One job the routing panel offers, as a switch registers it: which device it moves and onto which
 * circuit, or that it runs the pump. Configuration, seeded by each AFacilityRoutingSwitch when it
 * begins play and never written after, so the rules and the route solver know what the panel can do
 * without the actors.
 */
USTRUCT(BlueprintType)
struct SCIFISIMESCAPE_API FRoutingOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Routing")
	ERoutingAction Action = ERoutingAction::RouteDevice;

	/** Device id of what the switch moves, like CargoLift or Fans. Nothing to the pump. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Routing", meta = (EditCondition = "Action == ERoutingAction::RouteDevice", EditConditionHides))
	FName DeviceId;

	/** Circuit the switch moves it onto. Nothing to the pump. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Routing", meta = (EditCondition = "Action == ERoutingAction::RouteDevice", EditConditionHides))
	EFacilityCircuit Circuit = EFacilityCircuit::EFC_Security;

	bool operator==(const FRoutingOption& Other) const
	{
		return Action == Other.Action && DeviceId == Other.DeviceId && Circuit == Other.Circuit;
	}
};

/**
 * The circuit-routing panel in the Lab. PatchedDevice and HomeCircuit are the parts that change; the
 * rest is configuration, seeded by the panel's switches when they register. The panel carries one
 * patch at a time: the one device it has moved off the circuit it was placed on, and where that
 * device goes back to. Routing a second device sends the first home. What the devices draw from
 * meanwhile is FFacilityState::DeviceCircuits, which the panel writes through the rules.
 */
USTRUCT(BlueprintType)
struct SCIFISIMESCAPE_API FRoutingState
{
	GENERATED_BODY()

	/** Device id the panel's switches share: what has the hack level and the power. None until a switch registers. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Routing")
	FName PanelId;

	/** The jobs the panel offers, one per switch. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Routing")
	TArray<FRoutingOption> Options;

	/** The one device the panel has moved off its own circuit, None while every device is home. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Routing")
	FName PatchedDevice;

	/** Circuit the patched device was placed on and goes back to. None while nothing is patched. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Routing")
	EFacilityCircuit HomeCircuit = EFacilityCircuit::EFC_None;
};

/**
 * How alarmed the facility is. The order is the escalation order: noise, a sighting, a failed hack
 * and loud sabotage each move it up one, and it comes down one at a time when things stay quiet or
 * the console silences it. Guard-side state: it exists and moves whether or not SECURITY is live,
 * since that circuit powers the camera and the console, not the alarm itself.
 */
UENUM(BlueprintType)
enum class EAlarmLevel : uint8
{
	/** Not a level. The state never holds it, and setting it is refused. */
	EAL_None		UMETA(DisplayName = "None"),
	/** Nothing has noticed the player. Where every attempt starts. */
	EAL_Quiet		UMETA(DisplayName = "Quiet"),
	/** Something has. The guards search, and opening the gate is loud. */
	EAL_Alerted		UMETA(DisplayName = "Alerted"),
	/** The two lockdown doors and the main gate are sealed, whatever card is swiped, until the alarm comes back down. */
	EAL_Lockdown	UMETA(DisplayName = "Lockdown"),
};

/**
 * The tunable values the rules read. The GDD wants them in text the repository can review, so they
 * come from the game config through UFacilityStateSubsystem and sit in the state from then on: the
 * route graph is only well-defined once they are fixed, and a state carries the values it was built
 * under. Nothing writes them after an attempt starts.
 */
USTRUCT(BlueprintType)
struct SCIFISIMESCAPE_API FFacilityConfig
{
	GENERATED_BODY()

	/** Hack level of the handheld the player starts with. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hacking")
	int32 HandheldLevel = 1;

	/** Hack level the server drive raises the handheld to. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hacking")
	int32 ServerDriveHandheldLevel = 2;

	/** Quiet steps in a row before the alarm comes down one level. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Alarm")
	int32 AlarmDecaySteps = 6;

	/** Steps the backup generator runs for once started, carrying every cut circuit meanwhile. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	int32 GeneratorRunSteps = 10;

	/** Steps the flood has to stand before it shorts every circuit carrying power. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flood")
	int32 FloodShortSteps = 1;

	/** Steps the flood has to stand before it eats through the service tunnel lock. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flood")
	int32 CorrosionSteps = 4;
};

/**
 * The facility's world state. Owned by UFacilityStateSubsystem, which is the only thing that
 * writes it in a running game. Actors keep an FName id into this and ask predicates; they never
 * hold their own copy of an answer.
 *
 * Plain data on purpose. The route solver copies it, mutates the copy through FFacilityRules and
 * asks the same predicates, so nothing in here may depend on an actor or a world.
 */
USTRUCT(BlueprintType)
struct SCIFISIMESCAPE_API FFacilityState
{
	GENERATED_BODY()

	/** The values the rules are tuned with. Fixed for the attempt. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Config")
	FFacilityConfig Config;

	/**
	 * Breaker state of each circuit: what the panel says. Whether a circuit carries power is
	 * FFacilityRules::IsCircuitLive, which also asks the generator. A circuit missing from here reads as Cut.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	TMap<EFacilityCircuit, ECircuitState> Circuits;

	/**
	 * Which circuit each device draws from, by device id. Seeded by every AFacilityDeviceBase when
	 * it begins play, moved around by the circuit-routing panel. A device missing from here is on
	 * no circuit.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	TMap<FName, EFacilityCircuit> DeviceCircuits;

	/**
	 * The circuit-routing panel: the jobs its switches offer, and the one device it has moved off the
	 * circuit it was placed on. Seeded by the panel's switches when they begin play, moved by
	 * RouteDevice. What the moved device draws from is in DeviceCircuits like every other device.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	FRoutingState Routing;

	/**
	 * Steps the backup generator has left to run, 0 while it is off. While it runs every cut circuit
	 * carries power whatever its breaker says. Set by StartGenerator, counted down by AdvanceStep.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	int32 GeneratorStepsLeft = 0;

	/**
	 * Rigged to overload with the pry bar. The generator is finished for the attempt, and every breaker
	 * tripped when it went. The guards' answer to it is theirs. Only a reset puts it back.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	bool bGeneratorOverloaded = false;

	/**
	 * Handheld level each hackable device needs, by device id. Seeded by every AFacilityDeviceBase
	 * placed with a hack level when it begins play: the two lockdown doors and the elevator at 1, the
	 * camera at 1, the gate controller at 2. A device missing from here is nothing the handheld works
	 * on. What a hack does is the device's own rule.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hacking")
	TMap<FName, int32> HackLevels;

	/**
	 * Every lift by device id: where its car is, whether the handheld stopped it, and what kind it is.
	 * Seeded by every AFacilityLift when it begins play. Moved by CallLift and HackLift, and by the
	 * circuits and the alarm: the elevator parks on Level 1 while its circuit is dead or lockdown is
	 * called. A lift missing from here reads as at Bottom, unpowered and nothing the handheld works on.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lifts")
	TMap<FName, FLiftState> Lifts;

	/**
	 * Every door by door id, the hatch included; a door that is a device is keyed by its device id.
	 * Seeded by each door actor when it begins play with its configuration and starting position.
	 * Moved by ToggleDoor, ForceDoor and HackDoor, and by the circuits: a lockdown door follows its
	 * circuit whenever that changes, unless the pry bar forced it. A door missing from here is closed,
	 * unlocked and mechanical.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Doors")
	TMap<FName, FDoorState> Doors;

	/**
	 * Every camera by device id: whether the handheld looped it and the lights it sees by. Seeded by
	 * each AFacilityCamera when it begins play. A camera missing from here sees nothing.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cameras")
	TMap<FName, FCameraState> Cameras;

	/**
	 * Water on the Plant floor, and the fans that drain it. Seeded with the fans by the flood's actor
	 * when it begins play. Put there by FloodPlant, from the coolant valve, the routing panel's pump
	 * or the server rack's sprinklers, and counted, drained and made to short things by AdvanceStep.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flood")
	FFloodState Flood;

	/**
	 * The coolant valve in Plant is open. Opening it takes the pry bar, since it is seized, and floods
	 * the floor; while it stays open the fans cannot drain the water. Closed by hand.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flood")
	bool bCoolantValveOpen = false;

	/**
	 * The server rack in the Lab was rigged to overload with the pry bar and burns. The Lab is full of
	 * smoke guards will not enter for the rest of the attempt, the alarm went to Alerted as it caught,
	 * and the sprinklers flooded Plant. Only a reset puts it out.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sabotage")
	bool bServerRackOverloaded = false;

	/**
	 * Every crate by crate id: where it is, whether it was dropped, and the lift whose shaft it goes
	 * down. Seeded by each AFacilityCrate when it begins play. A crate missing from here jams nothing.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crates")
	TMap<FName, FCrateState> Crates;

	/**
	 * What the player carries. Empty when an attempt starts. Taking a pickup adds to it, and the
	 * neutral character can take a card or the locker item back out of it. Keycard levels come from
	 * the cards in here; a level 3 card opens level 2 locks too.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Items")
	TSet<EFacilityItem> Inventory;

	/**
	 * Every pickup by pickup id: what lies there and whether it has been taken. Seeded by each
	 * pickup actor when it begins play. A taken pickup stays taken until a reset, even if the item
	 * leaves the player's hands again, so nothing ever lies in two places. A pickup missing from
	 * here holds nothing.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Items")
	TMap<FName, FPickupState> Pickups;

	/**
	 * What guards each way out, by exit. Seeded by each exit's actor when it begins play. An exit
	 * missing from here has no door or fans of its own, which keeps the tunnel shut.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Exits")
	TMap<EFacilityExit, FExitState> Exits;

	/**
	 * The exit the player passed through, None while they are still inside. This is the win, and it
	 * happens exactly once: Escape refuses once it is set, and only a reset clears it.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Exits")
	EFacilityExit EscapedThrough = EFacilityExit::None;

	/**
	 * How alarmed the facility is. Quiet when an attempt starts, raised one level at a time by
	 * noise, a sighting, a failed hack or loud sabotage, lowered one at a time by quiet steps or the
	 * console. At Lockdown the two lockdown doors and the gate are sealed and the elevator parks on
	 * Level 1; see FFacilityRules::IsDoorSealed and FFacilityRules::GetLiftObstacle.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Alarm")
	EAlarmLevel AlarmLevel = EAlarmLevel::EAL_Quiet;

	/**
	 * Steps in a row in which nothing raised the alarm, counted only while it is above Quiet. When it
	 * reaches Config.AlarmDecaySteps the alarm comes down one level and the count starts again. Any
	 * noise puts it back to zero, at Lockdown too, where the level itself cannot rise. Bookkeeping for
	 * the rules: it never counts as a change on its own, so nothing broadcasts for it.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Alarm")
	int32 QuietSteps = 0;
};

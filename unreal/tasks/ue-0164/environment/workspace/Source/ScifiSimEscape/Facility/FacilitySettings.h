#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Facility/FacilityTypes.h"
#include "FacilitySettings.generated.h"

/**
 * What one door's lock takes, by door id. A door with no entry has no lock: it opens to anyone,
 * which is right for the hatch and wrong for a locker, so the facility warns about a locked-looking
 * door with no entry and about an entry no door claims.
 */
USTRUCT()
struct SCIFISIMESCAPE_API FDoorLockConfig
{
	GENERATED_BODY()

	/** DeviceId of the door, or DoorId of the hatch. */
	UPROPERTY(EditAnywhere, Category = "Door")
	FName Id;

	/** Keycard level that opens it, 0 for no card lock. A card of this level or higher does. */
	UPROPERTY(EditAnywhere, Category = "Door", meta = (ClampMin = "0"))
	int32 LockLevel = 0;

	/** Item that opens it, like the service key on the tunnel door. None when no item does. With a LockLevel as well, either opens it. */
	UPROPERTY(EditAnywhere, Category = "Door")
	EFacilityItem KeyItem = EFacilityItem::None;
};

/** What lies at one pickup, by pickup id. The GDD's item list and locations: the id says where, the item says what. */
USTRUCT()
struct SCIFISIMESCAPE_API FPickupConfig
{
	GENERATED_BODY()

	/** PickupId of the placed pickup. */
	UPROPERTY(EditAnywhere, Category = "Pickup")
	FName Id;

	/** What lies there. */
	UPROPERTY(EditAnywhere, Category = "Pickup")
	EFacilityItem Item = EFacilityItem::None;
};

/** Handheld level one device needs, by device id. A device with no entry is nothing the handheld works on. */
USTRUCT()
struct SCIFISIMESCAPE_API FHackTargetConfig
{
	GENERATED_BODY()

	/** DeviceId of the door, lift, camera, controller, console or rack. */
	UPROPERTY(EditAnywhere, Category = "Hacking")
	FName Id;

	/** Level the handheld needs to reach. What a hack does is the device's own rule. */
	UPROPERTY(EditAnywhere, Category = "Hacking", meta = (ClampMin = "1"))
	int32 Level = 1;
};

/**
 * The facility's tuning, as text the repository can review: DefaultGame.ini, section
 * [/Script/ScifiSimEscape.FacilitySettings], also shown under Project Settings > Game > Facility.
 *
 * The GDD wants every value the route graph depends on in text-backed configuration rather than on
 * placed actors, so that a designer reads the lock levels, the item list, the hack levels and the
 * step counts in one place and changes them in source control. What stays on an actor is placement:
 * its id, the circuit it is wired to, what kind of door it is, where a lift's car starts. What the
 * rules are tuned with lives here, keyed by those ids.
 *
 * UFacilityStateSubsystem reads this once when a level starts: the scalars go into the state as
 * FFacilityConfig, and the per-id tables are looked up as each actor registers its id. Nothing reads
 * it after that, so an attempt and every state the route solver copies are built under one set of numbers.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Facility"))
class SCIFISIMESCAPE_API UFacilitySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** The scalars the rules read, as the state carries them. */
	FFacilityConfig MakeFacilityConfig() const;

	/** The lock for a door id, null when the door has none. */
	const FDoorLockConfig* FindDoorLock(FName DoorId) const;

	/** What lies at a pickup id, None when nothing is configured there. */
	EFacilityItem FindPickupItem(FName PickupId) const;

	/** Handheld level a device id needs, 0 when the handheld has nothing to work on there. */
	int32 FindHackLevel(FName DeviceId) const;

	//
	// The clock
	//

	/**
	 * Seconds of play per step. A step is the unit the GDD counts in: alarm decay, the generator, the
	 * flood. Zero stops the clock, for stepping by hand with Facility.AdvanceStep.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Clock", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float StepSeconds = 5.f;

	//
	// Alarm
	//

	/** Quiet steps in a row before the alarm comes down one level. */
	UPROPERTY(Config, EditAnywhere, Category = "Alarm", meta = (ClampMin = "1"))
	int32 AlarmDecaySteps = 6;

	//
	// Hacking
	//

	/** Hack level of the handheld the player starts with. */
	UPROPERTY(Config, EditAnywhere, Category = "Hacking", meta = (ClampMin = "0"))
	int32 HandheldLevel = 1;

	/** Hack level the server drive raises the handheld to. */
	UPROPERTY(Config, EditAnywhere, Category = "Hacking", meta = (ClampMin = "0"))
	int32 ServerDriveHandheldLevel = 2;

	/**
	 * Handheld level each hackable device needs, by device id. The GDD puts the two lockdown doors,
	 * the elevator, the camera and the routing panel at 1, and the gate controller, the alarm console
	 * and the server rack at 2. A device not listed is nothing the handheld works on.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Hacking", meta = (TitleProperty = "Id"))
	TArray<FHackTargetConfig> HackTargets;

	//
	// Power
	//

	/** Steps the backup generator runs for once started, carrying every cut circuit meanwhile. */
	UPROPERTY(Config, EditAnywhere, Category = "Power", meta = (ClampMin = "1"))
	int32 GeneratorRunSteps = 10;

	//
	// Flood
	//

	/** Steps the flood has to stand before it shorts every live circuit. The GDD says one. */
	UPROPERTY(Config, EditAnywhere, Category = "Flood", meta = (ClampMin = "1"))
	int32 FloodShortSteps = 1;

	/** Steps the flood has to stand before it corrodes the service tunnel lock through. */
	UPROPERTY(Config, EditAnywhere, Category = "Flood", meta = (ClampMin = "1"))
	int32 CorrosionSteps = 4;

	//
	// Doors
	//

	/**
	 * What each door's lock takes, by door id. The GDD puts the Lab and Corridor-Security doors, the
	 * Storage locker and the weapons locker at level 2, the gate at level 3, and the tunnel door on the
	 * service key. A door not listed has no lock.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Doors", meta = (TitleProperty = "Id"))
	TArray<FDoorLockConfig> DoorLocks;

	//
	// Items
	//

	/** What lies at each pickup, by pickup id. The GDD's item list; the placed pickup is the location. */
	UPROPERTY(Config, EditAnywhere, Category = "Items", meta = (TitleProperty = "Id"))
	TArray<FPickupConfig> Pickups;
};

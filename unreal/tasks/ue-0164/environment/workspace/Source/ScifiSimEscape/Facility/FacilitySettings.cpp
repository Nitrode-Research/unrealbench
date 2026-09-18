#include "Facility/FacilitySettings.h"

FFacilityConfig UFacilitySettings::MakeFacilityConfig() const
{
	FFacilityConfig Config;
	Config.HandheldLevel = HandheldLevel;
	Config.ServerDriveHandheldLevel = ServerDriveHandheldLevel;
	Config.AlarmDecaySteps = FMath::Max(1, AlarmDecaySteps);
	Config.GeneratorRunSteps = FMath::Max(1, GeneratorRunSteps);
	Config.FloodShortSteps = FMath::Max(1, FloodShortSteps);
	Config.CorrosionSteps = FMath::Max(1, CorrosionSteps);
	return Config;
}

const FDoorLockConfig* UFacilitySettings::FindDoorLock(const FName DoorId) const
{
	if (DoorId.IsNone())
	{
		return nullptr;
	}

	return DoorLocks.FindByPredicate([DoorId](const FDoorLockConfig& Entry)
	{
		return Entry.Id == DoorId;
	});
}

EFacilityItem UFacilitySettings::FindPickupItem(const FName PickupId) const
{
	if (PickupId.IsNone())
	{
		return EFacilityItem::None;
	}

	const FPickupConfig* Found = Pickups.FindByPredicate([PickupId](const FPickupConfig& Entry)
	{
		return Entry.Id == PickupId;
	});
	return Found != nullptr ? Found->Item : EFacilityItem::None;
}

int32 UFacilitySettings::FindHackLevel(const FName DeviceId) const
{
	if (DeviceId.IsNone())
	{
		return 0;
	}

	const FHackTargetConfig* Found = HackTargets.FindByPredicate([DeviceId](const FHackTargetConfig& Entry)
	{
		return Entry.Id == DeviceId;
	});
	return Found != nullptr ? FMath::Max(0, Found->Level) : 0;
}

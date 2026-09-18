#include "Facility/FacilityStateSubsystem.h"

#include "Engine/World.h"
#include "Facility/FacilityRules.h"
#include "Facility/FacilitySettings.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "ScifiSimEscape.h"
#include "TimerManager.h"

namespace
{
	/**
	 * Facility.GiveItem <Item>. Puts an item in the player's hands from the console, named as in
	 * EFacilityItem, e.g. Facility.GiveItem PryBar. For trying things without walking to the pickup.
	 */
	void GiveItemFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		UFacilityStateSubsystem* Facility = UWorld::GetSubsystem<UFacilityStateSubsystem>(World);
		const UEnum* ItemEnum = StaticEnum<EFacilityItem>();
		const int64 Value = Args.Num() == 1 ? ItemEnum->GetValueByNameString(Args[0]) : INDEX_NONE;

		if (Facility == nullptr || Value <= static_cast<int64>(EFacilityItem::None))
		{
			// NumEnums counts the hidden _MAX entry at the end.
			FString Names;
			for (int32 Index = 0; Index < ItemEnum->NumEnums() - 1; ++Index)
			{
				if (ItemEnum->GetValueByIndex(Index) == static_cast<int64>(EFacilityItem::None))
				{
					continue;
				}
				if (Names.IsEmpty() == false)
				{
					Names += TEXT(", ");
				}
				Names += ItemEnum->GetNameStringByIndex(Index);
			}

			UE_LOG(LogScifiSimEscape, Warning, TEXT("Usage: Facility.GiveItem <Item>, from a running game. Items: %s."), *Names);
			return;
		}

		Facility->GiveItem(static_cast<EFacilityItem>(Value));
	}

	FAutoConsoleCommandWithWorldAndArgs GGiveItemCommand(
		TEXT("Facility.GiveItem"),
		TEXT("Puts an item in the player's hands, e.g. Facility.GiveItem PryBar or Facility.GiveItem Keycard2."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GiveItemFromConsole));

	/**
	 * Facility.SetAlarmLevel <Level>. Puts the alarm at a level from the console, named as the game
	 * shows it: Quiet, Alerted or Lockdown. For seeing what a level does before anything raises the alarm.
	 */
	void SetAlarmLevelFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		UFacilityStateSubsystem* Facility = UWorld::GetSubsystem<UFacilityStateSubsystem>(World);
		const UEnum* LevelEnum = StaticEnum<EAlarmLevel>();

		// Matched against the display name and the enum name both, so Lockdown and EAL_Lockdown
		// work. NumEnums counts the hidden _MAX entry at the end.
		EAlarmLevel Level = EAlarmLevel::EAL_None;
		FString Names;
		for (int32 Index = 0; Index < LevelEnum->NumEnums() - 1; ++Index)
		{
			const EAlarmLevel Candidate = static_cast<EAlarmLevel>(LevelEnum->GetValueByIndex(Index));
			if (Candidate == EAlarmLevel::EAL_None)
			{
				continue;
			}

			const FString DisplayName = LevelEnum->GetDisplayNameTextByIndex(Index).ToString();
			const bool bMatches = Args.Num() == 1
				&& (Args[0].Equals(DisplayName, ESearchCase::IgnoreCase)
					|| Args[0].Equals(LevelEnum->GetNameStringByIndex(Index), ESearchCase::IgnoreCase));
			if (bMatches)
			{
				Level = Candidate;
			}

			if (Names.IsEmpty() == false)
			{
				Names += TEXT(", ");
			}
			Names += DisplayName;
		}

		if (Facility == nullptr || Level == EAlarmLevel::EAL_None)
		{
			UE_LOG(LogScifiSimEscape, Warning, TEXT("Usage: Facility.SetAlarmLevel <Level>, from a running game. Levels: %s."), *Names);
			return;
		}

		Facility->SetAlarmLevel(Level);
	}

	FAutoConsoleCommandWithWorldAndArgs GSetAlarmLevelCommand(
		TEXT("Facility.SetAlarmLevel"),
		TEXT("Puts the alarm at a level, e.g. Facility.SetAlarmLevel Lockdown or Facility.SetAlarmLevel Quiet."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetAlarmLevelFromConsole));

	/**
	 * Facility.AdvanceStep [Count]. Advances the facility's clock by hand, one step or as many as
	 * given. For watching the alarm come down without waiting, or with StepSeconds at zero.
	 */
	void AdvanceStepFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		UFacilityStateSubsystem* Facility = UWorld::GetSubsystem<UFacilityStateSubsystem>(World);
		const int32 Count = Args.Num() == 1 ? FCString::Atoi(*Args[0]) : 1;

		if (Facility == nullptr || Count < 1)
		{
			UE_LOG(LogScifiSimEscape, Warning, TEXT("Usage: Facility.AdvanceStep [Count], from a running game."));
			return;
		}

		for (int32 Step = 0; Step < Count; ++Step)
		{
			Facility->AdvanceStep();
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GAdvanceStepCommand(
		TEXT("Facility.AdvanceStep"),
		TEXT("Advances the facility's clock one step, or as many as given, e.g. Facility.AdvanceStep 6."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AdvanceStepFromConsole));

	/**
	 * Facility.FloodPlant. Puts water on the Plant floor from the console, the way the routing panel's
	 * pump will. For seeing what the flood does without the pry bar and the valve.
	 */
	void FloodPlantFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		UFacilityStateSubsystem* Facility = UWorld::GetSubsystem<UFacilityStateSubsystem>(World);
		if (Facility == nullptr)
		{
			UE_LOG(LogScifiSimEscape, Warning, TEXT("Usage: Facility.FloodPlant, from a running game."));
			return;
		}

		Facility->FloodPlant();
	}

	FAutoConsoleCommandWithWorldAndArgs GFloodPlantCommand(
		TEXT("Facility.FloodPlant"),
		TEXT("Floods the Plant floor, as the coolant valve or the sprinklers would."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FloodPlantFromConsole));
}

void UFacilityStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FFacilityRules::SetInitialState(InitialState);

	// The tuning the rules read, from DefaultGame.ini through the settings. Fixed from here on: every
	// attempt and every state the route solver copies is built under the same numbers. The per-id
	// tables are read as the actors register.
	const UFacilitySettings* Settings = GetDefault<UFacilitySettings>();
	StepSeconds = Settings->StepSeconds;
	InitialState.Config = Settings->MakeFacilityConfig();

	State = InitialState;

	const FFacilityConfig& Config = InitialState.Config;
	UE_LOG(LogScifiSimEscape, Log,
		TEXT("Facility settings: StepSeconds=%g AlarmDecaySteps=%d HandheldLevel=%d ServerDriveHandheldLevel=%d GeneratorRunSteps=%d FloodShortSteps=%d CorrosionSteps=%d, %d door locks, %d pickups, %d hack targets."),
		StepSeconds, Config.AlarmDecaySteps, Config.HandheldLevel, Config.ServerDriveHandheldLevel, Config.GeneratorRunSteps,
		Config.FloodShortSteps, Config.CorrosionSteps, Settings->DoorLocks.Num(), Settings->Pickups.Num(), Settings->HackTargets.Num());
}

void UFacilityStateSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StepTimerHandle);
	}

	Super::Deinitialize();
}

void UFacilityStateSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// The clock starts with play, not with the subsystem, so no step passes while the level is still loading.
	if (StepSeconds > 0.f)
	{
		InWorld.GetTimerManager().SetTimer(StepTimerHandle, this, &UFacilityStateSubsystem::HandleStepTimer, StepSeconds, true);
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Facility clock is stopped: StepSeconds is zero. Steps only pass through Facility.AdvanceStep."));
	}

	// The placed actors begin play after this, once the game mode starts play, so the settings are
	// checked against the level on the next tick, when every one of them has registered.
	InWorld.GetTimerManager().SetTimerForNextTick(this, &UFacilityStateSubsystem::ReportUnclaimedSettings);
}

void UFacilityStateSubsystem::HandleStepTimer()
{
	AdvanceStep();
}

bool UFacilityStateSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

ECircuitState UFacilityStateSubsystem::GetCircuitState(const EFacilityCircuit Circuit) const
{
	return FFacilityRules::GetCircuitState(State, Circuit);
}

bool UFacilityStateSubsystem::IsCircuitLive(const EFacilityCircuit Circuit) const
{
	return FFacilityRules::IsCircuitLive(State, Circuit);
}

EFacilityCircuit UFacilityStateSubsystem::GetDeviceCircuit(const FName DeviceId) const
{
	return FFacilityRules::GetDeviceCircuit(State, DeviceId);
}

bool UFacilityStateSubsystem::IsDevicePowered(const FName DeviceId) const
{
	return FFacilityRules::IsDevicePowered(State, DeviceId);
}

bool UFacilityStateSubsystem::IsBreakerOn(const EFacilityCircuit Circuit) const
{
	return FFacilityRules::IsBreakerOn(State, Circuit);
}

bool UFacilityStateSubsystem::IsGeneratorRunning() const
{
	return FFacilityRules::IsGeneratorRunning(State);
}

int32 UFacilityStateSubsystem::GetGeneratorStepsLeft() const
{
	return FFacilityRules::GetGeneratorStepsLeft(State);
}

bool UFacilityStateSubsystem::IsGeneratorOverloaded() const
{
	return FFacilityRules::IsGeneratorOverloaded(State);
}

bool UFacilityStateSubsystem::CanStartGenerator() const
{
	return FFacilityRules::CanStartGenerator(State);
}

bool UFacilityStateSubsystem::CanOverloadGenerator() const
{
	return FFacilityRules::CanOverloadGenerator(State);
}

ELiftStop UFacilityStateSubsystem::GetLiftStop(const FName LiftId) const
{
	return FFacilityRules::GetLiftStop(State, LiftId);
}

bool UFacilityStateSubsystem::CanUseLift(const FName LiftId) const
{
	return FFacilityRules::CanUseLift(State, LiftId);
}

ELiftObstacle UFacilityStateSubsystem::GetLiftObstacle(const FName LiftId) const
{
	return FFacilityRules::GetLiftObstacle(State, LiftId);
}

bool UFacilityStateSubsystem::IsLiftHacked(const FName LiftId) const
{
	return FFacilityRules::IsLiftHacked(State, LiftId);
}

bool UFacilityStateSubsystem::CanHackLift(const FName LiftId) const
{
	return FFacilityRules::CanHackLift(State, LiftId);
}

bool UFacilityStateSubsystem::IsDoorOpen(const FName DoorId) const
{
	return FFacilityRules::IsDoorOpen(State, DoorId);
}

EDoorObstacle UFacilityStateSubsystem::GetDoorObstacle(const FName DoorId) const
{
	return FFacilityRules::GetDoorObstacle(State, DoorId);
}

bool UFacilityStateSubsystem::CanOpenDoor(const FName DoorId) const
{
	return FFacilityRules::CanOpenDoor(State, DoorId);
}

bool UFacilityStateSubsystem::CanCloseDoor(const FName DoorId) const
{
	return FFacilityRules::CanCloseDoor(State, DoorId);
}

bool UFacilityStateSubsystem::CanForceDoor(const FName DoorId) const
{
	return FFacilityRules::CanForceDoor(State, DoorId);
}

bool UFacilityStateSubsystem::CanHackDoor(const FName DoorId) const
{
	return FFacilityRules::CanHackDoor(State, DoorId);
}

bool UFacilityStateSubsystem::IsDoorLoudToOpen(const FName DoorId) const
{
	return FFacilityRules::IsDoorLoudToOpen(State, DoorId);
}

bool UFacilityStateSubsystem::HasItem(const EFacilityItem Item) const
{
	return FFacilityRules::HasItem(State, Item);
}

TArray<EFacilityItem> UFacilityStateSubsystem::GetInventory() const
{
	return State.Inventory.Array();
}

int32 UFacilityStateSubsystem::GetKeycardLevel() const
{
	return FFacilityRules::GetKeycardLevel(State);
}

int32 UFacilityStateSubsystem::GetHackLevel(const FName DeviceId) const
{
	return FFacilityRules::GetHackLevel(State, DeviceId);
}

int32 UFacilityStateSubsystem::GetHandheldLevel() const
{
	return FFacilityRules::GetHandheldLevel(State);
}

bool UFacilityStateSubsystem::WouldHackSucceed(const int32 TargetLevel) const
{
	return FFacilityRules::WouldHackSucceed(State, TargetLevel);
}

bool UFacilityStateSubsystem::IsServerDrivePulled() const
{
	return FFacilityRules::IsServerDrivePulled(State);
}

EFacilityItem UFacilityStateSubsystem::GetPickupItem(const FName PickupId) const
{
	return FFacilityRules::GetPickupItem(State, PickupId);
}

bool UFacilityStateSubsystem::IsPickupTaken(const FName PickupId) const
{
	return FFacilityRules::IsPickupTaken(State, PickupId);
}

bool UFacilityStateSubsystem::CanTakePickup(const FName PickupId) const
{
	return FFacilityRules::CanTakePickup(State, PickupId);
}

bool UFacilityStateSubsystem::AreFansRunning(const FName FansId) const
{
	return FFacilityRules::AreFansRunning(State, FansId);
}

bool UFacilityStateSubsystem::AreLightsOn(const FName LightsId) const
{
	return FFacilityRules::AreLightsOn(State, LightsId);
}

bool UFacilityStateSubsystem::IsCameraLooped(const FName CameraId) const
{
	return FFacilityRules::IsCameraLooped(State, CameraId);
}

bool UFacilityStateSubsystem::IsCameraWatching(const FName CameraId) const
{
	return FFacilityRules::IsCameraWatching(State, CameraId);
}

bool UFacilityStateSubsystem::CanHackCamera(const FName CameraId) const
{
	return FFacilityRules::CanHackCamera(State, CameraId);
}

bool UFacilityStateSubsystem::CanHackGateController(const FName ControllerId) const
{
	return FFacilityRules::CanHackGateController(State, ControllerId);
}

bool UFacilityStateSubsystem::CanUseRoutingPanel(const FName PanelId) const
{
	return FFacilityRules::CanUseRoutingPanel(State, PanelId);
}

FName UFacilityStateSubsystem::GetPatchedDevice() const
{
	return FFacilityRules::GetPatchedDevice(State);
}

bool UFacilityStateSubsystem::IsDevicePatched(const FName DeviceId) const
{
	return FFacilityRules::IsDevicePatched(State, DeviceId);
}

EFacilityCircuit UFacilityStateSubsystem::GetHomeCircuit(const FName DeviceId) const
{
	return FFacilityRules::GetHomeCircuit(State, DeviceId);
}

bool UFacilityStateSubsystem::CanRouteDevice(const FName PanelId, const FName DeviceId, const EFacilityCircuit Circuit) const
{
	return FFacilityRules::CanRouteDevice(State, PanelId, DeviceId, Circuit);
}

bool UFacilityStateSubsystem::CanRunCoolantPump(const FName PanelId) const
{
	return FFacilityRules::CanRunCoolantPump(State, PanelId);
}

bool UFacilityStateSubsystem::IsDoorLockCorroded(const FName DoorId) const
{
	return FFacilityRules::IsDoorLockCorroded(State, DoorId);
}

bool UFacilityStateSubsystem::IsPlantFlooded() const
{
	return FFacilityRules::IsPlantFlooded(State);
}

int32 UFacilityStateSubsystem::GetFloodSteps() const
{
	return FFacilityRules::GetFloodSteps(State);
}

bool UFacilityStateSubsystem::IsFloodLethal() const
{
	return FFacilityRules::IsFloodLethal(State);
}

bool UFacilityStateSubsystem::WouldFansDrainFlood() const
{
	return FFacilityRules::WouldFansDrainFlood(State);
}

bool UFacilityStateSubsystem::IsCoolantValveOpen() const
{
	return FFacilityRules::IsCoolantValveOpen(State);
}

bool UFacilityStateSubsystem::CanOpenCoolantValve() const
{
	return FFacilityRules::CanOpenCoolantValve(State);
}

bool UFacilityStateSubsystem::CanCloseCoolantValve() const
{
	return FFacilityRules::CanCloseCoolantValve(State);
}

bool UFacilityStateSubsystem::IsServerRackOverloaded() const
{
	return FFacilityRules::IsServerRackOverloaded(State);
}

bool UFacilityStateSubsystem::IsLabSmokeFilled() const
{
	return FFacilityRules::IsLabSmokeFilled(State);
}

bool UFacilityStateSubsystem::CanOverloadServerRack(const FName RackId) const
{
	return FFacilityRules::CanOverloadServerRack(State, RackId);
}

ECratePosition UFacilityStateSubsystem::GetCratePosition(const FName CrateId) const
{
	return FFacilityRules::GetCratePosition(State, CrateId);
}

bool UFacilityStateSubsystem::IsCrateJammingShutter(const FName CrateId) const
{
	return FFacilityRules::IsCrateJammingShutter(State, CrateId);
}

bool UFacilityStateSubsystem::IsDockShutterJammed() const
{
	return FFacilityRules::IsDockShutterJammed(State);
}

bool UFacilityStateSubsystem::WasCrateDropped(const FName CrateId) const
{
	return FFacilityRules::WasCrateDropped(State, CrateId);
}

bool UFacilityStateSubsystem::CanMoveCrate(const FName CrateId) const
{
	return FFacilityRules::CanMoveCrate(State, CrateId);
}

bool UFacilityStateSubsystem::CanDropCrate(const FName CrateId) const
{
	return FFacilityRules::CanDropCrate(State, CrateId);
}

bool UFacilityStateSubsystem::HasEscaped() const
{
	return FFacilityRules::HasEscaped(State);
}

EFacilityExit UFacilityStateSubsystem::GetEscapeExit() const
{
	return FFacilityRules::GetEscapeExit(State);
}

bool UFacilityStateSubsystem::CanEscape(const EFacilityExit Exit) const
{
	return FFacilityRules::CanEscape(State, Exit);
}

EAlarmLevel UFacilityStateSubsystem::GetAlarmLevel() const
{
	return FFacilityRules::GetAlarmLevel(State);
}

bool UFacilityStateSubsystem::IsLockdown() const
{
	return FFacilityRules::IsLockdown(State);
}

bool UFacilityStateSubsystem::CanSilenceAlarm(const FName ConsoleId) const
{
	return FFacilityRules::CanSilenceAlarm(State, ConsoleId);
}

AActor* UFacilityStateSubsystem::FindDeviceActor(const FName DeviceId) const
{
	const TArray<FRegisteredDevice>* Registered = DeviceActors.Find(DeviceId);
	if (Registered == nullptr)
	{
		return nullptr;
	}

	for (const FRegisteredDevice& Entry : *Registered)
	{
		if (AActor* Actor = Entry.Actor.Get())
		{
			return Actor;
		}
	}
	return nullptr;
}

void UFacilityStateSubsystem::RegisterDevice(AActor* DeviceActor, const FName DeviceId, const EFacilityCircuit DefaultCircuit, const bool bSharesId)
{
	if (DeviceActor == nullptr || DeviceId.IsNone())
	{
		return;
	}

	TArray<FRegisteredDevice>& Registered = DeviceActors.FindOrAdd(DeviceId);

	// An actor that went away without unregistering holds the id no longer.
	Registered.RemoveAll([](const FRegisteredDevice& Entry)
	{
		return Entry.Actor.IsValid() == false;
	});

	// Several actors hold one id only while every one of them shares it. Any other pair is two devices
	// that now share one state without meaning to, so the first such actor is named.
	FRegisteredDevice* OwnEntry = nullptr;
	const AActor* Conflicting = nullptr;
	for (FRegisteredDevice& Entry : Registered)
	{
		const AActor* Other = Entry.Actor.Get();
		if (Other == DeviceActor)
		{
			OwnEntry = &Entry;
		}
		else if (Conflicting == nullptr && (bSharesId == false || Entry.bSharesId == false))
		{
			Conflicting = Other;
		}
	}

	if (Conflicting != nullptr)
	{
		UE_LOG(LogScifiSimEscape, Error,
			TEXT("Device id '%s' is claimed by both %s and %s, and at least one of them does not share its id. An id that is not shared must be unique, or the two share one state."),
			*DeviceId.ToString(), *Conflicting->GetName(), *DeviceActor->GetName());
	}

	// An actor that registers again keeps its one entry.
	if (OwnEntry != nullptr)
	{
		OwnEntry->bSharesId = bSharesId;
	}
	else
	{
		Registered.Add(FRegisteredDevice{ DeviceActor, bSharesId });
	}

	// Actors sharing an id are one device with one routing entry. One placed on a different circuit
	// than the id was first placed on is routed with the rest all the same, which is rarely what was meant.
	if (bSharesId)
	{
		const EFacilityCircuit* PlacedCircuit = InitialState.DeviceCircuits.Find(DeviceId);
		if (PlacedCircuit != nullptr && *PlacedCircuit != DefaultCircuit)
		{
			UE_LOG(LogScifiSimEscape, Warning,
				TEXT("%s is placed on %s under device id '%s', which was first placed on %s. The id keeps the circuit it was first placed on, so %s draws from %s with the rest."),
				*DeviceActor->GetName(), *UEnum::GetDisplayValueAsText(DefaultCircuit).ToString(), *DeviceId.ToString(),
				*UEnum::GetDisplayValueAsText(*PlacedCircuit).ToString(), *DeviceActor->GetName(),
				*UEnum::GetDisplayValueAsText(*PlacedCircuit).ToString());
		}
	}

	// The circuit the device was placed on is where it starts every attempt. The live state only
	// takes it if nothing has routed the device yet.
	if (InitialState.DeviceCircuits.Contains(DeviceId) == false)
	{
		FFacilityRules::SetDeviceCircuit(InitialState, DeviceId, DefaultCircuit);
	}
	if (State.DeviceCircuits.Contains(DeviceId) == false)
	{
		FFacilityRules::SetDeviceCircuit(State, DeviceId, DefaultCircuit);
	}

	// The handheld level the device needs is the settings' by id. AddHackTarget seeds nothing for a
	// level of 0, which is no target, and leaves a known device alone.
	const int32 HackLevel = GetDefault<UFacilitySettings>()->FindHackLevel(DeviceId);
	FFacilityRules::AddHackTarget(InitialState, DeviceId, HackLevel);
	FFacilityRules::AddHackTarget(State, DeviceId, HackLevel);
}

void UFacilityStateSubsystem::UnregisterDevice(AActor* DeviceActor, const FName DeviceId)
{
	TArray<FRegisteredDevice>* Registered = DeviceActors.Find(DeviceId);
	if (Registered == nullptr)
	{
		return;
	}

	// Only this actor's entry. Others sharing the id stay registered.
	const TWeakObjectPtr<AActor> Leaving(DeviceActor);
	Registered->RemoveAll([&Leaving](const FRegisteredDevice& Entry)
	{
		return Entry.Actor.HasSameIndexAndSerialNumber(Leaving);
	});

	if (Registered->IsEmpty())
	{
		DeviceActors.Remove(DeviceId);
	}
}

void UFacilityStateSubsystem::RegisterLift(const FName LiftId, const FLiftState& Lift)
{
	// How the lift was placed is where it starts every attempt. AddLift leaves a known lift alone, so
	// the live state keeps a lift the player has called or stopped.
	FFacilityRules::AddLift(InitialState, LiftId, Lift);
	FFacilityRules::AddLift(State, LiftId, Lift);
}

void UFacilityStateSubsystem::RegisterDoor(const FName DoorId, const EDoorKind Kind, const bool bInitiallyOpen)
{
	if (DoorId.IsNone())
	{
		return;
	}

	// How the door was placed is where it starts every attempt, and what its lock takes is the settings'
	// by id. A door the settings do not name has no lock, which is right for the hatch and worth a line
	// for anything else.
	FDoorState Door;
	Door.bOpen = bInitiallyOpen;
	Door.Kind = Kind;
	if (const FDoorLockConfig* Lock = GetDefault<UFacilitySettings>()->FindDoorLock(DoorId))
	{
		Door.LockLevel = Lock->LockLevel;
		Door.KeyItem = Lock->KeyItem;
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Door '%s' has no lock in the facility settings. It opens to anyone."), *DoorId.ToString());
	}

	// AddDoor leaves a known door alone, so the live state keeps a door the player has used.
	FFacilityRules::AddDoor(InitialState, DoorId, Door);
	FFacilityRules::AddDoor(State, DoorId, Door);
}

void UFacilityStateSubsystem::RegisterPickup(const FName PickupId)
{
	if (PickupId.IsNone())
	{
		return;
	}

	// Where the pickup was placed is where its item lies at the start of every attempt, and what the
	// item is comes from the settings by id. A pickup the settings do not name holds nothing.
	const EFacilityItem Item = GetDefault<UFacilitySettings>()->FindPickupItem(PickupId);
	if (Item == EFacilityItem::None)
	{
		UE_LOG(LogScifiSimEscape, Warning,
			TEXT("Pickup '%s' has no entry under Pickups in the facility settings, so nothing lies there. Add +Pickups=(Id=\"%s\",Item=...) to DefaultGame.ini."),
			*PickupId.ToString(), *PickupId.ToString());
		return;
	}

	// AddPickup leaves a known pickup alone, so the live state keeps one the player has emptied.
	FFacilityRules::AddPickup(InitialState, PickupId, Item);
	FFacilityRules::AddPickup(State, PickupId, Item);
}

void UFacilityStateSubsystem::RegisterExit(const EFacilityExit Exit, const FExitState& ExitState)
{
	// What guards the exit when the level starts guards it every attempt. AddExit leaves a known exit alone.
	FFacilityRules::AddExit(InitialState, Exit, ExitState);
	FFacilityRules::AddExit(State, Exit, ExitState);
}

void UFacilityStateSubsystem::RegisterCamera(const FName CameraId, const FCameraState& Camera)
{
	// How the camera was placed is how it starts every attempt. AddCamera leaves a known camera alone, so
	// the live state keeps one the player has looped.
	FFacilityRules::AddCamera(InitialState, CameraId, Camera);
	FFacilityRules::AddCamera(State, CameraId, Camera);
}

void UFacilityStateSubsystem::RegisterFlood(const FName FansId)
{
	// The fans that drain the water are the same every attempt. AddFlood leaves named fans and the water alone.
	FFacilityRules::AddFlood(InitialState, FansId);
	FFacilityRules::AddFlood(State, FansId);
}

/** A routing job in words, for the log: "'CargoLift' onto SECURITY" or "the coolant pump". */
static FString DescribeRoutingOption(const FRoutingOption& Option)
{
	if (Option.Action == ERoutingAction::CoolantPump)
	{
		return TEXT("the coolant pump");
	}

	return FString::Printf(TEXT("'%s' onto %s"), *Option.DeviceId.ToString(), *UEnum::GetDisplayValueAsText(Option.Circuit).ToString());
}

void UFacilityStateSubsystem::RegisterRoutingOption(const FName PanelId, const FRoutingOption& Option)
{
	// What the panel offers when the level starts it offers every attempt. AddRoutingOption leaves a
	// known job alone, so the live state keeps a patch the player has made.
	const bool bAdded = FFacilityRules::AddRoutingOption(InitialState, PanelId, Option);
	FFacilityRules::AddRoutingOption(State, PanelId, Option);

	if (bAdded)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Routing panel '%s' offers %s."), *PanelId.ToString(), *DescribeRoutingOption(Option));
		return;
	}

	// Refused for a reason worth a warning, unless it is a job registered twice, which a re-created switch does.
	if (PanelId.IsNone())
	{
		UE_LOG(LogScifiSimEscape, Warning, TEXT("A routing switch has no DeviceId, so its job cannot join the panel."));
	}
	else if (InitialState.Routing.PanelId != PanelId)
	{
		UE_LOG(LogScifiSimEscape, Warning,
			TEXT("A routing switch registers under '%s', but the routing panel is '%s'. There is one panel, so give every switch its id."),
			*PanelId.ToString(), *InitialState.Routing.PanelId.ToString());
	}
	else if (Option.Action == ERoutingAction::RouteDevice && (Option.DeviceId.IsNone() || Option.Circuit == EFacilityCircuit::EFC_None))
	{
		UE_LOG(LogScifiSimEscape, Warning, TEXT("A routing switch on panel '%s' names no device or no circuit to route it onto, so it does nothing."), *PanelId.ToString());
	}
}

void UFacilityStateSubsystem::RegisterCrate(const FName CrateId, const FName LiftId)
{
	// The crate starts every attempt where it was placed, wedged in the shutter. AddCrate leaves a known
	// crate alone, so the live state keeps one the player has moved.
	FCrateState Crate;
	Crate.LiftId = LiftId;
	FFacilityRules::AddCrate(InitialState, CrateId, Crate);
	FFacilityRules::AddCrate(State, CrateId, Crate);
}

void UFacilityStateSubsystem::SetCircuitState(const EFacilityCircuit Circuit, const ECircuitState NewState)
{
	BroadcastIfChanged(FFacilityRules::SetCircuitState(State, Circuit, NewState));
}

void UFacilityStateSubsystem::SetDeviceCircuit(const FName DeviceId, const EFacilityCircuit NewCircuit)
{
	BroadcastIfChanged(FFacilityRules::SetDeviceCircuit(State, DeviceId, NewCircuit));
}

void UFacilityStateSubsystem::FlipBreaker(const EFacilityCircuit Circuit)
{
	BroadcastIfChanged(FFacilityRules::FlipBreaker(State, Circuit));
}

bool UFacilityStateSubsystem::StartGenerator()
{
	if (CanStartGenerator() == false)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("The generator cannot start: %s."),
			IsGeneratorOverloaded() ? TEXT("it is overloaded") : TEXT("it is running already"));
		return false;
	}

	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::StartGenerator(State);
	UE_LOG(LogScifiSimEscape, Log, TEXT("Player started the generator. Every circuit carries power for %d steps, and the noise raises the alarm."),
		GetGeneratorStepsLeft());

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::OverloadGenerator()
{
	if (CanOverloadGenerator() == false)
	{
		FString Reason = TEXT("the player carries no pry bar");
		if (IsGeneratorOverloaded())
		{
			Reason = TEXT("it is overloaded already");
		}
		else if (IsGeneratorRunning() == false)
		{
			Reason = TEXT("it is not running");
		}

		UE_LOG(LogScifiSimEscape, Log, TEXT("The generator cannot be rigged to overload: %s."), *Reason);
		return false;
	}

	const bool bChanged = FFacilityRules::OverloadGenerator(State);
	UE_LOG(LogScifiSimEscape, Log, TEXT("Player rigged the generator to overload. Every breaker tripped, and the generator is finished for the attempt."));

	BroadcastIfChanged(bChanged);
	return bChanged;
}

bool UFacilityStateSubsystem::CallLift(const FName LiftId, const ELiftStop Stop)
{
	// Asked here as well as inside the rules, for the log: the rules say no, this says why.
	const ELiftObstacle Obstacle = GetLiftObstacle(LiftId);
	if (Obstacle != ELiftObstacle::None)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Lift '%s' will not answer: %s."),
			*LiftId.ToString(), *UEnum::GetDisplayValueAsText(Obstacle).ToString());
		return false;
	}

	const bool bChanged = FFacilityRules::CallLift(State, LiftId, Stop);
	BroadcastIfChanged(bChanged);
	return bChanged;
}

bool UFacilityStateSubsystem::HackLift(const FName LiftId)
{
	if (CanHackLift(LiftId) == false)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Lift '%s' is nothing the handheld can work on right now: level %d, %s, %s."),
			*LiftId.ToString(), GetHackLevel(LiftId),
			IsDevicePowered(LiftId) ? TEXT("powered") : TEXT("unpowered"),
			IsLiftHacked(LiftId) ? TEXT("already stopped") : TEXT("running"));
		return false;
	}

	// Read before the hack, for the log and the alarm broadcast.
	const int32 TargetLevel = GetHackLevel(LiftId);
	const bool bSucceeds = WouldHackSucceed(TargetLevel);
	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::HackLift(State, LiftId);
	if (bSucceeds)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player hacked lift '%s' (level %d, handheld level %d). It is stopped for the attempt."),
			*LiftId.ToString(), TargetLevel, GetHandheldLevel());
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player's hack on lift '%s' failed: level %d is above handheld level %d. %s"),
			*LiftId.ToString(), TargetLevel, GetHandheldLevel(),
			bChanged ? TEXT("The noise raises the alarm.") : TEXT("The alarm is already at the top."));
	}

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::ToggleDoor(const FName DoorId)
{
	// Asked here as well as inside the rules, for the log: the rules say no, this says why.
	const EDoorObstacle Obstacle = GetDoorObstacle(DoorId);
	if (Obstacle != EDoorObstacle::None)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Door '%s' will not move: %s."),
			*DoorId.ToString(), *UEnum::GetDisplayValueAsText(Obstacle).ToString());
		return false;
	}

	// Read before the door moves, for the log and the alarm broadcast: opening the gate under an alert
	// raises the alarm, and if that calls lockdown the gate ends up closed again.
	const bool bLoud = IsDoorOpen(DoorId) == false && IsDoorLoudToOpen(DoorId);
	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::ToggleDoor(State, DoorId);
	if (bChanged && bLoud)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player opened door '%s' under %s. The noise raises the alarm%s."),
			*DoorId.ToString(), *UEnum::GetDisplayValueAsText(OldLevel).ToString(),
			IsDoorOpen(DoorId) ? TEXT("") : TEXT(", and lockdown sealed it again as it opened"));
	}
	else if (bChanged)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Door '%s' %s."),
			*DoorId.ToString(), IsDoorOpen(DoorId) ? TEXT("opened") : TEXT("closed"));
	}

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::ForceDoor(const FName DoorId)
{
	// Asked here as well as inside the rules, for the log: the rules say no, this says why.
	if (CanForceDoor(DoorId) == false)
	{
		const FDoorState* Door = State.Doors.Find(DoorId);
		FString Reason = TEXT("nothing holds it shut, so it opens by hand");
		if (Door == nullptr)
		{
			Reason = TEXT("no door by that id has registered");
		}
		else if (Door->bOpen)
		{
			Reason = TEXT("it is already open");
		}
		else if (Door->Kind == EDoorKind::Gate)
		{
			Reason = TEXT("the gate cannot be forced");
		}
		else if (HasItem(EFacilityItem::PryBar) == false)
		{
			Reason = TEXT("the player carries no pry bar");
		}

		UE_LOG(LogScifiSimEscape, Log, TEXT("Door '%s' cannot be forced: %s."), *DoorId.ToString(), *Reason);
		return false;
	}

	// Read before forcing, for the log and the alarm broadcast: what the pry bar got past, and where the alarm was.
	const EDoorObstacle Obstacle = GetDoorObstacle(DoorId);
	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::ForceDoor(State, DoorId);
	if (bChanged)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player forced door '%s' open with the pry bar, past %s. It stays open, and the noise raises the alarm."),
			*DoorId.ToString(), *UEnum::GetDisplayValueAsText(Obstacle).ToString());
	}

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::HackDoor(const FName DoorId)
{
	if (CanHackDoor(DoorId) == false)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Door '%s' is nothing the handheld can work on right now: level %d, %s, %s."),
			*DoorId.ToString(), GetHackLevel(DoorId),
			IsDevicePowered(DoorId) ? TEXT("powered") : TEXT("unpowered"),
			*UEnum::GetDisplayValueAsText(GetDoorObstacle(DoorId)).ToString());
		return false;
	}

	// Read before the hack, for the log and the alarm broadcast.
	const int32 TargetLevel = GetHackLevel(DoorId);
	const bool bSucceeds = WouldHackSucceed(TargetLevel);
	const bool bLoud = IsDoorLoudToOpen(DoorId);
	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::HackDoor(State, DoorId);
	if (bSucceeds)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player hacked door '%s' open (level %d, handheld level %d)%s."),
			*DoorId.ToString(), TargetLevel, GetHandheldLevel(),
			bLoud == false ? TEXT("") : IsDoorOpen(DoorId)
				? TEXT(". Opening it under an alert is loud: the alarm rises")
				: TEXT(". Opening it under an alert is loud: the alarm rises, and lockdown sealed it again as it opened"));
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player's hack on door '%s' failed: level %d is above handheld level %d. %s"),
			*DoorId.ToString(), TargetLevel, GetHandheldLevel(),
			bChanged ? TEXT("The noise raises the alarm.") : TEXT("The alarm is already at the top."));
	}

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::HackGateController(const FName ControllerId)
{
	if (CanHackGateController(ControllerId) == false)
	{
		const FName GateId = FFacilityRules::FindDoorOfKind(State, EDoorKind::Gate);
		UE_LOG(LogScifiSimEscape, Log, TEXT("Gate controller '%s' is nothing the handheld can work on right now: level %d, %s, gate %s."),
			*ControllerId.ToString(), GetHackLevel(ControllerId),
			IsDevicePowered(ControllerId) ? TEXT("powered") : TEXT("unpowered"),
			GateId.IsNone() ? TEXT("not registered") : *UEnum::GetDisplayValueAsText(GetDoorObstacle(GateId)).ToString());
		return false;
	}

	// Read before the hack, for the log and the alarm broadcast.
	const FName GateId = FFacilityRules::FindDoorOfKind(State, EDoorKind::Gate);
	const int32 TargetLevel = GetHackLevel(ControllerId);
	const bool bSucceeds = WouldHackSucceed(TargetLevel);
	const bool bLoud = IsDoorLoudToOpen(GateId);
	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::HackGateController(State, ControllerId);
	if (bSucceeds)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player hacked gate controller '%s' (level %d, handheld level %d): the gate opens%s."),
			*ControllerId.ToString(), TargetLevel, GetHandheldLevel(),
			bLoud == false ? TEXT("") : IsDoorOpen(GateId)
				? TEXT(". Opening it under an alert is loud: the alarm rises")
				: TEXT(". Opening it under an alert is loud: the alarm rises, and lockdown sealed it again as it opened"));
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player's hack on gate controller '%s' failed: level %d is above handheld level %d. %s"),
			*ControllerId.ToString(), TargetLevel, GetHandheldLevel(),
			bChanged ? TEXT("The noise raises the alarm.") : TEXT("The alarm is already at the top."));
	}

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::RouteDevice(const FName PanelId, const FName DeviceId, const EFacilityCircuit Circuit)
{
	if (CanRouteDevice(PanelId, DeviceId, Circuit) == false)
	{
		FString Reason = TEXT("no switch offers that job");
		if (State.Routing.PanelId != PanelId)
		{
			Reason = TEXT("no switch registered the panel");
		}
		else if (GetHackLevel(PanelId) <= 0)
		{
			Reason = TEXT("the settings give it no hack level");
		}
		else if (IsDevicePowered(PanelId) == false)
		{
			Reason = TEXT("it has no power");
		}
		else if (State.DeviceCircuits.Contains(DeviceId) == false)
		{
			Reason = TEXT("no such device registered");
		}
		else if (GetDeviceCircuit(DeviceId) == Circuit)
		{
			Reason = TEXT("the device is on that circuit already");
		}

		UE_LOG(LogScifiSimEscape, Log, TEXT("Routing panel '%s' cannot route '%s' onto %s right now: %s."),
			*PanelId.ToString(), *DeviceId.ToString(), *UEnum::GetDisplayValueAsText(Circuit).ToString(), *Reason);
		return false;
	}

	// Read before the hack, for the log and the alarm broadcast.
	const int32 TargetLevel = GetHackLevel(PanelId);
	const bool bSucceeds = WouldHackSucceed(TargetLevel);
	const EAlarmLevel OldLevel = GetAlarmLevel();
	const EFacilityCircuit From = GetDeviceCircuit(DeviceId);
	const FName Displaced = IsDevicePatched(DeviceId) ? NAME_None : GetPatchedDevice();

	const bool bChanged = FFacilityRules::RouteDevice(State, PanelId, DeviceId, Circuit);
	if (bSucceeds)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player rerouted '%s' from %s onto %s at routing panel '%s' (level %d, handheld level %d).%s%s"),
			*DeviceId.ToString(), *UEnum::GetDisplayValueAsText(From).ToString(), *UEnum::GetDisplayValueAsText(GetDeviceCircuit(DeviceId)).ToString(),
			*PanelId.ToString(), TargetLevel, GetHandheldLevel(),
			IsDevicePatched(DeviceId) ? TEXT("") : TEXT(" It is back on the circuit it was placed on."),
			Displaced.IsNone() ? TEXT("") : *FString::Printf(TEXT(" '%s' went back to %s first: the panel carries one patch."),
				*Displaced.ToString(), *UEnum::GetDisplayValueAsText(GetDeviceCircuit(Displaced)).ToString()));
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player's hack on routing panel '%s' failed: level %d is above handheld level %d. %s"),
			*PanelId.ToString(), TargetLevel, GetHandheldLevel(),
			bChanged ? TEXT("The noise raises the alarm.") : TEXT("The alarm is already at the top."));
	}

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::RunCoolantPump(const FName PanelId)
{
	if (CanRunCoolantPump(PanelId) == false)
	{
		FString Reason = TEXT("no switch offers the pump");
		if (State.Routing.PanelId != PanelId)
		{
			Reason = TEXT("no switch registered the panel");
		}
		else if (GetHackLevel(PanelId) <= 0)
		{
			Reason = TEXT("the settings give it no hack level");
		}
		else if (IsDevicePowered(PanelId) == false)
		{
			Reason = TEXT("it has no power");
		}
		else if (IsPlantFlooded())
		{
			Reason = TEXT("Plant is flooded already");
		}

		UE_LOG(LogScifiSimEscape, Log, TEXT("Routing panel '%s' cannot run the coolant pump right now: %s."), *PanelId.ToString(), *Reason);
		return false;
	}

	// Read before the hack, for the log and the alarm broadcast.
	const int32 TargetLevel = GetHackLevel(PanelId);
	const bool bSucceeds = WouldHackSucceed(TargetLevel);
	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::RunCoolantPump(State, PanelId);
	if (bSucceeds)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player ran the coolant pump from routing panel '%s' (level %d, handheld level %d). Plant floods; the valve is closed, so the fans can drain it. The water is %s."),
			*PanelId.ToString(), TargetLevel, GetHandheldLevel(),
			IsFloodLethal() ? TEXT("lethal while any circuit is live") : TEXT("harmless while every circuit is dead"));
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player's hack on routing panel '%s' failed: level %d is above handheld level %d. %s"),
			*PanelId.ToString(), TargetLevel, GetHandheldLevel(),
			bChanged ? TEXT("The noise raises the alarm.") : TEXT("The alarm is already at the top."));
	}

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::HackCamera(const FName CameraId)
{
	if (CanHackCamera(CameraId) == false)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Camera '%s' is nothing the handheld can work on right now: level %d, %s, %s."),
			*CameraId.ToString(), GetHackLevel(CameraId),
			IsDevicePowered(CameraId) ? TEXT("powered") : TEXT("unpowered"),
			IsCameraLooped(CameraId) ? TEXT("looped already") : TEXT("not looped"));
		return false;
	}

	// Read before the hack, for the log and the alarm broadcast.
	const int32 TargetLevel = GetHackLevel(CameraId);
	const bool bSucceeds = WouldHackSucceed(TargetLevel);
	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::HackCamera(State, CameraId);
	if (bSucceeds)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player looped camera '%s' (level %d, handheld level %d). It sees nothing more this attempt."),
			*CameraId.ToString(), TargetLevel, GetHandheldLevel());
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player's hack on camera '%s' failed: level %d is above handheld level %d. %s"),
			*CameraId.ToString(), TargetLevel, GetHandheldLevel(),
			bChanged ? TEXT("The noise raises the alarm.") : TEXT("The alarm is already at the top."));
	}

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::CameraSighting(const FName CameraId)
{
	if (IsCameraWatching(CameraId) == false)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Camera '%s' reported a sighting while it was not watching. Nothing happens."), *CameraId.ToString());
		return false;
	}

	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::CameraSighting(State, CameraId);
	UE_LOG(LogScifiSimEscape, Log, TEXT("Camera '%s' saw the player. %s"),
		*CameraId.ToString(), bChanged ? TEXT("The alarm rises.") : TEXT("The alarm is already at the top."));

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::FloodPlant()
{
	const bool bChanged = FFacilityRules::FloodPlant(State);
	if (bChanged)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Plant floods. The water is %s, shorts what is live after %d step(s), and eats through the tunnel lock after %d."),
			IsFloodLethal() ? TEXT("lethal while any circuit is live") : TEXT("harmless while every circuit is dead"),
			State.Config.FloodShortSteps, State.Config.CorrosionSteps);
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Plant is flooded already; more water changes nothing."));
	}

	BroadcastIfChanged(bChanged);
	return bChanged;
}

bool UFacilityStateSubsystem::OpenCoolantValve()
{
	if (CanOpenCoolantValve() == false)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("The coolant valve cannot be opened: %s."),
			IsCoolantValveOpen() ? TEXT("it is open already") : TEXT("it is seized and the player carries no pry bar"));
		return false;
	}

	const bool bChanged = FFacilityRules::OpenCoolantValve(State);
	UE_LOG(LogScifiSimEscape, Log, TEXT("Player forced the coolant valve open with the pry bar. Plant floods, and the fans cannot drain it while the valve stays open."));

	BroadcastIfChanged(bChanged);
	return bChanged;
}

bool UFacilityStateSubsystem::CloseCoolantValve()
{
	const bool bChanged = FFacilityRules::CloseCoolantValve(State);
	if (bChanged)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player closed the coolant valve. %s"),
			IsPlantFlooded() ? TEXT("The water stays until the fans drain it.") : TEXT(""));
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("The coolant valve is closed already."));
	}

	BroadcastIfChanged(bChanged);
	return bChanged;
}

bool UFacilityStateSubsystem::OverloadServerRack(const FName RackId)
{
	if (CanOverloadServerRack(RackId) == false)
	{
		FString Reason = TEXT("the player carries no pry bar");
		if (IsServerRackOverloaded())
		{
			Reason = TEXT("it is burning already");
		}
		else if (IsDevicePowered(RackId) == false)
		{
			Reason = TEXT("it has no power");
		}

		UE_LOG(LogScifiSimEscape, Log, TEXT("Server rack '%s' cannot be rigged to overload: %s."), *RackId.ToString(), *Reason);
		return false;
	}

	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::OverloadServerRack(State, RackId);
	UE_LOG(LogScifiSimEscape, Log, TEXT("Player rigged server rack '%s' to overload. It burns, the Lab fills with smoke, the alarm %s, and the sprinklers flood Plant."),
		*RackId.ToString(), OldLevel < EAlarmLevel::EAL_Alerted ? TEXT("goes to Alerted") : TEXT("is already up"));

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::MoveCrate(const FName CrateId)
{
	if (CanMoveCrate(CrateId) == false)
	{
		const FCrateState* Crate = State.Crates.Find(CrateId);
		FString Reason = TEXT("the player carries no pry bar and its lift answers no call");
		if (Crate == nullptr)
		{
			Reason = TEXT("no crate by that id has registered");
		}
		else if (Crate->Position != ECratePosition::JammingShutter)
		{
			Reason = TEXT("it is off the shutter already");
		}

		UE_LOG(LogScifiSimEscape, Log, TEXT("Crate '%s' cannot be moved: %s."), *CrateId.ToString(), *Reason);
		return false;
	}

	const bool bChanged = FFacilityRules::MoveCrate(State, CrateId);
	UE_LOG(LogScifiSimEscape, Log, TEXT("Player moved crate '%s' off the shutter to the shaft head, with %s."),
		*CrateId.ToString(), HasItem(EFacilityItem::PryBar) ? TEXT("the pry bar") : TEXT("the lift"));

	BroadcastIfChanged(bChanged);
	return bChanged;
}

bool UFacilityStateSubsystem::DropCrate(const FName CrateId)
{
	if (CanDropCrate(CrateId) == false)
	{
		const FCrateState* Crate = State.Crates.Find(CrateId);
		FString Reason = TEXT("the car is at the top, so the crate sits on it");
		if (Crate == nullptr)
		{
			Reason = TEXT("no crate by that id has registered");
		}
		else if (Crate->Position != ECratePosition::AtShaftHead)
		{
			Reason = TEXT("it is not at the shaft head");
		}
		else if (State.Lifts.Contains(Crate->LiftId) == false)
		{
			Reason = FString::Printf(TEXT("its lift '%s' has not registered"), *Crate->LiftId.ToString());
		}

		UE_LOG(LogScifiSimEscape, Log, TEXT("Crate '%s' cannot be dropped: %s."), *CrateId.ToString(), *Reason);
		return false;
	}

	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::DropCrate(State, CrateId);
	UE_LOG(LogScifiSimEscape, Log, TEXT("Player pushed crate '%s' down the shaft. It lands on whatever stood below, and the noise raises the alarm."), *CrateId.ToString());

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::TakePickup(const FName PickupId)
{
	// Read before the take empties it, for the log.
	const EFacilityItem Item = GetPickupItem(PickupId);

	const bool bChanged = FFacilityRules::TakePickup(State, PickupId);
	if (bChanged)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player took %s from pickup '%s'. Keycard level is now %d, handheld level %d."),
			*UEnum::GetDisplayValueAsText(Item).ToString(), *PickupId.ToString(), GetKeycardLevel(), GetHandheldLevel());
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Nothing to take at pickup '%s'."), *PickupId.ToString());
	}

	BroadcastIfChanged(bChanged);
	return bChanged;
}

bool UFacilityStateSubsystem::GiveItem(const EFacilityItem Item)
{
	const bool bChanged = FFacilityRules::GiveItem(State, Item);
	if (bChanged)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player now carries %s. Keycard level is now %d, handheld level %d."),
			*UEnum::GetDisplayValueAsText(Item).ToString(), GetKeycardLevel(), GetHandheldLevel());
	}

	BroadcastIfChanged(bChanged);
	return bChanged;
}

bool UFacilityStateSubsystem::TakeItem(const EFacilityItem Item)
{
	const bool bChanged = FFacilityRules::TakeItem(State, Item);
	if (bChanged)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("%s left the player's hands. Keycard level is now %d."),
			*UEnum::GetDisplayValueAsText(Item).ToString(), GetKeycardLevel());
	}

	BroadcastIfChanged(bChanged);
	return bChanged;
}

bool UFacilityStateSubsystem::Escape(const EFacilityExit Exit)
{
	const bool bChanged = FFacilityRules::Escape(State, Exit);
	if (bChanged)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player escaped through the %s. The attempt is won."),
			*UEnum::GetDisplayValueAsText(Exit).ToString());
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("The %s does not let the player through right now."),
			*UEnum::GetDisplayValueAsText(Exit).ToString());
	}

	BroadcastIfChanged(bChanged);
	return bChanged;
}

bool UFacilityStateSubsystem::SetAlarmLevel(const EAlarmLevel NewLevel)
{
	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::SetAlarmLevel(State, NewLevel);
	if (bChanged == false)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Alarm stays at %s: %s."),
			*UEnum::GetDisplayValueAsText(OldLevel).ToString(),
			NewLevel == EAlarmLevel::EAL_None ? TEXT("None is not a level") : TEXT("it is there already"));
	}

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::RaiseAlarmLevelByOne()
{
	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::RaiseAlarmLevelByOne(State);
	if (bChanged == false)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Alarm stays at %s: nothing is above it."),
			*UEnum::GetDisplayValueAsText(OldLevel).ToString());
	}

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::LowerAlarmLevelByOne()
{
	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::LowerAlarmLevelByOne(State);
	if (bChanged == false)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Alarm stays at %s: nothing is below it."),
			*UEnum::GetDisplayValueAsText(OldLevel).ToString());
	}

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::ResetAlarmLevel()
{
	return SetAlarmLevel(EAlarmLevel::EAL_Quiet);
}

bool UFacilityStateSubsystem::SilenceAlarm(const FName ConsoleId)
{
	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bChanged = FFacilityRules::SilenceAlarm(State, ConsoleId);
	if (bChanged)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Player silenced the alarm at console '%s'."), *ConsoleId.ToString());
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("Console '%s' cannot silence the alarm: %s."),
			*ConsoleId.ToString(),
			IsDevicePowered(ConsoleId) == false ? TEXT("it has no power") : TEXT("the alarm is quiet already"));
	}

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

bool UFacilityStateSubsystem::AdvanceStep()
{
	const EAlarmLevel OldLevel = GetAlarmLevel();

	const bool bWasGeneratorRunning = IsGeneratorRunning();
	const bool bWasFlooded = IsPlantFlooded();
	const FName TunnelDoorId = State.Exits.Contains(EFacilityExit::ServiceTunnel) ? State.Exits[EFacilityExit::ServiceTunnel].DoorId : NAME_None;
	const bool bWasTunnelCorroded = IsDoorLockCorroded(TunnelDoorId);
	TMap<EFacilityCircuit, ECircuitState> OldCircuits = State.Circuits;

	const bool bChanged = FFacilityRules::AdvanceStep(State);
	if (GetAlarmLevel() != OldLevel)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("The alarm came down after %d quiet steps."), GetAlarmDecaySteps());
	}
	if (bWasGeneratorRunning && IsGeneratorRunning() == false)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("The generator ran out. Power is back to what the breakers say."));
	}
	if (bWasFlooded && IsPlantFlooded() == false)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("The fans drained the flood. The Plant floor is dry."));
	}
	for (const TPair<EFacilityCircuit, ECircuitState>& Entry : State.Circuits)
	{
		const ECircuitState* Old = OldCircuits.Find(Entry.Key);
		if (Entry.Value == ECircuitState::Shorted && (Old == nullptr || *Old != ECircuitState::Shorted))
		{
			UE_LOG(LogScifiSimEscape, Log, TEXT("The flood shorted %s after standing %d step(s). It is dead for the attempt%s."),
				*UEnum::GetDisplayValueAsText(Entry.Key).ToString(), GetFloodSteps(),
				Entry.Key == EFacilityCircuit::EFC_Doors ? TEXT(", and the lockdown doors fail open") : TEXT(""));
		}
	}
	if (bWasTunnelCorroded == false && IsDoorLockCorroded(TunnelDoorId))
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("The flood corroded the lock of tunnel door '%s' through after %d step(s). It opens to anyone."),
			*TunnelDoorId.ToString(), GetFloodSteps());
	}

	// Every step, so kept out of the default log level.
	UE_LOG(LogScifiSimEscape, Verbose, TEXT("Step. Alarm %s, %d of %d quiet steps, generator %d step(s) left, flood %s."),
		*UEnum::GetDisplayValueAsText(GetAlarmLevel()).ToString(), GetQuietSteps(), GetAlarmDecaySteps(), GetGeneratorStepsLeft(),
		IsPlantFlooded() ? *FString::Printf(TEXT("standing %d step(s)"), GetFloodSteps()) : TEXT("dry"));

	BroadcastIfChanged(bChanged);
	BroadcastAlarmLevelIfChanged(OldLevel);
	return bChanged;
}

void UFacilityStateSubsystem::ResetToInitialState()
{
	const EAlarmLevel OldAlarmLevel = GetAlarmLevel();
	State = InitialState;

	UE_LOG(LogScifiSimEscape, Log, TEXT("Facility state reset to where the attempt started."));

	// Held for the broadcasts only, so listeners can tell a reset from something happening in the world.
	bResetting = true;
	BroadcastIfChanged(true);
	BroadcastAlarmLevelIfChanged(OldAlarmLevel);
	bResetting = false;
}

void UFacilityStateSubsystem::BroadcastIfChanged(const bool bChanged)
{
	if (bChanged == false)
	{
		return;
	}

	// The breakers as the panel has them; while the generator runs every cut one carries power all the same.
	UE_LOG(LogScifiSimEscape, Log, TEXT("Facility state changed. DOORS=%s SECURITY=%s PLANT=%s GENERATOR=%s ALARM=%s FLOOD=%s ROUTED=%s"),
		*UEnum::GetDisplayValueAsText(GetCircuitState(EFacilityCircuit::EFC_Doors)).ToString(),
		*UEnum::GetDisplayValueAsText(GetCircuitState(EFacilityCircuit::EFC_Security)).ToString(),
		*UEnum::GetDisplayValueAsText(GetCircuitState(EFacilityCircuit::EFC_Plant)).ToString(),
		IsGeneratorOverloaded() ? TEXT("overloaded") : IsGeneratorRunning() ? *FString::Printf(TEXT("running(%d)"), GetGeneratorStepsLeft()) : TEXT("off"),
		*UEnum::GetDisplayValueAsText(GetAlarmLevel()).ToString(),
		IsPlantFlooded() == false ? TEXT("dry") : IsFloodLethal() ? *FString::Printf(TEXT("lethal(%d)"), GetFloodSteps()) : *FString::Printf(TEXT("standing(%d)"), GetFloodSteps()),
		GetPatchedDevice().IsNone() ? TEXT("none") : *FString::Printf(TEXT("%s>%s"), *GetPatchedDevice().ToString(), *UEnum::GetDisplayValueAsText(GetDeviceCircuit(GetPatchedDevice())).ToString()));

	OnStateChanged.Broadcast();
}

void UFacilityStateSubsystem::ReportUnclaimedSettings()
{
	const UFacilitySettings* Settings = GetDefault<UFacilitySettings>();

	for (const FDoorLockConfig& Lock : Settings->DoorLocks)
	{
		if (State.Doors.Contains(Lock.Id) == false)
		{
			UE_LOG(LogScifiSimEscape, Warning,
				TEXT("The facility settings give door '%s' a lock, but no door registered under that id. Check the DeviceId of the placed door, or the DoorLocks entry in DefaultGame.ini."),
				*Lock.Id.ToString());
		}
	}

	for (const FPickupConfig& Pickup : Settings->Pickups)
	{
		if (State.Pickups.Contains(Pickup.Id) == false)
		{
			UE_LOG(LogScifiSimEscape, Warning,
				TEXT("The facility settings put %s at pickup '%s', but no pickup registered under that id, so the item is nowhere. Check the PickupId of the placed pickup, or the Pickups entry in DefaultGame.ini."),
				*UEnum::GetDisplayValueAsText(Pickup.Item).ToString(), *Pickup.Id.ToString());
		}
	}

	for (const FHackTargetConfig& Target : Settings->HackTargets)
	{
		if (State.DeviceCircuits.Contains(Target.Id) == false)
		{
			UE_LOG(LogScifiSimEscape, Warning,
				TEXT("The facility settings give device '%s' hack level %d, but no device registered under that id. Check the DeviceId of the placed device, or the HackTargets entry in DefaultGame.ini."),
				*Target.Id.ToString(), Target.Level);
		}
	}
}

void UFacilityStateSubsystem::BroadcastAlarmLevelIfChanged(const EAlarmLevel OldLevel)
{
	const EAlarmLevel NewLevel = GetAlarmLevel();
	if (NewLevel == OldLevel)
	{
		return;
	}

	UE_LOG(LogScifiSimEscape, Log, TEXT("Alarm %s from %s to %s."),
		NewLevel > OldLevel ? TEXT("raised") : TEXT("lowered"),
		*UEnum::GetDisplayValueAsText(OldLevel).ToString(),
		*UEnum::GetDisplayValueAsText(NewLevel).ToString());

	OnAlarmLevelChanged.Broadcast(OldLevel, NewLevel);
}

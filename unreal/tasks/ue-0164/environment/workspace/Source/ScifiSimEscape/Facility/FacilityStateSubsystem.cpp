#include "Facility/FacilityStateSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

void UFacilityStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	State = FFacilityState{};
	InitialState = State;
	DeviceActors.Reset();
	bResetting = false;
}

void UFacilityStateSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StepTimerHandle);
	}

	DeviceActors.Reset();
	Super::Deinitialize();
}

void UFacilityStateSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
}

void UFacilityStateSubsystem::HandleStepTimer()
{
}

bool UFacilityStateSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

ECircuitState UFacilityStateSubsystem::GetCircuitState(const EFacilityCircuit Circuit) const
{
	return ECircuitState::Cut;
}

bool UFacilityStateSubsystem::IsCircuitLive(const EFacilityCircuit Circuit) const
{
	return false;
}

EFacilityCircuit UFacilityStateSubsystem::GetDeviceCircuit(const FName DeviceId) const
{
	return EFacilityCircuit::EFC_None;
}

bool UFacilityStateSubsystem::IsDevicePowered(const FName DeviceId) const
{
	return false;
}

bool UFacilityStateSubsystem::IsBreakerOn(const EFacilityCircuit Circuit) const
{
	return false;
}

bool UFacilityStateSubsystem::IsGeneratorRunning() const
{
	return false;
}

int32 UFacilityStateSubsystem::GetGeneratorStepsLeft() const
{
	return 0;
}

bool UFacilityStateSubsystem::IsGeneratorOverloaded() const
{
	return false;
}

bool UFacilityStateSubsystem::CanStartGenerator() const
{
	return false;
}

bool UFacilityStateSubsystem::CanOverloadGenerator() const
{
	return false;
}

ELiftStop UFacilityStateSubsystem::GetLiftStop(const FName LiftId) const
{
	return ELiftStop::ELS_Bottom;
}

bool UFacilityStateSubsystem::CanUseLift(const FName LiftId) const
{
	return false;
}

ELiftObstacle UFacilityStateSubsystem::GetLiftObstacle(const FName LiftId) const
{
	return ELiftObstacle::None;
}

bool UFacilityStateSubsystem::IsLiftHacked(const FName LiftId) const
{
	return false;
}

bool UFacilityStateSubsystem::CanHackLift(const FName LiftId) const
{
	return false;
}

bool UFacilityStateSubsystem::IsDoorOpen(const FName DoorId) const
{
	return false;
}

EDoorObstacle UFacilityStateSubsystem::GetDoorObstacle(const FName DoorId) const
{
	return EDoorObstacle::None;
}

bool UFacilityStateSubsystem::CanOpenDoor(const FName DoorId) const
{
	return false;
}

bool UFacilityStateSubsystem::CanCloseDoor(const FName DoorId) const
{
	return false;
}

bool UFacilityStateSubsystem::CanForceDoor(const FName DoorId) const
{
	return false;
}

bool UFacilityStateSubsystem::CanHackDoor(const FName DoorId) const
{
	return false;
}

bool UFacilityStateSubsystem::IsDoorLoudToOpen(const FName DoorId) const
{
	return false;
}

bool UFacilityStateSubsystem::HasItem(const EFacilityItem Item) const
{
	return false;
}

TArray<EFacilityItem> UFacilityStateSubsystem::GetInventory() const
{
	return {};
}

int32 UFacilityStateSubsystem::GetKeycardLevel() const
{
	return 0;
}

int32 UFacilityStateSubsystem::GetHackLevel(const FName DeviceId) const
{
	return 0;
}

int32 UFacilityStateSubsystem::GetHandheldLevel() const
{
	return 0;
}

bool UFacilityStateSubsystem::WouldHackSucceed(const int32 TargetLevel) const
{
	return false;
}

bool UFacilityStateSubsystem::IsServerDrivePulled() const
{
	return false;
}

EFacilityItem UFacilityStateSubsystem::GetPickupItem(const FName PickupId) const
{
	return EFacilityItem::None;
}

bool UFacilityStateSubsystem::IsPickupTaken(const FName PickupId) const
{
	return false;
}

bool UFacilityStateSubsystem::CanTakePickup(const FName PickupId) const
{
	return false;
}

bool UFacilityStateSubsystem::AreFansRunning(const FName FansId) const
{
	return false;
}

bool UFacilityStateSubsystem::AreLightsOn(const FName LightsId) const
{
	return false;
}

bool UFacilityStateSubsystem::IsCameraLooped(const FName CameraId) const
{
	return false;
}

bool UFacilityStateSubsystem::IsCameraWatching(const FName CameraId) const
{
	return false;
}

bool UFacilityStateSubsystem::CanHackCamera(const FName CameraId) const
{
	return false;
}

bool UFacilityStateSubsystem::CanHackGateController(const FName ControllerId) const
{
	return false;
}

bool UFacilityStateSubsystem::CanUseRoutingPanel(const FName PanelId) const
{
	return false;
}

FName UFacilityStateSubsystem::GetPatchedDevice() const
{
	return NAME_None;
}

bool UFacilityStateSubsystem::IsDevicePatched(const FName DeviceId) const
{
	return false;
}

EFacilityCircuit UFacilityStateSubsystem::GetHomeCircuit(const FName DeviceId) const
{
	return EFacilityCircuit::EFC_None;
}

bool UFacilityStateSubsystem::CanRouteDevice(
	const FName PanelId,
	const FName DeviceId,
	const EFacilityCircuit Circuit) const
{
	return false;
}

bool UFacilityStateSubsystem::CanRunCoolantPump(const FName PanelId) const
{
	return false;
}

bool UFacilityStateSubsystem::IsDoorLockCorroded(const FName DoorId) const
{
	return false;
}

bool UFacilityStateSubsystem::IsPlantFlooded() const
{
	return false;
}

int32 UFacilityStateSubsystem::GetFloodSteps() const
{
	return 0;
}

bool UFacilityStateSubsystem::IsFloodLethal() const
{
	return false;
}

bool UFacilityStateSubsystem::WouldFansDrainFlood() const
{
	return false;
}

bool UFacilityStateSubsystem::IsCoolantValveOpen() const
{
	return false;
}

bool UFacilityStateSubsystem::CanOpenCoolantValve() const
{
	return false;
}

bool UFacilityStateSubsystem::CanCloseCoolantValve() const
{
	return false;
}

bool UFacilityStateSubsystem::IsServerRackOverloaded() const
{
	return false;
}

bool UFacilityStateSubsystem::IsLabSmokeFilled() const
{
	return false;
}

bool UFacilityStateSubsystem::CanOverloadServerRack(const FName RackId) const
{
	return false;
}

ECratePosition UFacilityStateSubsystem::GetCratePosition(const FName CrateId) const
{
	return ECratePosition::InPlant;
}

bool UFacilityStateSubsystem::IsCrateJammingShutter(const FName CrateId) const
{
	return false;
}

bool UFacilityStateSubsystem::IsDockShutterJammed() const
{
	return false;
}

bool UFacilityStateSubsystem::WasCrateDropped(const FName CrateId) const
{
	return false;
}

bool UFacilityStateSubsystem::CanMoveCrate(const FName CrateId) const
{
	return false;
}

bool UFacilityStateSubsystem::CanDropCrate(const FName CrateId) const
{
	return false;
}

bool UFacilityStateSubsystem::HasEscaped() const
{
	return false;
}

EFacilityExit UFacilityStateSubsystem::GetEscapeExit() const
{
	return EFacilityExit::None;
}

bool UFacilityStateSubsystem::CanEscape(const EFacilityExit Exit) const
{
	return false;
}

EAlarmLevel UFacilityStateSubsystem::GetAlarmLevel() const
{
	return EAlarmLevel::EAL_Quiet;
}

bool UFacilityStateSubsystem::IsLockdown() const
{
	return false;
}

bool UFacilityStateSubsystem::CanSilenceAlarm(const FName ConsoleId) const
{
	return false;
}

AActor* UFacilityStateSubsystem::FindDeviceActor(const FName DeviceId) const
{
	return nullptr;
}

void UFacilityStateSubsystem::RegisterDevice(
	AActor* DeviceActor,
	const FName DeviceId,
	const EFacilityCircuit DefaultCircuit,
	const bool bSharesId)
{
}

void UFacilityStateSubsystem::UnregisterDevice(AActor* DeviceActor, const FName DeviceId)
{
}

void UFacilityStateSubsystem::RegisterLift(const FName LiftId, const FLiftState& Lift)
{
}

void UFacilityStateSubsystem::RegisterDoor(
	const FName DoorId,
	const EDoorKind Kind,
	const bool bInitiallyOpen)
{
}

void UFacilityStateSubsystem::RegisterPickup(const FName PickupId)
{
}

void UFacilityStateSubsystem::RegisterExit(
	const EFacilityExit Exit,
	const FExitState& ExitState)
{
}

void UFacilityStateSubsystem::RegisterCamera(
	const FName CameraId,
	const FCameraState& Camera)
{
}

void UFacilityStateSubsystem::RegisterFlood(const FName FansId)
{
}

void UFacilityStateSubsystem::RegisterRoutingOption(
	const FName PanelId,
	const FRoutingOption& Option)
{
}

void UFacilityStateSubsystem::RegisterCrate(const FName CrateId, const FName LiftId)
{
}

void UFacilityStateSubsystem::SetCircuitState(
	const EFacilityCircuit Circuit,
	const ECircuitState NewState)
{
}

void UFacilityStateSubsystem::SetDeviceCircuit(
	const FName DeviceId,
	const EFacilityCircuit NewCircuit)
{
}

void UFacilityStateSubsystem::FlipBreaker(const EFacilityCircuit Circuit)
{
}

bool UFacilityStateSubsystem::StartGenerator()
{
	return false;
}

bool UFacilityStateSubsystem::OverloadGenerator()
{
	return false;
}

bool UFacilityStateSubsystem::CallLift(const FName LiftId, const ELiftStop Stop)
{
	return false;
}

bool UFacilityStateSubsystem::HackLift(const FName LiftId)
{
	return false;
}

bool UFacilityStateSubsystem::ToggleDoor(const FName DoorId)
{
	return false;
}

bool UFacilityStateSubsystem::ForceDoor(const FName DoorId)
{
	return false;
}

bool UFacilityStateSubsystem::HackDoor(const FName DoorId)
{
	return false;
}

bool UFacilityStateSubsystem::HackGateController(const FName ControllerId)
{
	return false;
}

bool UFacilityStateSubsystem::RouteDevice(
	const FName PanelId,
	const FName DeviceId,
	const EFacilityCircuit Circuit)
{
	return false;
}

bool UFacilityStateSubsystem::RunCoolantPump(const FName PanelId)
{
	return false;
}

bool UFacilityStateSubsystem::HackCamera(const FName CameraId)
{
	return false;
}

bool UFacilityStateSubsystem::CameraSighting(const FName CameraId)
{
	return false;
}

bool UFacilityStateSubsystem::FloodPlant()
{
	return false;
}

bool UFacilityStateSubsystem::OpenCoolantValve()
{
	return false;
}

bool UFacilityStateSubsystem::CloseCoolantValve()
{
	return false;
}

bool UFacilityStateSubsystem::OverloadServerRack(const FName RackId)
{
	return false;
}

bool UFacilityStateSubsystem::MoveCrate(const FName CrateId)
{
	return false;
}

bool UFacilityStateSubsystem::DropCrate(const FName CrateId)
{
	return false;
}

bool UFacilityStateSubsystem::TakePickup(const FName PickupId)
{
	return false;
}

bool UFacilityStateSubsystem::GiveItem(const EFacilityItem Item)
{
	return false;
}

bool UFacilityStateSubsystem::TakeItem(const EFacilityItem Item)
{
	return false;
}

bool UFacilityStateSubsystem::Escape(const EFacilityExit Exit)
{
	return false;
}

bool UFacilityStateSubsystem::SetAlarmLevel(const EAlarmLevel NewLevel)
{
	return false;
}

bool UFacilityStateSubsystem::RaiseAlarmLevelByOne()
{
	return false;
}

bool UFacilityStateSubsystem::LowerAlarmLevelByOne()
{
	return false;
}

bool UFacilityStateSubsystem::ResetAlarmLevel()
{
	return false;
}

bool UFacilityStateSubsystem::SilenceAlarm(const FName ConsoleId)
{
	return false;
}

bool UFacilityStateSubsystem::AdvanceStep()
{
	return false;
}

void UFacilityStateSubsystem::ResetToInitialState()
{
}

void UFacilityStateSubsystem::BroadcastIfChanged(const bool bChanged)
{
}

void UFacilityStateSubsystem::ReportUnclaimedSettings()
{
}

void UFacilityStateSubsystem::BroadcastAlarmLevelIfChanged(const EAlarmLevel OldLevel)
{
}

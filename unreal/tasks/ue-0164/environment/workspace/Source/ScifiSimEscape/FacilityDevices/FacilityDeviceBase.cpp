#include "FacilityDevices/FacilityDeviceBase.h"

#include "Facility/FacilityStateSubsystem.h"
#include "ScifiSimEscape.h"

#define LOCTEXT_NAMESPACE "FacilityDevice"

void AFacilityDeviceBase::BeginPlay()
{
	// Restore the published gameplay contract.
	Super::BeginPlay();
}

void AFacilityDeviceBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Restore the published gameplay contract.
	Super::EndPlay(EndPlayReason);
}

EFacilityCircuit AFacilityDeviceBase::GetCircuit() const
{
	// Restore the published gameplay contract.
	return {};
}

bool AFacilityDeviceBase::IsPowered() const
{
	// Restore the published gameplay contract.
	return {};
}

int32 AFacilityDeviceBase::GetHackLevel() const
{
	// Restore the published gameplay contract.
	return {};
}

FText AFacilityDeviceBase::DescribeNoPower(const EFacilityCircuit Circuit)
{
	// Restore the published gameplay contract.
	return {};
}

void AFacilityDeviceBase::PreFacilityStateChanged()
{
	// Restore the published gameplay contract.
}

void AFacilityDeviceBase::ReconcilePower(const bool bForceNotify)
{
	// Restore the published gameplay contract.
}

#undef LOCTEXT_NAMESPACE

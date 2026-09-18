#include "FacilityDevices/Lift/FacilityLiftCallPanel.h"

#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityStateSubsystem.h"
#include "FacilityDevices/FacilityDeviceBase.h"
#include "InteractionSystem/InteractableComponent.h"

#define LOCTEXT_NAMESPACE "FacilityLiftCallPanel"

AFacilityLiftCallPanel::AFacilityLiftCallPanel()
{
	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Root);
}

// UE-0144: restore the panel behavior described in instruction.md.
bool AFacilityLiftCallPanel::Call() { return false; }
bool AFacilityLiftCallPanel::CanCall() const { return false; }
bool AFacilityLiftCallPanel::IsCarHere() const { return false; }
bool AFacilityLiftCallPanel::Hack() { return false; }
bool AFacilityLiftCallPanel::CanHack() const { return false; }
void AFacilityLiftCallPanel::OnHacked(AActor* Hacker) { Super::OnHacked(Hacker); }
void AFacilityLiftCallPanel::RefreshDisplayData() {}

#undef LOCTEXT_NAMESPACE

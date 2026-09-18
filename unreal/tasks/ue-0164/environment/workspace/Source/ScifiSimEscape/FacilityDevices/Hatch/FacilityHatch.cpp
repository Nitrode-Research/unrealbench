#include "FacilityDevices/Hatch/FacilityHatch.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

AFacilityHatch::AFacilityHatch()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(Root);

	Lid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lid"));
	Lid->SetupAttachment(Hinge);
	Lid->Mobility = EComponentMobility::Movable;
}

void AFacilityHatch::BeginPlay()
{
	Super::BeginPlay();
}

void AFacilityHatch::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SetActorTickEnabled(false);
}

void AFacilityHatch::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();
}

void AFacilityHatch::RefreshDisplayData()
{
}

bool AFacilityHatch::Toggle()
{
	return false;
}

bool AFacilityHatch::IsOpen() const
{
	return false;
}

bool AFacilityHatch::CanOpen() const
{
	return false;
}

void AFacilityHatch::SnapTo(const bool bOpen)
{
	(void)bOpen;
	bMoving = false;
	SetActorTickEnabled(false);
}

void AFacilityHatch::SwingTo(const bool bOpen)
{
	(void)bOpen;
}

FQuat AFacilityHatch::GetPose(const bool bOpen) const
{
	(void)bOpen;
	return FQuat::Identity;
}

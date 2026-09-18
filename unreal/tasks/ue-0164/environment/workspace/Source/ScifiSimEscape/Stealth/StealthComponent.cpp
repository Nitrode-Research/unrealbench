#include "Stealth/StealthComponent.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "FacilityDevices/HidingSpot/FacilityHidingSpot.h"
#include "FacilityDevices/LightZone/FacilityLightZone.h"
#include "GameFramework/Character.h"
#include "ScifiSimEscape.h"

UStealthComponent::UStealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UStealthComponent::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogScifiSimEscape, Log, TEXT("Stealth classification implementation is pending."));
}

void UStealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UStealthComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

EStealthState UStealthComponent::GetStealthState() const
{
	return EStealthState::Exposed;
}

bool UStealthComponent::IsDetectable() const
{
	return false;
}

bool UStealthComponent::IsHidden() const
{
	return false;
}

bool UStealthComponent::IsInDarkness() const
{
	return false;
}

bool UStealthComponent::IsSneaking() const
{
	return false;
}

bool UStealthComponent::IsCrouched() const
{
	return false;
}

float UStealthComponent::GetSpeed() const
{
	return 0.f;
}

FName UStealthComponent::GetLightZoneId() const
{
	return NAME_None;
}

FText UStealthComponent::DescribeStealthState(const EStealthState State)
{
	return FText::GetEmpty();
}

void UStealthComponent::HandleOwnerBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
}

void UStealthComponent::HandleOwnerEndOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
}

void UStealthComponent::Track(AActor* Other, const bool bInside)
{
}

void UStealthComponent::Reconcile()
{
}

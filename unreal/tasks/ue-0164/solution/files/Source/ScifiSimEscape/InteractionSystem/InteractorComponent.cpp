// Fill out your copyright notice in the Description page of Project Settings.


#include "InteractionSystem/InteractorComponent.h"

#include "InteractableComponent.h"
#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ScifiSimEscape.h"

// Sets default values for this component's properties
UInteractorComponent::UInteractorComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}

void UInteractorComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const AActor* Owner = GetOwner())
	{
		CachedCamera = Owner->FindComponentByClass<UCameraComponent>();
	}

	// Before detection starts, because its first pass highlights whatever is already in range.
	CreateHighlightOverlays();
	StartSphereDetection();
}

void UInteractorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSphereDetection();
	Super::EndPlay(EndPlayReason);
}

void UInteractorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UInteractorComponent::StartSphereDetection()
{
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetTimerManager().IsTimerActive(SphereDetectionTimerHandle))
	{
		return;
	}
	
	PerformSphereDetection();
	World->GetTimerManager().SetTimer(
		SphereDetectionTimerHandle,
		this,
		&UInteractorComponent::PerformSphereDetection,
		SphereDetectionInterval,
		true);
}

void UInteractorComponent::StopSphereDetection()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SphereDetectionTimerHandle);
	}

	ClearDetectionState();
}

void UInteractorComponent::PerformSphereDetection()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();

	if (World == nullptr || Owner == nullptr)
	{
		return;
	}

	// Query Params & ignored actors
	const FCollisionObjectQueryParams ObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Owner);

	const FVector SphereCenter = Owner->GetActorLocation();

	TArray<FOverlapResult> OverlapResults;
	World->OverlapMultiByObjectType(
		OverlapResults,
		SphereCenter,
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(SphereRadius),
		QueryParams);

	if (bDrawDebug)
	{
		DrawDebugSphere(World, SphereCenter, SphereRadius, 24, FColor::Green, false, DebugDrawDuration, 0, 1.5f);
	}

	// Every interactable inside the sphere is highlighted. There are no visibility or facing checks.
	TSet<TWeakObjectPtr<UInteractableComponent>> InteractableCandidates;
	for (const FOverlapResult& OverlapResult : OverlapResults)
	{
		const AActor* HitActor = OverlapResult.GetActor();
		UInteractableComponent* Interactable = IsValid(HitActor)
			? HitActor->FindComponentByClass<UInteractableComponent>()
			: nullptr;
		if (Interactable != nullptr)
		{
			InteractableCandidates.Add(Interactable);
		}
	}

	ReconcileHighlightedInteractables(InteractableCandidates);
	DetectedInteractables = InteractableCandidates;

	// A single forward trace from the camera decides which of them is focused.
	SetFocusedInteractable(SelectBestCandidate());
}


UInteractableComponent* UInteractorComponent::SelectBestCandidate() const
{
	// Focus goes to the highlighted interactable whose actor the camera's forward trace lands on.
	const AActor* LookedAtActor = FindLookedAtActor();
	if (LookedAtActor == nullptr)
	{
		return nullptr;
	}

	for (const TWeakObjectPtr<UInteractableComponent>& InteractableComponent : HighlightedInteractables)
	{
		if (!IsValidInteractableComponent(InteractableComponent))
		{
			continue;
		}

		if (InteractableComponent.Get()->GetOwner() == LookedAtActor)
		{
			return InteractableComponent.Get();
		}
	}

	return nullptr;
}

AActor* UInteractorComponent::FindLookedAtActor() const
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (World == nullptr || Owner == nullptr)
	{
		return nullptr;
	}

	const FVector StartLocation = GetViewLocation();
	const FVector EndLocation = StartLocation + GetViewForward() * SphereRadius;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Owner);

	FHitResult HitResult;
	const bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		StartLocation,
		EndLocation,
		ECC_Visibility,
		QueryParams);

	if (bDrawDebug)
	{
		const FVector TraceEnd = bHit ? HitResult.ImpactPoint : EndLocation;
		DrawDebugLine(World, StartLocation, TraceEnd, FColor::Cyan, false, DebugDrawDuration, 0, 1.0f);
		if (bHit)
		{
			DrawDebugPoint(World, HitResult.ImpactPoint, 8.f, FColor::Cyan, false, DebugDrawDuration);
		}
	}

	return bHit ? HitResult.GetActor() : nullptr;
}

void UInteractorComponent::ClearDetectionState()
{
	SetFocusedInteractable(nullptr);

	const TSet<TWeakObjectPtr<UInteractableComponent>> NoInteractables;
	ReconcileHighlightedInteractables(NoInteractables);
	DetectedInteractables.Reset();
}

FVector UInteractorComponent::GetViewLocation() const
{
	if (CachedCamera != nullptr)
	{
		return CachedCamera->GetComponentLocation();
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (const AActor* Owner = GetOwner())
	{
		Owner->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}
	return ViewLocation;
}

FVector UInteractorComponent::GetViewForward() const
{
	if (CachedCamera != nullptr)
	{
		return CachedCamera->GetForwardVector();
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (const AActor* Owner = GetOwner())
	{
		Owner->GetActorEyesViewPoint(ViewLocation, ViewRotation);
		return ViewRotation.Vector();
	}
	return FVector::ForwardVector;
}

void UInteractorComponent::ReconcileHighlightedInteractables(
	const TSet<TWeakObjectPtr<UInteractableComponent>>& CurrentInteractables)
{
	for (auto HighlightedIterator = HighlightedInteractables.CreateIterator(); HighlightedIterator; ++
		 HighlightedIterator)
	{
		const TWeakObjectPtr<UInteractableComponent> HighlightedInteractable = *HighlightedIterator;
		if (IsValidInteractableComponent(HighlightedInteractable) == false)
		{
			HighlightedIterator.RemoveCurrent();
			continue;
		}

		if (CurrentInteractables.Contains(HighlightedInteractable) == false)
		{
			UnhighlightInteractable(HighlightedInteractable.Get());
			HighlightedIterator.RemoveCurrent();
		}
	}

	for (const TWeakObjectPtr<UInteractableComponent>& CurrentInteractable : CurrentInteractables)
	{
		if (IsValidInteractableComponent(CurrentInteractable) == false)
		{
			continue;
		}

		if (HighlightedInteractables.Contains(CurrentInteractable) == false)
		{
			HighlightInteractable(CurrentInteractable.Get());
			HighlightedInteractables.Add(CurrentInteractable);
		}
	}
}

void UInteractorComponent::CreateHighlightOverlays()
{
	if (HighlightMaterial == nullptr)
	{
		UE_LOG(LogInteraction, Warning,
			TEXT("%s on %s has no Highlight Material, so interactables in range are not highlighted."),
			*GetName(), *GetNameSafe(GetOwner()));
		return;
	}

	FLinearColor MaterialColor;
	if (HighlightMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(HighlightColorParameter), MaterialColor) == false)
	{
		UE_LOG(LogInteraction, Warning,
			TEXT("Highlight Material %s has no vector parameter named %s, so both highlight colors draw in the material's own color."),
			*HighlightMaterial->GetName(), *HighlightColorParameter.ToString());
	}

	CanInteractOverlay = UMaterialInstanceDynamic::Create(HighlightMaterial, this);
	CanInteractOverlay->SetVectorParameterValue(HighlightColorParameter, CanInteractColor);

	CannotInteractOverlay = UMaterialInstanceDynamic::Create(HighlightMaterial, this);
	CannotInteractOverlay->SetVectorParameterValue(HighlightColorParameter, CannotInteractColor);
}

void UInteractorComponent::HighlightInteractable(UInteractableComponent* Interactable)
{
	// The color follows the interactable's display data for as long as it stays in range.
	Interactable->OnDisplayDataChanged.AddUniqueDynamic(this, &UInteractorComponent::HandleHighlightedDisplayDataChanged);
	RefreshHighlight(Interactable);

	UE_LOG(LogInteraction, Verbose, TEXT("%s came into range of %s and is highlighted (can interact: %s)"),
		*GetNameSafe(Interactable->GetOwner()), *GetNameSafe(GetOwner()),
		Interactable->GetDisplayData().bCanInteract ? TEXT("true") : TEXT("false"));
}

void UInteractorComponent::UnhighlightInteractable(UInteractableComponent* Interactable)
{
	Interactable->OnDisplayDataChanged.RemoveDynamic(this, &UInteractorComponent::HandleHighlightedDisplayDataChanged);
	Interactable->SetHighlightOverlay(nullptr);

	UE_LOG(LogInteraction, Verbose, TEXT("%s left the range of %s and is no longer highlighted"),
		*GetNameSafe(Interactable->GetOwner()), *GetNameSafe(GetOwner()));
}

void UInteractorComponent::RefreshHighlight(UInteractableComponent* Interactable) const
{
	const bool bCanInteract = Interactable->GetDisplayData().bCanInteract;
	Interactable->SetHighlightOverlay(bCanInteract ? CanInteractOverlay.Get() : CannotInteractOverlay.Get());
}

void UInteractorComponent::HandleHighlightedDisplayDataChanged()
{
	// The delegate does not say which interactable changed. Recoloring the few in range is cheap, and one
	// whose color did not change returns early.
	for (const TWeakObjectPtr<UInteractableComponent>& HighlightedInteractable : HighlightedInteractables)
	{
		if (IsValidInteractableComponent(HighlightedInteractable))
		{
			RefreshHighlight(HighlightedInteractable.Get());
		}
	}
}

void UInteractorComponent::SetFocusedInteractable(UInteractableComponent* NewFocusedInteractable)
{
	UInteractableComponent* PreviousFocusedInteractable = FocusedInteractable.Get();

	// An interactable whose actor was destroyed while focused reads as null here, but it was still
	// focused and has to be reported as lost, or the HUD keeps showing it.
	const bool bHadFocus = FocusedInteractable.IsValid() || FocusedInteractable.IsStale(true);

	const bool bFocusUnchanged = NewFocusedInteractable != nullptr
		? NewFocusedInteractable == PreviousFocusedInteractable
		: bHadFocus == false;
	if (bFocusUnchanged)
	{
		return;
	}

	if (bHadFocus)
	{
		OnInteractionLost.Broadcast();
		UE_LOG(LogTemp, Warning, TEXT("UInteractorComponent:: Focus lost"));
	}

	if (PreviousFocusedInteractable != nullptr)
	{
		PreviousFocusedInteractable->OnDisplayDataChanged.RemoveDynamic(this, &UInteractorComponent::HandleFocusedDisplayDataChanged);
		PreviousFocusedInteractable->OnUnFocused();
	}

	FocusedInteractable = NewFocusedInteractable;

	if (NewFocusedInteractable != nullptr)
	{
		NewFocusedInteractable->OnDisplayDataChanged.AddUniqueDynamic(this, &UInteractorComponent::HandleFocusedDisplayDataChanged);
		NewFocusedInteractable->OnFocused();
		OnInteractableDetected.Broadcast();
	}

	OnFocusedDisplayDataChanged.Broadcast();
}

bool UInteractorComponent::Interact()
{
	if (IsValidInteractableComponent(FocusedInteractable) == false)
	{
		return false;
	}

	FocusedInteractable.Get()->Interact(GetOwner());
	return true;
}

bool UInteractorComponent::Hack()
{
	if (IsValidInteractableComponent(FocusedInteractable) == false)
	{
		return false;
	}

	FocusedInteractable.Get()->Hack(GetOwner());
	return true;
}

UInteractableComponent* UInteractorComponent::GetFocusedInteractable() const
{
	return FocusedInteractable.Get();
}

void UInteractorComponent::HandleFocusedDisplayDataChanged()
{
	OnFocusedDisplayDataChanged.Broadcast();
}

bool UInteractorComponent::IsValidInteractableComponent(
	const TWeakObjectPtr<UInteractableComponent>& InteractableComponent) const
{
	const UInteractableComponent* Interactable = InteractableComponent.Get();
	const AActor* InteractableOwner = Interactable != nullptr ? Interactable->GetOwner() : nullptr;
	return IsValid(Interactable) && IsValid(InteractableOwner) && InteractableOwner->IsActorBeingDestroyed() == false;
}
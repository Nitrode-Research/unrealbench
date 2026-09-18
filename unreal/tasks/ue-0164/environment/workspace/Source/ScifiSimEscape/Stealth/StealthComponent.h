#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StealthComponent.generated.h"

class AFacilityHidingSpot;
class AFacilityLightZone;

/**
 * How visible the player is, in one word. The GDD's stealth rule: line of sight with the lights on
 * detects the player, and breaking line of sight, moving slowly, hiding or being in a dark room
 * prevents it. Line of sight is the watcher's to trace; the rest is this. Ordered by how safe: a
 * hidden player is hidden whatever the lights, a player in the dark is unseen whatever their pace.
 */
UENUM(BlueprintType)
enum class EStealthState : uint8
{
	/** Lit, in the open, upright and moving at speed. A guard with line of sight sees this. */
	Exposed		UMETA(DisplayName = "Exposed"),
	/** Lit and in the open, but crouched or moving slowly. Not detected. */
	Sneaking	UMETA(DisplayName = "Sneaking"),
	/** In no lit lights zone. Not detected. */
	Dark		UMETA(DisplayName = "In the dark"),
	/** Inside a hiding spot. Not detected, and not found. */
	Hidden		UMETA(DisplayName = "Hidden"),
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStealthStateChanged, EStealthState, OldState, EStealthState, NewState);

/**
 * What the player's stealth is right now, on the player's character: the lights zone they stand in
 * and whether it is lit, whether they are crouched or moving slowly, and whether they are in a hiding
 * spot. A guard or the neutral character with line of sight asks IsDetectable and nothing else; the
 * HUD binds OnStealthStateChanged for a line.
 *
 * Zones and spots are volumes, AFacilityLightZone and AFacilityHidingSpot, found through the owner's
 * overlap events and kept as lists. Whether a zone is lit is the facility's answer,
 * FFacilityRules::AreLightsOn over the zone's id, asked every time and never cached, so a breaker
 * flip changes the answer at once. Pace is the owner's ground speed against SneakSpeed, and crouch is
 * the character's. The component ticks to catch pace changes and broadcasts on every change of the
 * summary, which is also logged at Verbose.
 *
 * Not facility state: it is the pawn's, per frame. The route solver reasons about rooms and lights instead.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SCIFISIMESCAPE_API UStealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UStealthComponent();

	/** Fires after the summary changes, with the state it left and the one it reached. */
	UPROPERTY(BlueprintAssignable, Category = "Stealth")
	FOnStealthStateChanged OnStealthStateChanged;

	/** The summary: hidden first, then dark, then sneaking, otherwise exposed. Computed now, not cached. */
	UFUNCTION(BlueprintPure, Category = "Stealth")
	EStealthState GetStealthState() const;

	/** True when a watcher with line of sight sees the player: lit, in the open, and neither crouched nor slow. */
	UFUNCTION(BlueprintPure, Category = "Stealth")
	bool IsDetectable() const;

	/** True while inside a hiding spot, crouched if the spot requires it. */
	UFUNCTION(BlueprintPure, Category = "Stealth")
	bool IsHidden() const;

	/** True while in no lit lights zone, which includes being in no zone at all. */
	UFUNCTION(BlueprintPure, Category = "Stealth")
	bool IsInDarkness() const;

	/** True while crouched or moving no faster than SneakSpeed. Standing still counts. */
	UFUNCTION(BlueprintPure, Category = "Stealth")
	bool IsSneaking() const;

	/** True while the owning character is crouched. */
	UFUNCTION(BlueprintPure, Category = "Stealth")
	bool IsCrouched() const;

	/** The owner's ground speed right now. */
	UFUNCTION(BlueprintPure, Category = "Stealth")
	float GetSpeed() const;

	/** Id of the lights zone the player stands in: a lit one first where several overlap. None outside every zone. */
	UFUNCTION(BlueprintPure, Category = "Stealth")
	FName GetLightZoneId() const;

	/** The state's name as the HUD shows it. */
	UFUNCTION(BlueprintPure, Category = "Stealth")
	static FText DescribeStealthState(EStealthState State);

	//
	// Configuration
	//

	/**
	 * Ground speed at or under which the player moves slowly, in cm/s. Under the template's crouched
	 * walk, since crouching counts on its own; a walk at full speed is well over it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stealth", meta = (ClampMin = "0.0", ForceUnits = "cm/s"))
	float SneakSpeed = 250.f;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	UFUNCTION()
	void HandleOwnerBeginOverlap(AActor* OverlappedActor, AActor* OtherActor);

	UFUNCTION()
	void HandleOwnerEndOverlap(AActor* OverlappedActor, AActor* OtherActor);

	/** Adds or drops the actor from the zone and spot lists, if it is either. */
	void Track(AActor* Other, bool bInside);

	/** Re-reads the summary and broadcasts when it differs from what was last broadcast. */
	void Reconcile();

	/** The zones the owner stands in. Weak: a zone can go away without telling anyone. */
	TArray<TWeakObjectPtr<AFacilityLightZone>> Zones;

	/** The hiding spots the owner stands in. */
	TArray<TWeakObjectPtr<AFacilityHidingSpot>> HidingSpots;

	/** What was last broadcast. Exists only to turn frames into edges; GetStealthState is the answer. */
	EStealthState LastReportedState = EStealthState::Exposed;
};

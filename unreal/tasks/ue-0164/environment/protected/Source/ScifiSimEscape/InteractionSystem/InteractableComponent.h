// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionSystem/Core/InteractionTypes.h"
#include "InteractableComponent.generated.h"

class AActor;
class UMaterialInterface;
class UMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFocusedChanged, bool, bFocused);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractableInteracted, AActor*, InteractingActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractableHacked, AActor*, HackingActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInteractableDisplayDataChanged);

/** A mesh the highlight is drawn on, and the overlay material it drew before the highlight took its overlay slot. */
USTRUCT()
struct FInteractableHighlightedMesh
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> OverlayBeforeHighlight;
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SCIFISIMESCAPE_API UInteractableComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UInteractableComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	/** Takes the highlight off, so meshes that outlive this component do not keep it. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnFocusedChanged OnFocusedChanged;

	/**
	 * @brief Fires when an Interactor presses interact while this is its focused interactable.
	 * The owner decides what the press does, including how to answer a press it refuses.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractableInteracted OnInteracted;

	/**
	 * @brief Fires when an Interactor presses hack while this is its focused interactable.
	 * The owner decides what the handheld does to it, including how to answer a press it refuses.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractableHacked OnHacked;

	/** @brief Fires when the owner changes what this interactable shows the player, on either line. */
	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractableDisplayDataChanged OnDisplayDataChanged;

	/**
		 * @brief Triggered when an InteractorComponent selects this interactable as its best candidate.
		 * Use this to pop up interaction UI or play audio cues. The highlight does not follow focus: the
		 * Interactor draws it on everything in its detection range, see SetHighlightOverlay.
		 */
	void OnFocused();

	/**
	 * @brief Triggered when an InteractorComponent loses focus on this interactable (e.g., player looks away).
	 * Use this to hide UI prompts.
	 */
	void OnUnFocused();

	/**
	 * @brief Draws Overlay over every mesh on the owning actor, swaps it for another, or takes it off when null.
	 *
	 * Called by an Interactor for each interactable in its detection range, so no Blueprint has to do it.
	 * While the highlight is on, it owns the overlay slot of those meshes, and each mesh gets back the overlay
	 * it had before once the highlight comes off. Meshes with the component tag NoHighlight are left alone,
	 * meshes on child actors are not touched, and nothing is drawn while bEnableHighlight is off.
	 * @param Overlay The overlay to draw, or null to take the highlight off.
	 */
	void SetHighlightOverlay(UMaterialInterface* Overlay);

	/** @brief Whether an Interactor currently has the highlight drawn on this interactable. */
	UFUNCTION(BlueprintPure, Category = "Interaction|Highlight")
	bool IsHighlighted() const { return HighlightOverlay != nullptr; }

	/**
	 * @brief Called by an Interactor when its owner presses interact while this interactable is focused.
	 *
	 * Broadcasts OnInteracted even when the display data says a press does nothing, so the owner can
	 * answer a refused press with feedback of its own.
	 * @param InteractingActor The actor that pressed, normally the player's pawn.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void Interact(AActor* InteractingActor);

	/**
	 * @brief Called by an Interactor when its owner presses hack while this interactable is focused.
	 *
	 * Broadcasts OnHacked even when the display data says the handheld does nothing here, so the owner
	 * can answer a refused press with feedback of its own.
	 * @param HackingActor The actor that pressed, normally the player's pawn.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void Hack(AActor* HackingActor);

	/**
	 * @brief Sets what this interactable shows the player for the interact press.
	 *
	 * The owner calls this whenever the answer may have changed. OnDisplayDataChanged only fires when
	 * it actually did, so calling it on every change in the world is cheap.
	 * @param InDisplayText  The action a press performs, or the reason nothing happens.
	 * @param bInCanInteract Whether a press does something right now.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void SetDisplayData(const FText& InDisplayText, bool bInCanInteract);

	/**
	 * @brief Sets what this interactable shows the player for the hack press.
	 *
	 * Same contract as SetDisplayData. An owner the handheld has no business with never calls this, and
	 * the line stays empty.
	 * @param InHackDisplayText The action the handheld performs, or the reason it does nothing.
	 * @param bInCanHack        Whether the hack press does something right now.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void SetHackDisplayData(const FText& InHackDisplayText, bool bInCanHack);

	/** @brief What this interactable currently shows the player. */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	FInteractionDisplayData GetDisplayData() const { return DisplayData; }
	
protected:

	/**
	 * @brief Tracks whether this interactable is currently the primary focus of an Interactor.
	 * Runtime State: If true, this item is the designated target for an interaction input.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	bool bIsFocused;

	/** @brief What this interactable currently shows the player. Written only through SetDisplayData. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Interaction")
	FInteractionDisplayData DisplayData;

	/** Wether to run the debug or not*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Debug")
	bool bEnableDebug = false;

	/**
	 * @brief Whether this interactable shows the highlight while it is in an Interactor's detection range.
	 * Turn it off for things the player only reads and never uses, like light fixtures.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Highlight")
	bool bEnableHighlight = true;

	///
	// Setters
	/// 
	 
	/**
	 * @brief Updates the focused state of this interactable.
	 * 
	 * @param InFocused True if the item is gaining focus, false if losing it.
	 * Typically called by the InteractorComponent during its scoring and reconciliation phase.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	FORCEINLINE void SetIsFocused(bool InFocused) { bIsFocused = InFocused; }

private:

	/** @brief The overlay the highlight currently draws, or null while the highlight is off. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> HighlightOverlay;

	/** @brief The meshes the highlight is drawn on, each with the overlay to give back. Empty while it is off. */
	UPROPERTY(Transient)
	TArray<FInteractableHighlightedMesh> HighlightedMeshes;
};

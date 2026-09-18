#pragma once

#include "CoreMinimal.h"
#include "InteractionTypes.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class EActionTriggerType : uint8
{
	EATT_None UMETA(DisplayName = "None"),
	EATT_Press UMETA(DisplayName = "Press"),
	EATT_Hold UMETA(DisplayName = "Hold"),
};

UENUM(BlueprintType)
enum class EInteractionTriggerState : uint8
{
	EITS_None UMETA(DisplayName = "None"),
	EITS_Started UMETA(DisplayName = "Started"),
	EITS_InProgress UMETA(DisplayName = "In Progress"),
	EITS_Completed UMETA(DisplayName = "Completed"),
	EITS_Cancelled UMETA(DisplayName = "Cancelled")
};

USTRUCT(BlueprintType)
struct FInteractionUI
{
	GENERATED_BODY()
	
public:

	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category = "UI")
	FText DisplayName;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category = "UI")
	FText DisplayDescription;

	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category = "UI")
	TObjectPtr<UTexture2D> Icon;
};

/**
 * What an interactable shows the player right now. Its owner sets it whenever the answer may have
 * changed, and the HUD reads it from the Interactor's focused interactable. There are two lines: what
 * the interact press does, and what the handheld does on the hack press.
 */
USTRUCT(BlueprintType)
struct FInteractionDisplayData
{
	GENERATED_BODY();

public:

	/** The action a press performs, or the reason nothing happens when bCanInteract is false. */
	UPROPERTY(BlueprintReadWrite,Category = "UI")
	FText InteractionDisplayText;

	/**
	 * Whether pressing interact does something right now. Display only: a press still reaches the
	 * interactable's owner, which decides what a refused press does.
	 */
	UPROPERTY(BlueprintReadWrite,Category = "UI")
	bool bCanInteract = true;

	/**
	 * What the handheld does on the hack press, or the reason it does nothing when bCanHack is false.
	 * Empty for anything the handheld has no business with, which is most things; the HUD shows nothing then.
	 */
	UPROPERTY(BlueprintReadWrite,Category = "UI")
	FText HackDisplayText;

	/** Whether the hack press does something right now. Display only, like bCanInteract. */
	UPROPERTY(BlueprintReadWrite,Category = "UI")
	bool bCanHack = false;
};

/** Per-interactor mutable trigger data. Owned by UInteractorComponent. */
USTRUCT(BlueprintType)
struct SCIFISIMESCAPE_API FInteractionTriggerRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Runtime")
	EInteractionTriggerState State = EInteractionTriggerState::EITS_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Runtime")
	float ElapsedTime = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Runtime")
	float Progress = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction|Runtime")
	bool bInputHeld = false;

	void Reset()
	{
		State = EInteractionTriggerState::EITS_None;
		ElapsedTime = 0.f;
		Progress = 0.f;
		bInputHeld = false;
	}
};

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NightSkyEngine/Battle/Misc/Bitflags.h"
#include "ComboDiscovery.generated.h"

class ANightSkyGameState;

// A move the search may use. Input is the single-frame input that begins it.
USTRUCT(BlueprintType)
struct NIGHTSKYENGINE_API FComboMove
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag State;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Bitmask, BitmaskEnum = "/Script/NightSkyEngine.EInputFlags"))
	int32 Input = INP_None;
};

UENUM(BlueprintType)
enum class EComboStepKind : uint8
{
	// The move begins while the previous move is still in progress.
	Cancel,
	// The move begins after the player has returned to idle (primary state of the Standing type).
	Link,
};

USTRUCT(BlueprintType)
struct NIGHTSKYENGINE_API FComboStep
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGameplayTag State;
	// Index into the request's move list.
	UPROPERTY(BlueprintReadOnly)
	int32 MoveIndex = 0;
	UPROPERTY(BlueprintReadOnly)
	EComboStepKind Kind = EComboStepKind::Link;
	// Frames count from 1 for the first frame simulated after the starting situation.
	UPROPERTY(BlueprintReadOnly)
	int32 BeginFrame = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 HitFrame = 0;
	// Health the hit removed from the opponent.
	UPROPERTY(BlueprintReadOnly)
	int32 Damage = 0;
};

USTRUCT(BlueprintType)
struct NIGHTSKYENGINE_API FComboTrial
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TArray<FComboStep> Steps;
	// Sum of the steps' damage.
	UPROPERTY(BlueprintReadOnly)
	int32 TotalDamage = 0;
	// Player 1 input for every frame from 1 through the last step's hit frame.
	UPROPERTY(BlueprintReadOnly)
	TArray<int32> Inputs;
};

USTRUCT(BlueprintType)
struct NIGHTSKYENGINE_API FComboSearchRequest
{
	GENERATED_BODY()

	// Moves the search may use, in ranking order.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FComboMove> Moves;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MaxSteps = 3;
	// The last hit of a route must register on or before this frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MaxFrames = 120;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MaxResults = 10;
	// Battle frame updates the search may run in total, every update counting, before it must stop and report an incomplete result.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MaxSimulatedFrames = 2000000;
	// Input supplied to player 2 on every simulated frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Bitmask, BitmaskEnum = "/Script/NightSkyEngine.EInputFlags"))
	int32 OpponentInput = INP_Neutral;
};

USTRUCT(BlueprintType)
struct NIGHTSKYENGINE_API FComboSearchResult
{
	GENERATED_BODY()

	// Best first.
	UPROPERTY(BlueprintReadOnly)
	TArray<FComboTrial> Trials;
	// True only when the search exhausted the domain, so Trials is exactly the prefix of the full ranking.
	UPROPERTY(BlueprintReadOnly)
	bool bComplete = false;
};

USTRUCT(BlueprintType)
struct NIGHTSKYENGINE_API FComboTrialReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bCompleted = false;
	// First step that did not connect as required; the step count when the failure follows the last step; -1 on success.
	UPROPERTY(BlueprintReadOnly)
	int32 FailedStep = -1;
	// Health the opponent lost during the replay, including to the action in progress at the situation; on failure, up to and including the failing contact.
	UPROPERTY(BlueprintReadOnly)
	int32 ObservedDamage = 0;
};

UCLASS()
class NIGHTSKYENGINE_API UComboDiscovery : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Searches the declared domain from the battle's current situation. Leaves the battle's gameplay state unchanged; LocalFrame counts the updates run.
	UFUNCTION(BlueprintCallable, Category = "Combo Discovery")
	static FComboSearchResult DiscoverCombos(ANightSkyGameState* Battle, const FComboSearchRequest& Request);

	// Replays the trial's inputs from the battle's current situation and judges completion against each step's State and Damage.
	// Leaves the battle's gameplay state unchanged; LocalFrame counts the updates run.
	UFUNCTION(BlueprintCallable, Category = "Combo Discovery")
	static FComboTrialReport ValidateTrial(ANightSkyGameState* Battle, const FComboTrial& Trial, int32 OpponentInput);
};

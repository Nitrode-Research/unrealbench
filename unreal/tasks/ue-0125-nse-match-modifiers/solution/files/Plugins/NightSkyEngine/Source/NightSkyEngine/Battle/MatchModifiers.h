#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MatchModifiers.generated.h"

class ANightSkyGameState;
class APlayerObject;
class FModifierInteger;

UENUM(BlueprintType)
enum class EModifierEvent : uint8
{
	Damage,
	Meter
};
UENUM(BlueprintType)
enum class EModifierOperation : uint8
{
	Add,
	Multiply,
	Cancel,
	ChildDamage,
	ChildMeter
};

USTRUCT(BlueprintType)
struct NIGHTSKYENGINE_API FModifierOperation
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	EModifierOperation Operation = EModifierOperation::Add;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Amount = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Denominator = 1;
	// Child target: 0 victim, 1 attacker.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	bool ToAttacker = false;
};

USTRUCT(BlueprintType)
struct NIGHTSKYENGINE_API FModifierDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FString Identifier;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Revision = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Priority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<FString> ExclusiveGroups;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	EModifierEvent Subscription = EModifierEvent::Damage;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<FModifierOperation> Operations;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	bool MatchAmount = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 RequiredAmount = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Drain = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	bool ConvertDamage = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	bool RestrictMovement = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	bool SuddenDeath = false;
	// Optional real fighter object state created on activation, one per team.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FGameplayTag OwnedAttack;
};

USTRUCT(BlueprintType)
struct NIGHTSKYENGINE_API FModifierInterval
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FString Identifier;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Revision = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Round = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Start = 0;
	// -1 means round end; other durations must be positive.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Duration = -1;
};

USTRUCT(BlueprintType)
struct NIGHTSKYENGINE_API FModifierConfiguration
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<FModifierDefinition> Definitions;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<FModifierInterval> Schedule;
	bool Validate(FString& Reason) const;
	FString Canonical() const;
	static FModifierConfiguration FourRulePreset();
};

USTRUCT(BlueprintType)
struct NIGHTSKYENGINE_API FModifierDisplay
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly)
	FString Identifier;
	UPROPERTY(BlueprintReadOnly)
	FString Name;
	UPROPERTY(BlueprintReadOnly)
	int32 Revision = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 RemainingFrames = 0;
};

USTRUCT()
struct FModifierOwnedObject
{
	GENERATED_BODY()
	UPROPERTY(SaveGame)
	FString Owner;
	UPROPERTY(SaveGame)
	int32 ObjectIndex = -1;
	UPROPERTY(SaveGame)
	int32 Token = 0;
};

struct FModifierPendingEvent
{
	EModifierEvent Kind;
	int32 TargetTeam;
	int32 SourceTeam;
	int32 Amount;
	TArray<int32> Ancestry;
};

USTRUCT(BlueprintType)
struct NIGHTSKYENGINE_API FMatchModifierState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame)
	FModifierConfiguration Configuration;
	UPROPERTY(SaveGame)
	int32 Round = 0;
	UPROPERTY(SaveGame)
	int32 Frame = -1;
	UPROPERTY(SaveGame)
	TArray<int32> Active;
	UPROPERTY(SaveGame)
	TArray<int32> ActiveIntervals;
	UPROPERTY(SaveGame)
	TArray<FString> DeactivateNextStep;
	UPROPERTY(SaveGame)
	TArray<int32> RemovedIntervals;
	UPROPERTY(SaveGame)
	TArray<FModifierOwnedObject> Owned;
	UPROPERTY(SaveGame)
	int32 NextOwnershipToken = 1;
	UPROPERTY(SaveGame)
	int32 HealthDamageSides = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame)
	int32 RoundWinner = 0;
	UPROPERTY(SaveGame)
	int32 P1WinsAtRoundStart = 0;
	UPROPERTY(SaveGame)
	int32 P2WinsAtRoundStart = 0;
	UPROPERTY(SaveGame)
	bool Accepted = true;
	UPROPERTY(SaveGame)
	FString Rejection;
	int32 ActivatingOwnershipToken = 0;
	bool Configure(const FModifierConfiguration& In, FString& Reason);
	void BeginRound(ANightSkyGameState& Game, int32 Number);
	void BeginFrame(ANightSkyGameState& Game);
	void EndRound(ANightSkyGameState& Game);
	void Deactivate(const FString& Identifier);
	bool MovementRestricted() const;
	bool HasSuddenDeath() const;
	TArray<FModifierDisplay> Display() const;
	int32 Damage(ANightSkyGameState& Game, APlayerObject& Victim, APlayerObject& Attacker,
	             int32 Amount);
	int32 GuardDamage(ANightSkyGameState& Game, APlayerObject& Victim, APlayerObject& Attacker, int32 Amount);
	void Meter(ANightSkyGameState& Game, int32 Team, int32 Amount);
	void SpendMeter(ANightSkyGameState& Game, int32 Team, int32 Amount);

  private:
	int32 Dispatch(ANightSkyGameState& Game, EModifierEvent Kind, int32 Team, int32 SourceTeam,
	               FModifierInteger Amount, TArray<int32> Ancestry, APlayerObject* Victim = nullptr,
	               APlayerObject* Attacker = nullptr, bool Guard = false,
	               TArray<FModifierPendingEvent>* DeferredChildren = nullptr);
	void RemoveOwned(ANightSkyGameState& Game, const FString& Identifier);
};

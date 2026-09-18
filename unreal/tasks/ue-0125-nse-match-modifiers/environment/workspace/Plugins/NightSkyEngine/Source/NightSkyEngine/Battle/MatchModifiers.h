#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MatchModifiers.generated.h"

class ANightSkyGameState;
class APlayerObject;

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
struct NIGHTSKYENGINE_API FMatchModifierState
{
	GENERATED_BODY()
	FString Rejection;
	bool Configure(const FModifierConfiguration& Configuration, FString& Reason);
	void Deactivate(const FString& Identifier);
	TArray<FModifierDisplay> Display() const;
};

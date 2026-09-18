#pragma once
#include "CoreMinimal.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "NightSkyEngine/Battle/Script/BattleExtension.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NSE010Fixture.generated.h"

struct FNSE010Battle;

// Authored state script for players and shots. Every knob is ordinary saved
// battle state, so it is carried by the engine's rollback snapshot as well.
UCLASS()
class NIGHTSKYENGINE_API UNSE010Script : public UState
{
	GENERATED_BODY()
public:
	// Identifies the object in the fixture trace.
	UPROPERTY(SaveGame)
	int32 TraceId = 0;
	// Players: horizontal step while Left or Right is held.
	UPROPERTY(SaveGame)
	int32 WalkSpeed = 3000;
	// Players: an A press spawns a shot from the owner's registered shot script.
	UPROPERTY(SaveGame)
	bool bShootOnA = true;
	UPROPERTY(SaveGame)
	bool bAWasDown = false;
	// Shots: travel per frame in the facing direction.
	UPROPERTY(SaveGame)
	int32 Travel = 0;
	// Constant drift applied every frame regardless of input.
	UPROPERTY(SaveGame)
	int32 DriftX = 0;
	UPROPERTY(SaveGame)
	int32 DriftY = 0;
	// When positive, a random horizontal step in [-RandomWalk, RandomWalk] each frame.
	UPROPERTY(SaveGame)
	int32 RandomWalk = 0;
	// Executions so far.
	UPROPERTY(SaveGame)
	int32 Counter = 0;
	// When Counter reaches FreezeAtCounter the owner starts a super freeze.
	UPROPERTY(SaveGame)
	int32 FreezeAtCounter = -1;
	UPROPERTY(SaveGame)
	int32 FreezeDuration = 0;
	UPROPERTY(SaveGame)
	int32 FreezeSelfDuration = 0;
	// Players: when >= 0 and that stored object is active, copy its vertical position.
	UPROPERTY(SaveGame)
	int32 FollowStoredIndex = -1;
	// Shots: hit configuration applied on the first execution.
	UPROPERTY(SaveGame)
	int32 Damage = 100;
	UPROPERTY(SaveGame)
	int32 HitstopFrames = 3;
	UPROPERTY(SaveGame)
	bool bDeactivateOnHit = false;
	UPROPERTY(SaveGame)
	bool bArmed = false;

	virtual void Exec_Implementation() override;
	UFUNCTION()
	void RecordHit();
	UFUNCTION()
	void RecordReceive();
	UFUNCTION()
	void RecordFreezeEnd();

private:
	void Arm();
};

// Battle extension with its own saved counter, called on every battle update.
UCLASS()
class NIGHTSKYENGINE_API UNSE010Extension : public UBattleExtension
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	int32 Ticks = 0;
	// Every LiftPeriod ticks the first player rises by LiftAmount.
	UPROPERTY(SaveGame)
	int32 LiftPeriod = 0;
	UPROPERTY(SaveGame)
	int32 LiftAmount = 0;
	virtual void Exec_Implementation() override;
};

// A second player class, so two lineups can differ by class alone.
UCLASS()
class NIGHTSKYENGINE_API ANSE010AltPlayer : public APlayerObject
{
	GENERATED_BODY()
};

struct NIGHTSKYENGINE_API FNSE010Lineup
{
	int32 TeamCountP1 = 1;
	int32 TeamCountP2 = 1;
	int32 PoolCapacity = 4;
	bool bAltClassP2 = false;
	bool bWithExtension = false;
};

// Neutral battle fixture: a real game state, players and pooled objects in a
// private world. Stepping always runs the engine's own battle frame.
struct NIGHTSKYENGINE_API FNSE010Battle
{
	UWorld* World = nullptr;
	ANightSkyGameState* Game = nullptr;
	// Player 1's team first, then player 2's team.
	TArray<APlayerObject*> Players;
	// Callback log: TraceId on hit or block, -(TraceId + 1) on receive, 5000 + TraceId on freeze end.
	TArray<int32> Trace;

	explicit FNSE010Battle(const FNSE010Lineup& Lineup = FNSE010Lineup());
	~FNSE010Battle();
	FNSE010Battle(const FNSE010Battle&) = delete;
	FNSE010Battle& operator=(const FNSE010Battle&) = delete;

	void Step(int32 Input1 = INP_Neutral, int32 Input2 = INP_Neutral);
	// Returns the battle to the state it had when built, through the engine's
	// own rollback snapshot, and clears Trace. One battle can serve many episodes.
	void Reset();
	UNSE010Script* Script(ABattleObject* Object) const;
	// The registered shot script an owner's spawns are copied from.
	UNSE010Script* ShotTemplate(APlayerObject* Owner) const;
	ABattleObject* Spawn(APlayerObject* Owner, int32 X, int32 Y);
	void Store(APlayerObject* Owner, ABattleObject* Object, int32 Index);
	UNSE010Extension* Extension() const;
	static FNSE010Battle* Find(const ANightSkyGameState* InGame);

private:
	void CreateWorldAndServices(const FNSE010Lineup& Lineup);
	void CreatePlayers(const FNSE010Lineup& Lineup);
	void CreatePool(const FNSE010Lineup& Lineup);
	void ConfigureBattle(const FNSE010Lineup& Lineup);
	UNSE010Script* NewScript(APlayerObject* Owner, FGameplayTag Name) const;
	// Every registered player state and object template, in a fixed order.
	void ForEachRegisteredScript(TFunctionRef<void(USerializableObj*)> Fn) const;

	// Taken at the end of construction, replayed by Reset().
	FRollbackData InitialSnapshot;
	TArray<TArray<uint8>> InitialScriptData;
};

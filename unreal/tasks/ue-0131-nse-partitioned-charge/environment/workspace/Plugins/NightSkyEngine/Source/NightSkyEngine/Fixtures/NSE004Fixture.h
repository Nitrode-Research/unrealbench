#pragma once
#include "CoreMinimal.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NSE004Fixture.generated.h"

struct FNSE004Battle;

// One recorded move start: the battle frame number and the index returned by AddMove.
struct FNSE004Entry
{
	int32 Frame = 0;
	int32 Move = INDEX_NONE;
};

// Bookkeeping for one player. Side 0 is Player, side 1 is Dummy.
struct FNSE004Side
{
	// Every move start of this player, in order.
	TArray<FNSE004Entry> Entries;
	// Steps on which this player's state ran, i.e. steps on which the player was not frozen. Changes only inside Step.
	int32 ClockFrames = 0;
	// Battle frame last counted in ClockFrames; re-pinned by Restore so replayed frames count again.
	int32 LastClockFrame = 0;
	TArray<FGameplayTag> MoveTags;
	// Per move; a move that is not enterable fails the state's CanEnterState check.
	TArray<bool> MoveEnterable;
};

UCLASS()
class NIGHTSKYENGINE_API UNSE004Script : public UState
{
	GENERATED_BODY()
public:
	// Index of the move this state represents, or INDEX_NONE for an idle state.
	int32 MoveIndex = INDEX_NONE;
	// Unfrozen frames a move keeps the player busy, entry frame included. Idle states never end on their own.
	int32 RecoveryFrames = 1;
	FNSE004Battle* Battle = nullptr;
	virtual void Exec_Implementation() override;
	virtual bool CanEnterState_Implementation() override;
};

// Two-player battle in a synthetic world. Stepping always calls the real battle frame.
// Player receives Input1 and Dummy receives Input2; either may have authored moves.
struct NIGHTSKYENGINE_API FNSE004Battle
{
	UWorld* World = nullptr;
	ANightSkyGameState* Game = nullptr;
	APlayerObject* Player = nullptr;
	APlayerObject* Dummy = nullptr;
	FNSE004Side Sides[2];

	FNSE004Battle();
	~FNSE004Battle();
	// Adds a move for Player (Side 0) or Dummy (Side 1). Call before the first Step; at most eight moves per side.
	// Moves added later are checked before moves added earlier. Returns the move index.
	int32 AddMove(int32 RecoveryFrames, const TArray<FChargeCommand>& ChargeCommands,
				  const TArray<FInputConditionList>& InputConditionLists = {}, int32 Side = 0);
	// Makes a move enterable or not through the engine's scripted state condition. Moves start enterable.
	void SetMoveEnterable(int32 Move, bool Enterable, int32 Side = 0);
	void Step(int32 Input1 = INP_Neutral, int32 Input2 = INP_Neutral);
	// The move the player is currently in, or INDEX_NONE while idle.
	int32 CurrentMove(int32 Side = 0) const;
	// Puts a player in hitstop, as a hit would.
	void Hitstop(int32 Frames, int32 Side = 0);
	// Starts a super freeze called by Dummy, so Player is frozen for the duration and Dummy is not.
	void SuperFreeze(int32 Frames);
	// Swaps the players' positions and makes both face their opponent.
	void SwitchSides();
	// Runs the engine's round reset, then restores the fixture's timer settings.
	void ResetRound();
	FRollbackData Save();
	void Restore(FRollbackData& Snapshot);

private:
	void CreateWorldAndServices();
	void CreatePlayers();
	void CreateObjectPool();
	void ConfigureBattle();
	void PinTimers() const;
	UNSE004Script* AddScript(APlayerObject* Owner, FGameplayTag Name, int32 MoveIndex, int32 RecoveryFrames,
							 const TArray<FChargeCommand>& ChargeCommands = {},
							 const TArray<FInputConditionList>& InputConditionLists = {});
};

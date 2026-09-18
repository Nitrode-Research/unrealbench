#pragma once
#include "CoreMinimal.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "NightSkyEngine/Battle/Misc/Bitflags.h"
#include "NightSkyEngine/Battle/Misc/CollisionBox.h"
#include "NSE006Fixture.generated.h"

class ANightSkyGameState;
class APlayerObject;
class ABattleObject;
struct FNSE006Battle;
struct FNSE006Snapshot;

UENUM()
enum class ENSE006Event : uint8
{
	Hit,
	CounterHit,
	Block,
	HitOrBlock,
	Receive
};

UENUM()
enum class ENSE006OnHit : uint8
{
	None,
	DisableHit,
	Deactivate,
	SpawnFollowUp
};

// One recorded callback. Actor is the key of the object whose callback fired. Other is the key of
// its attack target for attacker-side events and of its attack owner for Receive. TargetHealth is the
// health of the fighter on the receiving end (Other, or Actor for Receive) when the callback fired,
// -1 when that end is not a fighter.
struct FNSE006Event
{
	ENSE006Event Kind;
	int32 Actor;
	int32 Other;
	int32 TargetHealth;
};

// One fighter state entry, recorded in the order the engine performs them.
struct FNSE006StateEntry
{
	int32 Player;
	FGameplayTag State;
};

// The only state script the fixture uses. Every fighter state and every object state is one of these.
UCLASS()
class NIGHTSKYENGINE_API UNSE006Script : public UState
{
	GENERATED_BODY()
public:
	// Authored identity of a non-fighter object. Fighters are keyed by player index instead.
	UPROPERTY(SaveGame)
	int32 Key = -1;
	FNSE006Battle* Battle = nullptr;
	virtual void Exec_Implementation() override;
	UFUNCTION()
	void RecordHit();
	UFUNCTION()
	void RecordCounterHit();
	UFUNCTION()
	void RecordBlock();
	UFUNCTION()
	void RecordHitOrBlock();
	UFUNCTION()
	void RecordReceive();
	UFUNCTION()
	void RecordExit();
};

// Two fighters and a pool of four battle objects in one synthetic world. Stepping runs the real battle frame.
struct NIGHTSKYENGINE_API FNSE006Battle
{
	static constexpr int32 PoolSize = 4;
	// P1 stands at -FighterX facing right, P2 at +FighterX facing left, both on the ground.
	static constexpr int32 FighterX = 50000;

	UWorld* World = nullptr;
	ANightSkyGameState* Game = nullptr;
	APlayerObject* P1 = nullptr;
	APlayerObject* P2 = nullptr;
	TArray<FNSE006Event> Trace;
	TArray<FNSE006StateEntry> StateEntries;

	FNSE006Battle();
	~FNSE006Battle();

	// Returns the battle to its freshly constructed shape without creating a new world.
	void Reset();
	void Step(int32 Input1 = INP_Neutral, int32 Input2 = INP_Neutral);
	// Saves the engine's battle snapshot together with the fixture's own tables (pending moves, deferred
	// actions, on-hit actions, guard and invulnerability settings, authored boxes) and returns a handle.
	// Restore loads that snapshot back, keeping the handle valid, and clears Trace and StateEntries.
	int32 Snapshot();
	void Restore(int32 Handle);
	// Activates a pooled object for Owner with the given authored key, facing the owner's way, with a 4x4 hurtbox.
	// Slot forces the lowest free pool slot at or above that index; -1 takes the first free slot.
	ABattleObject* Spawn(APlayerObject* Owner, int32 Key, int32 X, int32 Y = 0, int32 Slot = -1);
	// Makes Attacker an active strike with the fixture's default hit data; Hitstop is written to both the normal
	// and the counter hit data. Does not add hit boxes. Sets the attack flags directly, so unlike EnableHit(true)
	// it does not forget who the attacker has already contacted.
	void Strike(ABattleObject* Attacker, int32 Damage = 100, int32 Priority = 0, int32 AttackLevel = 0,
				int32 Hitstop = 3);
	void Boxes(ABattleObject* Object, const TArray<FCollisionBox>& InBoxes);
	static FCollisionBox Box(int32 Width = 4, int32 Height = 4, EBoxType Type = BOX_Hurt, int32 X = 0, int32 Y = 0);
	// Guard-type armor with a hit budget that covers fighter strikes and/or projectile strikes.
	void Armor(ABattleObject* Target, int32 Hits, bool bStrikes = true, bool bProjectiles = true,
			   bool bDisableIncomingHit = false);
	// Lets the fighter block while holding away from the opponent (P1 holds INP_Left, P2 holds INP_Right).
	void Guard(APlayerObject* Player, bool bEnabled);
	// Applies a one-frame position delta the next time the object's state script executes.
	void Move(ABattleObject* Object, int32 DeltaX, int32 DeltaY = 0);
	// Runs an action from Attacker's hit callback. A follow-up is spawned for Attacker's owner with a 4x4 hit box;
	// SpawnFollowUp fires once, on the first hit callback after it is set, and is then cleared.
	void OnHit(ABattleObject* Attacker, ENSE006OnHit Action, int32 FollowUpKey = -1, int32 FollowUpX = 0,
			   int32 FollowUpY = 0);
	// Runs an arbitrary action once, from Attacker's next hit callback, then forgets it. One action per object;
	// a later call replaces it. It runs after any OnHit action of the same callback.
	void OnHitDo(ABattleObject* Attacker, TFunction<void()> Action);
	// Makes the fighter strike-invulnerable for Frames frames from inside its receive-hit callback; 0 disables.
	void InvulnerableOnReceive(APlayerObject* Player, int32 Frames);
	// Runs Action the next time Object's state script executes. For a fighter entering its reaction that is on
	// entry, after the engine has cleared its flags, hit data and armor and before that frame's collision phase.
	// One action per object; a later call replaces it.
	void Defer(ABattleObject* Object, TFunction<void()> Action);

	UNSE006Script* Script(ABattleObject* Object) const;
	int32 KeyOf(const ABattleObject* Object) const;
	ABattleObject* Find(int32 Key) const;
	// Counts trace entries; -1 matches any actor or other.
	int32 Count(ENSE006Event Kind, int32 Actor = -1, int32 Other = -1) const;

	// Called by the script.
	void Record(ENSE006Event Kind, ABattleObject* Actor);
	void RecordStateExit(ABattleObject* Object);
	void ScriptExec(ABattleObject* Object);

private:
	friend struct FNSE006Snapshot;
	struct FPendingMove
	{
		int32 X = 0;
		int32 Y = 0;
	};
	struct FOnHitAction
	{
		ENSE006OnHit Action = ENSE006OnHit::None;
		int32 FollowUpKey = -1;
		int32 X = 0;
		int32 Y = 0;
	};
	TMap<int32, FPendingMove> PendingMoves;
	TMap<int32, FOnHitAction> OnHitActions;
	TMap<int32, TFunction<void()>> OnHitCustom;
	TMap<int32, TFunction<void()>> Deferred;
	bool bGuard[2] = {false, false};
	int32 InvulnerableFrames[2] = {0, 0};
	// Set by the exit recorder, consumed by the next script execution, which is the entry of the new state.
	bool bPendingEntry[2] = {false, false};
	TArray<TSharedPtr<FNSE006Snapshot>> Snapshots;

	void CreateWorldAndServices();
	void CreatePlayers();
	void ConfigureBattle();
	void CreatePool();
	void ResetPlayer(APlayerObject* Player);
	void Recorders(ABattleObject* Object) const;
	UNSE006Script* NewScript(UObject* Outer, FGameplayTag Name);
};

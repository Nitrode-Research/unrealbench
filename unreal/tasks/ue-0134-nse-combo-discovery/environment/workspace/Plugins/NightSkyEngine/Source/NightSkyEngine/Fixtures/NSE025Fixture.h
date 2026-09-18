#pragma once
#include "CoreMinimal.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/BattleObject.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "NSE025Fixture.generated.h"

class APlayerObject;

// Authored parameters of one attack. Startup, Active, Recovery and CancelFrom are action frames (the state's
// ActionTime, which does not advance during hitstop); Hitstun and Hitstop are battle frames; distances are battle units.
struct NIGHTSKYENGINE_API FNSE025MoveSpec
{
	// Single-frame input that begins the move. Give every move its own button.
	int32 Input = INP_A;
	int32 Startup = 4;
	int32 Active = 2;
	int32 Recovery = 6;
	// The hitbox spans this far in front of the attacker while active.
	int32 Reach = 160000;
	int32 Damage = 300;
	int32 Hitstun = 12;
	int32 Hitstop = 4;
	int32 Pushback = 20000;
	int32 InitialProration = 100;
	int32 ForcedProration = 90;
	int32 MinimumDamagePercent = 0;
	int32 MaxChain = -1;
	bool bReverseBeat = true;
	bool bFollowup = false;
	EStateType Type = EStateType::NormalAttack;
	// Indices of moves this move may chain into once it has hit.
	TArray<int32> ChainCancels;
	// Action frame on which the chain options become available; 0 means from the first frame.
	int32 CancelFrom = 0;
	// The move's hit action against grounded and airborne defenders alike; the air actions launch.
	EHitAction AirHitAction = HACT_GroundNormal;
	// Launch arc: vertical speed and gravity of the launched defender.
	int32 AirPushbackY = 0;
	int32 Gravity = 0;
	// Launch stun (untech); 0 means Hitstun.
	int32 Untech = 0;
	// Action frame on which the hitbox re-arms and may hit again; 0 never re-arms.
	int32 MultiHit = 0;
};

// Idle stance. Enables the given state types every frame.
UCLASS()
class NIGHTSKYENGINE_API UNSE025Stand : public UState
{
	GENERATED_BODY()
public:
	int32 Enable = ENB_Standing;
	FGameplayTag Cel;
	virtual void Exec_Implementation() override;
};

// One authored attack: startup, active hitbox, recovery, then back to the stand state.
UCLASS()
class NIGHTSKYENGINE_API UNSE025Attack : public UState
{
	GENERATED_BODY()
public:
	FNSE025MoveSpec Spec;
	FGameplayTag IdleCel;
	FGameplayTag ActiveCel;
	// Entry work runs once per entry; the engine resets this on every state change and rolls it back.
	UPROPERTY(SaveGame)
	int32 Entered = 0;
	virtual void Exec_Implementation() override;
};

// Hitstun or blockstun. Applies the received hit or guard values on entry.
UCLASS()
class NIGHTSKYENGINE_API UNSE025Reaction : public UState
{
	GENERATED_BODY()
public:
	bool bGuard = false;
	FGameplayTag Cel;
	UPROPERTY(SaveGame)
	int32 Entered = 0;
	virtual void Exec_Implementation() override;
};

// Air tech, entered on the tech button while the engine's ENB_Tech is enabled (untech expired,
// airborne). Falls under its own momentum and returns to the stand state on landing.
UCLASS()
class NIGHTSKYENGINE_API UNSE025Tech : public UState
{
	GENERATED_BODY()
public:
	FGameplayTag Cel;
	virtual void Exec_Implementation() override;
};

// Neutral two-fighter battle for authoring attacks. Stepping always runs the real battle frame.
struct NIGHTSKYENGINE_API FNSE025Battle
{
	UWorld* World = nullptr;
	ANightSkyGameState* Game = nullptr;
	APlayerObject* Attacker = nullptr;
	APlayerObject* Defender = nullptr;
	TArray<FNSE025MoveSpec> Moves;

	FNSE025Battle();
	~FNSE025Battle();
	// Adds an attack for the attacker. The i-th added move is the state MoveTag(i). Call before Start.
	void AddMove(const FNSE025MoveSpec& Spec);
	// Rewrites an added move's parameters in place. Legal after Start while the attacker is idle.
	void SetMove(int32 Index, const FNSE025MoveSpec& Spec);
	// Places the fighters on the ground facing each other, runs one neutral frame and remembers the situation.
	// The walls are the stage bounds at +-3200000 and a fighter stops 85000 inside them (the corner is +-3115000).
	// Calling it again first returns to the previous situation, so a reused battle starts clean.
	// The attacker's CanReverseBeat as set when this is called becomes part of the situation.
	void Start(int32 AttackerX = -200000, int32 DefenderX = -50000);
	void Step(int32 Input1 = INP_Neutral, int32 Input2 = INP_Neutral);
	// Returns the battle to the situation remembered by Start.
	void Reset();
	// Frames advanced since Start.
	int32 Frame() const;
	// Raw input that makes the defender hold back, given its facing.
	int32 GuardInput() const;
	// Raw input that makes the defender tech on the first frame it can (the tech button, held).
	static int32 TechInput();
	// True at the end of the frame on which the attacker began the given move.
	bool AttackerBegan(int32 MoveIndex) const;
	// True while the defender is stunned by a hit.
	bool DefenderInHitstun() const;
	static FGameplayTag MoveTag(int32 Index);
	static FGameplayTag TechTag();
	static constexpr int32 MaxMoves = 8;

private:
	TArray<UNSE025Attack*> AttackStates;
	FRollbackData StartState;
	int32 StartFrame = 0;
	bool bStarted = false;
	void CreateWorldAndServices();
	APlayerObject* CreatePlayer(int32 PlayerIndex, int32 ObjNumber);
	void ConfigureBattle();
	void AddStand(APlayerObject* Player, FGameplayTag Name, int32 Enable, FGameplayTag Cel);
	void AddReaction(APlayerObject* Player, FGameplayTag Name, EStateType Type, bool bGuard, FGameplayTag Cel);
	void AddTech(APlayerObject* Player, FGameplayTag Name, FGameplayTag Cel);
	void AddCel(APlayerObject* Player, FGameplayTag Cel, const TArray<FCollisionBox>& Boxes);
};

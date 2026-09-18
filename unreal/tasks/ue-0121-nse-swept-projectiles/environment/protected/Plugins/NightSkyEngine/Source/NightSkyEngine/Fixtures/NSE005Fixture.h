#pragma once
#include "CoreMinimal.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "NSE005Fixture.generated.h"

struct FNSE005Battle;

UENUM()
enum class ENSE005ContactAction : uint8
{
	None,
	DeactivateProjectile,
	DisableHit,
	MakeTargetInvulnerable
};

UCLASS()
class NIGHTSKYENGINE_API UNSE005Script : public UState
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	int32 PendingMoveX = 0;
	UPROPERTY(SaveGame)
	int32 PendingMoveY = 0;
	UPROPERTY(SaveGame)
	bool bTeleportPending = false;
	UPROPERTY(SaveGame)
	int32 TeleportDestinationX = 0;
	UPROPERTY(SaveGame)
	int32 TeleportDestinationY = 0;
	UPROPERTY(SaveGame)
	ENSE005ContactAction OnContactAction = ENSE005ContactAction::None;
	UPROPERTY()
	ABattleObject* ChangeTarget = nullptr;
	TArray<int32>* Trace = nullptr;
	FNSE005Battle* Battle = nullptr;
	UPROPERTY(SaveGame)
	bool bSpawnPending = false;
	UPROPERTY(SaveGame)
	int32 SpawnX = 0;
	UPROPERTY(SaveGame)
	bool bSpawnTargetPending = false;
	virtual void Exec_Implementation() override;
	UFUNCTION()
	void RecordHit();
	UFUNCTION()
	void RecordBlock();
	UFUNCTION()
	void RecordReceive();
};

// Authored state and asset fixture. Stepping always calls the real battle frame.
struct NIGHTSKYENGINE_API FNSE005Battle
{
	UWorld* World = nullptr;
	ANightSkyGameState* Game = nullptr;
	ABattleObject* Projectile = nullptr;
	TArray<APlayerObject*> Targets;
	TArray<int32> Trace;
	FNSE005Battle(int32 TargetCount = 1, bool bConfigureSweep = true);
	~FNSE005Battle();
	UNSE005Script* Script(ABattleObject* Object) const;
	void Boxes(ABattleObject* Object, const TArray<FCollisionBox>& InBoxes);
	void Move(ABattleObject* Object, int32 PendingMoveX, int32 PendingMoveY = 0);
	void Teleport(ABattleObject* Object, int32 X, int32 Y, int32 MoveAfterTeleportX = 0);
	void Step(int32 Input1 = INP_Neutral, int32 Input2 = INP_Neutral);
	ABattleObject* SpawnTarget(int32 X, int32 Y = 100);
	void Spawn(int32 X = 0, int32 Y = 100, bool Sweep = true, bool Piercing = false, int32 Limit = 1,
			   bool bConfigureSweep = true);
	static FCollisionBox Box(int32 Width = 4, int32 Height = 4, EBoxType Type = BOX_Hurt, int32 X = 0,
							 int32 Y = 0);

private:
	void CreateWorldAndServices(int32 TargetCount);
	void CreatePlayers(int32 TargetCount);
	void ConfigureBattle(int32 TargetCount);
	void CreateProjectilePool(bool bConfigureSweep);
};

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "ReplayFixture.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

NIGHTSKYENGINE_API bool ReplayFixtureConfigurationMatches(const FBattleData &Actual, const FBattleData &Expected);

// Fixed deterministic training content, identical in starter and reference.
UCLASS()

class NIGHTSKYENGINE_API UReplayFixtureGameInstance : public UNightSkyGameInstance
{
    GENERATED_BODY()
  public:
    virtual void Init() override
    {
        UGameInstance::Init();
    }
    virtual void TravelToVSInfo() const override;
    void UseRoomFixtureBattle(class AReplayFixtureBattle *Battle);
    void QueueConstructedRoomStage(class AReplayFixtureBattle *Battle);
    void CompleteRoomFixtureBinding();
    class AReplayFixtureBattle *AssociatedRoomFixtureBattle() const;
    FString DescribeRoomFixtureSetup() const;

  private:
    bool RoomFixtureStageLifecycle = false;
    bool RoomFixtureBindingComplete = false;
    uint64 RoomFixtureSetupGeneration = 0;
    TWeakObjectPtr<class AReplayFixtureBattle> AssociatedRoomBattle;
    TWeakObjectPtr<class AReplayFixtureBattle> ConstructedRoomStage;
};

// Native substitute for the authored room fixture's loading-screen content.
UCLASS()
class NIGHTSKYENGINE_API AReplayFixtureRoomGameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    AReplayFixtureRoomGameMode();
    virtual void StartPlay() override;
};

UCLASS()

class NIGHTSKYENGINE_API UReplayFixtureState : public UState
{
    GENERATED_BODY()
  public:
    virtual void Exec_Implementation() override;
};

UCLASS()

class NIGHTSKYENGINE_API AReplayFixtureFighter : public APlayerObject
{
    GENERATED_BODY()
  public:
    AReplayFixtureFighter();
    void EmitAttackSound();
    UPROPERTY()
    class USoundWaveProcedural *Tone = nullptr;
    virtual void BeginPlay() override;
    virtual void UpdateVisualsNoRollback() override;

  protected:
    float FixtureTorsoWidth = 38.f;

  private:
    void InitializeFixtureMaterials();
    void UpdateFixturePose();
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> FixtureParts;
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInstanceDynamic>> FixtureMaterials;
    int32 FixtureTintPlayerIndex = INDEX_NONE;
};

UCLASS()

class NIGHTSKYENGINE_API AReplayFixtureHeavyFighter : public AReplayFixtureFighter
{
    GENERATED_BODY()
  public:
    AReplayFixtureHeavyFighter();
};

UCLASS()

class NIGHTSKYENGINE_API UReplayFixtureProjectile : public UState
{
    GENERATED_BODY()
  public:
    virtual void Exec_Implementation() override;
};
UCLASS()

class NIGHTSKYENGINE_API AReplayFixtureProjectileActor : public ABattleObject
{
    GENERATED_BODY()
  public:
    AReplayFixtureProjectileActor();
};
UCLASS()

class NIGHTSKYENGINE_API AReplayFixtureBattle : public ANightSkyGameState
{
    GENERATED_BODY()
  public:
    DECLARE_MULTICAST_DELEGATE_OneParam(FSoundStarted, const AReplayFixtureBattle *);
    static FSoundStarted SoundStarted;
    AReplayFixtureBattle();
    virtual void BeginPlay() override;
    const FBattleData &FixtureInitialConfiguration() const { return InitialConfiguration; }
    bool FixtureInitialTraining() const { return InitialTraining; }
    bool DidCompleteFixtureInitialization() const
    {
        return FixtureInitializationComplete;
    }

  private:
    UPROPERTY()
    FBattleData InitialConfiguration;
    UPROPERTY()
    TArray<TObjectPtr<UObject>> InitialAssets;
    bool InitialTraining = false;
    bool FixtureInitializationComplete = false;
};

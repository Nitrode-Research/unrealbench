#pragma once
#include "CoreMinimal.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "NightSkyEngine/Data/PrimaryStageData.h"
#include "GameFramework/GameModeBase.h"
#include "ReplayFixture.generated.h"

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
};

UCLASS()
class NIGHTSKYENGINE_API UReplayFixtureState : public UState
{
    GENERATED_BODY()
  public:
    virtual void Exec_Implementation() override;
};

UCLASS()
class NIGHTSKYENGINE_API UReplayFixtureProjectile : public UState
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
    class USoundWaveProcedural* Tone = nullptr;
    virtual void BeginPlay() override;
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
    AReplayFixtureBattle();
    virtual void MatchInit() override;
    virtual void ProcessEvent(UFunction* Function, void* Parameters) override;
    TArray<FIntPoint> PresentedHealth;
    virtual void BeginPlay() override;
};

UCLASS()
class NIGHTSKYENGINE_API UReplayFixtureCharaData : public UPrimaryCharaData
{
    GENERATED_BODY()
  public:
    UReplayFixtureCharaData();
};

UCLASS()
class NIGHTSKYENGINE_API UReplayFixtureStageData : public UPrimaryStageData
{
    GENERATED_BODY()
  public:
    UReplayFixtureStageData();
};

UCLASS()
class NIGHTSKYENGINE_API AReplayFixtureGameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    AReplayFixtureGameMode();
};

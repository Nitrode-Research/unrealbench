#pragma once
#include "CoreMinimal.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "RelayFixture.generated.h"

// Fixed deterministic training content, identical in starter and reference.
UCLASS()
class NIGHTSKYENGINE_API URelayFixtureGameInstance : public UNightSkyGameInstance
{
    GENERATED_BODY()
  public:
    virtual void Init() override;
};

UCLASS()
class NIGHTSKYENGINE_API URelayFixtureState : public UState
{
    GENERATED_BODY()
  public:
    virtual void Exec_Implementation() override;
};

UCLASS()
class NIGHTSKYENGINE_API ARelayFixtureFighter : public APlayerObject
{
    GENERATED_BODY()
  public:
    ARelayFixtureFighter();
    UPROPERTY(EditAnywhere)
    int32 SampleThrowRange = 400000;
    UPROPERTY(EditAnywhere)
    int32 SampleHitstun = 18;
    UPROPERTY(EditAnywhere)
    int32 SampleProjectileHeight = 0;
    virtual void BeginPlay() override;
    virtual void UpdateVisuals() override;
};

UCLASS()
class NIGHTSKYENGINE_API ARelayFixtureBattle : public ANightSkyGameState
{
    GENERATED_BODY()
  public:
    ARelayFixtureBattle();
    virtual void BeginPlay() override;
};

// Analytic sample move content. No relay scheduling or role logic belongs here.
UCLASS()
class NIGHTSKYENGINE_API URelayFixtureMove : public UState
{
    GENERATED_BODY()
  public:
    virtual void Init_Implementation() override;
    virtual void Exec_Implementation() override;
};
UCLASS()
class NIGHTSKYENGINE_API URelayFixtureProjectile : public UState
{
    GENERATED_BODY()
  public:
    virtual void Exec_Implementation() override;
};
UCLASS()
class NIGHTSKYENGINE_API URelayFixtureReaction : public UState
{
    GENERATED_BODY()
  public:
    virtual void Init_Implementation() override;
    virtual void Exec_Implementation() override;
};
UCLASS()
class NIGHTSKYENGINE_API URelayFixtureThrow : public UState
{
    GENERATED_BODY()
  public:
    virtual void Exec_Implementation() override;
};

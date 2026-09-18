#pragma once
#include "RelayFixture.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "NightSkyEngine/Data/PrimaryStageData.h"
#include "GameFramework/GameModeBase.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "RelaySample.generated.h"
UCLASS()
class NIGHTSKYENGINE_API ARelaySampleHeavy : public ARelayFixtureFighter
{
    GENERATED_BODY()
  public:
    ARelaySampleHeavy()
    {
        MaxHealth = 12000;
    }
};
UCLASS()
class NIGHTSKYENGINE_API ARelaySampleLight : public ARelayFixtureFighter
{
    GENERATED_BODY()
  public:
    ARelaySampleLight()
    {
        MaxHealth = 9000;
    }
};
UCLASS()
class NIGHTSKYENGINE_API ARelayPeerGlass : public ARelayFixtureFighter
{
    GENERATED_BODY()
  public:
    ARelayPeerGlass()
    {
        MaxHealth = 500;
    }
};
UCLASS()
class NIGHTSKYENGINE_API URelayPeerThree : public UPrimaryCharaData
{
    GENERATED_BODY()
  public:
    URelayPeerThree();
};
UCLASS()
class NIGHTSKYENGINE_API URelaySampleOne : public UPrimaryCharaData
{
    GENERATED_BODY()
  public:
    URelaySampleOne();
};
UCLASS()
class NIGHTSKYENGINE_API URelaySampleTwo : public UPrimaryCharaData
{
    GENERATED_BODY()
  public:
    URelaySampleTwo();
};
UCLASS()
class NIGHTSKYENGINE_API URelaySampleThree : public UPrimaryCharaData
{
    GENERATED_BODY()
  public:
    URelaySampleThree();
};
UCLASS()
class NIGHTSKYENGINE_API ARelaySampleGameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    ARelaySampleGameMode();
    virtual void BeginPlay() override;
};

UCLASS()
class NIGHTSKYENGINE_API ARelaySampleController : public ANightSkyPlayerController
{
    GENERATED_BODY()
  public:
    virtual void SetupInputComponent() override;
    void OneDown()
    {
        RelayButton(1, true);
    }
    void OneUp()
    {
        RelayButton(1, false);
    }
    void TwoDown()
    {
        RelayButton(2, true);
    }
    void TwoUp()
    {
        RelayButton(2, false);
    }
    void ThreeDown()
    {
        RelayButton(3, true);
    }
    void ThreeUp()
    {
        RelayButton(3, false);
    }
};

UCLASS()
class NIGHTSKYENGINE_API URelaySampleStage : public UPrimaryStageData
{
    GENERATED_BODY()
  public:
    URelaySampleStage();
};

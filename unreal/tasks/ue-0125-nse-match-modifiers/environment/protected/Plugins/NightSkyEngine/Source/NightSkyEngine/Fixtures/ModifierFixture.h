#pragma once
#include "CoreMinimal.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "ModifierFixture.generated.h"

// Fixed deterministic training content, identical in starter and reference.
UCLASS()
class NIGHTSKYENGINE_API UModifierFixtureGameInstance : public UNightSkyGameInstance
{
	GENERATED_BODY()
  public:
	virtual void Init() override;
};

UCLASS()
class NIGHTSKYENGINE_API UModifierFixtureState : public UState
{
	GENERATED_BODY()
  public:
	virtual void Init_Implementation() override;
	virtual void Exec_Implementation() override;
};

UCLASS()
class NIGHTSKYENGINE_API AModifierFixtureFighter : public APlayerObject
{
	GENERATED_BODY()
  public:
	AModifierFixtureFighter();
	virtual void BeginPlay() override;
};

UCLASS()
class NIGHTSKYENGINE_API AModifierFixtureBattle : public ANightSkyGameState
{
	GENERATED_BODY()
  public:
	AModifierFixtureBattle();
	virtual void BeginPlay() override;
};

NIGHTSKYENGINE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ModifierFixture_ProjectileA);
NIGHTSKYENGINE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ModifierFixture_ProjectileB);
UCLASS()
class NIGHTSKYENGINE_API UModifierProjectileState : public UState
{
	GENERATED_BODY()
  public:
	virtual void Exec_Implementation() override;
};
UCLASS()
class NIGHTSKYENGINE_API AModifierFixtureProjectile : public ABattleObject
{
	GENERATED_BODY()
  public:
	AModifierFixtureProjectile();
};

// Native class default has a stable /Script reference across replay processes.
UCLASS()
class NIGHTSKYENGINE_API UModifierFixtureCharaData : public UPrimaryCharaData
{
	GENERATED_BODY()
  public:
	UModifierFixtureCharaData();
};

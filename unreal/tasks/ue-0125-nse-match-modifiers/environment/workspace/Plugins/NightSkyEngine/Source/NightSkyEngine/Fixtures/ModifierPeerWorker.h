#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "GameFramework/GameModeBase.h"
#include "ModifierFixture.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "ModifierPeerWorker.generated.h"
UCLASS()
class NIGHTSKYENGINE_API AModifierSampleController : public ANightSkyPlayerController
{
	GENERATED_BODY()
  public:
	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	UFUNCTION(BlueprintCallable)
	void ShowModifierSetup();

  private:
	UPROPERTY()
	TObjectPtr<class UModifierSetupWidget> ModifierSetup;
};
UCLASS()
class NIGHTSKYENGINE_API AModifierPeerGameMode : public AGameModeBase
{
	GENERATED_BODY()
  public:
	AModifierPeerGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options,
	                      FString& ErrorMessage) override;
};

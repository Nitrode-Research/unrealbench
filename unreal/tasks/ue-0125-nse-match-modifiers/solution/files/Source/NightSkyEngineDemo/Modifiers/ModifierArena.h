#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/SaveGame.h"
#include "Blueprint/UserWidget.h"
#include "NightSkyEngine/Fixtures/ModifierFixture.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "NightSkyEngine/Battle/Script/BattleExtension.h"
#include "NightSkyEngine/Data/PrimaryStageData.h"
#include "ModifierArena.generated.h"
class UModifierSetupWidget;
class USkeletalMeshComponent;
class UTextRenderComponent;
class UStaticMeshComponent;
class UAnimationAsset;
class UMaterialInterface;
UCLASS()
class UModifierArenaProfile : public USaveGame
{
 GENERATED_BODY()
public:
 UPROPERTY(SaveGame) FModifierConfiguration Authoring;
 UPROPERTY(SaveGame) FModifierConfiguration Selected;
 UPROPERTY(SaveGame) FString LastReplay;
};
UCLASS()
class UModifierArenaInstance : public UModifierFixtureGameInstance
{
 GENERATED_BODY()
public:
 virtual void Init() override;
 UPROPERTY() TObjectPtr<UModifierArenaProfile> Profile;
 UPROPERTY(BlueprintReadOnly) FString Notice;
 bool Online=false, ReplayTravel=false, StartOnTravel=false;
 void Prepare();
 void Persist();
 void SaveArenaReplay();
 void WatchArenaReplay();
 void StartNetwork(const FModifierConfiguration& C,bool Host,const FString& Address);
 void ReturnLocal();
};
UCLASS()
class AModifierArenaFighter : public AModifierFixtureFighter
{ GENERATED_BODY() public: AModifierArenaFighter(); };
UCLASS()
class UModifierArenaCharacter : public UPrimaryCharaData
{ GENERATED_BODY() public: UModifierArenaCharacter(); };
UCLASS()
class UModifierArenaStage : public UPrimaryStageData
{ GENERATED_BODY() public: UModifierArenaStage(); };
UCLASS()
class UModifierArenaRound : public UBattleExtension
{ GENERATED_BODY() public: UModifierArenaRound(); virtual void Exec_Implementation() override; };
UCLASS()
class AModifierArenaBattle : public AModifierFixtureBattle
{ GENERATED_BODY() public: AModifierArenaBattle(); virtual void BeginPlay() override; };
UCLASS()
class AModifierArenaGameMode : public AGameModeBase
{
 GENERATED_BODY()
public:
 AModifierArenaGameMode();
 virtual void InitGame(const FString& MapName,const FString& Options,FString& Error) override;
};
UCLASS()
class AModifierArenaController : public ANightSkyPlayerController
{
 GENERATED_BODY()
public:
 AModifierArenaController();
 virtual void BeginPlay() override;
 virtual void SetupInputComponent() override;
 virtual void Tick(float Delta) override;
 virtual void PostRematch() override;
 UFUNCTION(BlueprintCallable) void ToggleSetup();
 UFUNCTION(BlueprintCallable) void StartSelectedMatch();
 UFUNCTION(BlueprintCallable) void HostSelectedMatch();
 UFUNCTION(BlueprintCallable) void JoinSelectedMatch(const FString& Address);
 UFUNCTION(BlueprintCallable) void TogglePause();
 UFUNCTION(BlueprintCallable) void RestartMatch();
 UFUNCTION(BlueprintCallable) void SaveMatchReplay();
 UFUNCTION(BlueprintCallable) void WatchMatchReplay();
 UFUNCTION(BlueprintCallable) void SetInputWord(int32 Word,bool Second=false);
 void InputBit(int32 Mask,bool Held,bool Second);
 void InitializeLocalUI();
 UPROPERTY() TObjectPtr<UModifierSetupWidget> Setup;
 UPROPERTY() TObjectPtr<UUserWidget> HUD;
 int32 SecondWord=0;
 bool PairRematch=false,AgreementSent=false,NetworkStarted=false;
};
UCLASS()
class UModifierArenaHUD : public UUserWidget
{ GENERATED_BODY() protected: virtual TSharedRef<SWidget> RebuildWidget() override; };
UCLASS()
class AModifierArenaPresentation : public AActor
{
 GENERATED_BODY()
public:
 AModifierArenaPresentation();
 virtual void Tick(float Delta) override;
private:
 void InitializeFighters(ANightSkyGameState* Battle);
 UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> Bodies;
 UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> Labels;
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Projectiles;
 UPROPERTY() TObjectPtr<USkeletalMesh> FighterMesh;
 UPROPERTY() TObjectPtr<UMaterialInterface> TeamMaterials[2];
 UPROPERTY() TObjectPtr<UAnimationAsset> Idle;
 UPROPERTY() TObjectPtr<UAnimationAsset> Walk;
 UPROPERTY() TObjectPtr<UAnimationAsset> Strike;
 UPROPERTY() TObjectPtr<UAnimationAsset> Followup;
 UPROPERTY() TObjectPtr<UAnimationAsset> Block;
 UPROPERTY() TObjectPtr<UAnimationAsset> Hit;
 UPROPERTY() TObjectPtr<UAnimationAsset> Knockout;
};

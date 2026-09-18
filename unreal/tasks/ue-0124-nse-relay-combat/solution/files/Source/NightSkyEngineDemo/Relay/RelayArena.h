#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/GameModeBase.h"
#include "NightSkyEngine/Fixtures/RelaySample.h"
#include "RelayArena.generated.h"

class USkeletalMeshComponent;
class UTextRenderComponent;
class UAnimationAsset;
class UStaticMeshComponent;
class UMaterialInterface;

/** Local front end. Combat remains in NightSkyGameState and the replay/GGPO runners. */
UCLASS()
class URelayArenaSession : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    UPROPERTY(BlueprintReadOnly) TArray<int32> Roster;
    UPROPERTY(BlueprintReadOnly) bool SelectionOpen = true;
    UPROPERTY(BlueprintReadOnly) bool Training = true;
    UPROPERTY(BlueprintReadOnly) bool Sparring = false;
    UPROPERTY(BlueprintReadOnly) FString Message;
    UPROPERTY(BlueprintReadOnly) FString SavedReplay;
    bool ReplayTravel = false;
    bool StartOnTravel = false;
    void ApplyRoster();
    void StartMatch();
    void SaveReplay();
    void WatchReplay();
    void CycleSlot(int32 Index);
    FString SlotLabel(int32 Index) const;
};

UCLASS()
class URelayArenaStage : public UPrimaryStageData
{
    GENERATED_BODY()
public:
    URelayArenaStage();
};

UCLASS()
class ARelayArenaGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ARelayArenaGameMode();
    virtual void InitGame(const FString& MapName, const FString& Options, FString& Error) override;
    virtual void BeginPlay() override;
};

UCLASS()
class ARelayArenaController : public ARelaySampleController
{
    GENERATED_BODY()
public:
    virtual void SetupInputComponent() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    UFUNCTION(BlueprintCallable) void ToggleSelection();
    UFUNCTION(BlueprintCallable) void StartSelectedMatch();
    UFUNCTION(BlueprintCallable) void TogglePause();
    UFUNCTION(BlueprintCallable) void SaveMatchReplay();
    UFUNCTION(BlueprintCallable) void WatchMatchReplay();
    UFUNCTION(BlueprintCallable) void RestartMatch();
    virtual void PostRematch() override;
    void SecondInput(int32 Mask, bool Held);
    void SetMenuInput(bool Open);
    UPROPERTY() TObjectPtr<UUserWidget> SelectionWidget;
    int32 ManualSecondInput = 0;
    int32 RematchFrames = 0;
};

UCLASS()
class URelayArenaMenu : public UUserWidget
{
    GENERATED_BODY()
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};

/** Read-only visual projection of the deterministic fighter state, including rollback. */
UCLASS()
class ARelayArenaPresentation : public AActor
{
    GENERATED_BODY()
public:
    ARelayArenaPresentation();
    virtual void Tick(float DeltaSeconds) override;
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

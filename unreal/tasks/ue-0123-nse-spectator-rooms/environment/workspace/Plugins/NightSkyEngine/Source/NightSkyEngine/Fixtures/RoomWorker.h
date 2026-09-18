#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "GameFramework/GameModeBase.h"
#include "NightSkyEngine/Network/SpectatorRoom.h"
#include "Dom/JsonObject.h"
#include "ReplayFixture.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "RoomWorker.generated.h"

namespace RoomWidgetTestAccess
{
class FPanelHost;
}
UCLASS()

class NIGHTSKYENGINE_API URoomWorkerGameInstance : public UReplayFixtureGameInstance
{
    GENERATED_BODY()
  public:
    virtual void Init() override;
    virtual void TravelToVSInfo() const override;
};
UCLASS()

class NIGHTSKYENGINE_API ARoomWorkerController : public ANightSkyPlayerController
{
    GENERATED_BODY()
  public:
    ARoomWorkerController();

    virtual void Tick(float Delta) override
    {
        APlayerController::Tick(Delta);
    }
};
UCLASS()

class NIGHTSKYENGINE_API ARoomWorkerGameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    ARoomWorkerGameMode();
};
/** File commands drive public operations on real network connections. No room policy lives here. */
UCLASS()

class NIGHTSKYENGINE_API URoomWorker : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
  public:
    virtual void Initialize(FSubsystemCollectionBase &Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;

    virtual bool IsTickable() const override
    {
        return Enabled && !IsTemplate();
    }

    virtual bool IsTickableWhenPaused() const override
    {
        return true;
    }

    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(URoomWorker, STATGROUP_Tickables);
    }

    bool BeginBattle();
    UFUNCTION()
    void Present(const FRoomDelivery &Delivery);
    UFUNCTION()
    void Audit(const FRoomDelivery &Delivery);
    void AuditAuthority(const FRoomDelivery &Delivery);

  private:
    void MapLoaded(UWorld *LoadedWorld);
    void CompleteInputWorldTick(UWorld *World, ELevelTick TickType, float DeltaTime);
    bool WidgetInputReady() const;
    TWeakObjectPtr<UWorld> CompletedInputFrameWorld;
    bool PreparedBattleMatches() const;
    void EmitStartPreparationDiagnostic(const FString &Phase, const FString &Nonce);
    void FinishBattlePreparation();
    void UpdateManualBattleControl();
    TArray<FString> PreparationFighters;
    TArray<FString> PreviousFighterMatches;
    FString ManualBattleMatch;
    bool AwaitingPublicStart = false;
    bool PublicStartRequested = false;
    bool CheckingPublicStart = false;
    bool BindingBattlePreparation = false;
    bool PreparingBattleForStart = false;
    bool PreparedBattleForStart = false;
    bool PreparationTravel = false;
    int32 PreparationSerial = 0;
    TWeakObjectPtr<AReplayFixtureBattle> PreparedBattle;
    TWeakObjectPtr<UWorld> PreparedWorld;
    // Strong references own fixture assets across travel and later retained-match checks.
    UPROPERTY()
    TArray<TObjectPtr<UObject>> AuthoredAssets;
    UPROPERTY()
    FBattleData AuthoredConfiguration;
    bool AuthoredTraining = false;
    bool HasAuthoredConfiguration = false;
    TMap<FString, FString> AuthoredContent;
    UPROPERTY()
    FBattleData PreparedConfiguration;
    TMap<FString, FString> PreparedContent;
    bool PreparedTraining = false;
    uint32 PreparedSeed = 0;
    int32 CompletedMapLoads = 0;
    bool OfflineReplayReady = false;
    void Emit(const TSharedPtr<FJsonObject> &Event);
    bool Execute(const TSharedPtr<FJsonObject> &Command);
    bool EnsureWidgetInput();
    bool ExecuteWidgetCommand(const TSharedPtr<FJsonObject> &Command, USpectatorRoom *Room);
    bool LoadFixtureContent(const TSharedPtr<FJsonObject> &Command, USpectatorRoom *Room);
    bool InspectSavedReplay(const TSharedPtr<FJsonObject> &Command, USpectatorRoom *Room);
    bool AdvanceSavedReplay(const TSharedPtr<FJsonObject> &Command, USpectatorRoom *Room);
    bool SendCorruptedHistory(const TSharedPtr<FJsonObject> &Command, USpectatorRoom *Room);
    bool ExecuteRoomCommand(const TSharedPtr<FJsonObject> &Command, USpectatorRoom *Room);
    TSharedPtr<RoomWidgetTestAccess::FPanelHost> AdapterInput;
    UPROPERTY()
    TObjectPtr<class URoomPanel> AdapterPanel;
    UPROPERTY()
    TObjectPtr<URoomConnection> Connection;
    UPROPERTY()
    TObjectPtr<AReplayFixtureBattle> Battle;
    TArray<FString> Content;
    TArray<FString> BeforeExportIds;
    TArray<uint8> PendingExportBytes;
    FString Directory, Role;
    int64 CommandOffset = 0;
    TArray<uint8> PendingCommandBytes;
    bool ObservedInvalidHistory = false;
    FString StatusBeforeInvalidHistory;
    bool Enabled = false;
    double Heartbeat = 0;
    bool PendingBattleTravel = false;
    int64 ControlledTick = 0;
};

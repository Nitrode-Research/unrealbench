#include "RoomWorker.h"
#include "RoomWidgetCapture.h"
#include "RoomReplayTestCatalog.h"
#include "RoomWidgetTestAccess.h"
#include "LoadingScreenManager.h"
#include "NightSkyEngine/Network/RoomPanel.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "NightSkyEngine/Data/PrimaryStageData.h"
#include "UObject/SavePackage.h"
#include "UObject/Linker.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Serialization/BufferArchive.h"
#include "Misc/SecureHash.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

ARoomWorkerGameMode::ARoomWorkerGameMode()
{
    PlayerControllerClass = ARoomWorkerController::StaticClass();
    DefaultPawnClass = nullptr;
    GameStateClass = AGameStateBase::StaticClass();
    bStartPlayersAsSpectators = true;
}

void URoomWorkerGameInstance::Init()
{
    UReplayFixtureGameInstance::Init();
    int32 Seed = 9009;
    FParse::Value(FCommandLine::Get(), TEXT("RoomBattleSeed="), Seed);
    BattleData.Random.Reseed(Seed);
    BattleVersion = TEXT("RoomFixture-1");
}

void URoomWorkerGameInstance::TravelToVSInfo() const
{
    GetSubsystem<URoomWorker>()->BeginBattle();
}

bool URoomWorker::PreparedBattleMatches() const
{
    auto *Game = Cast<UNightSkyGameInstance>(GetGameInstance());
    if (!PreparedBattleForStart || !Game || Game->IsReplay || !IsValid(Battle) ||
        PreparedBattle.Get() != Battle.Get() || PreparedWorld.Get() != GetWorld() ||
        Battle->GetWorld() != GetWorld() || !Battle->GetMainPlayer(true) || !Battle->GetMainPlayer(false))
        return false;
    if (!Battle->DidCompleteFixtureInitialization() || !IsValid(PreparedConfiguration.Stage) ||
        PreparedConfiguration.PlayerListP1.Num() != 1 || PreparedConfiguration.PlayerListP2.Num() != 1)
        return false;
    auto *First = PreparedConfiguration.PlayerListP1[0].LoadSynchronous();
    auto *Second = PreparedConfiguration.PlayerListP2[0].LoadSynchronous();
    if (!IsValid(First) || !IsValid(Second) || !First->PlayerClass || !Second->PlayerClass ||
        GetWorld()->GetOutermost()->GetName() != FURL(nullptr, *PreparedConfiguration.Stage->StageURL, TRAVEL_Absolute).Map ||
        !Battle->GetMainPlayer(true)->IsA(First->PlayerClass.Get()) ||
        !Battle->GetMainPlayer(false)->IsA(Second->PlayerClass.Get()))
        return false;
    // The completed actor's construction and actual native players are evidence;
    // GameInstance staging may legitimately be consumed by public binding/start.
    return ReplayFixtureConfigurationMatches(Battle->FixtureInitialConfiguration(), PreparedConfiguration) &&
           Battle->FixtureInitialTraining() == PreparedTraining &&
           Battle->BattleState.BattleFormat == PreparedConfiguration.BattleFormat &&
           Battle->BattleState.MaxRoundCount == PreparedConfiguration.RoundCount &&
           Battle->BattleState.MaxTimeUntilRoundStart == PreparedConfiguration.TimeUntilRoundStart &&
           !PreparedContent.IsEmpty() &&
           USpectatorRoom::InspectContent(PreparedConfiguration, Game->BattleVersion)
               .OrderIndependentCompareEqual(PreparedContent);
}

void URoomWorker::EmitStartPreparationDiagnostic(const FString &Phase, const FString &Nonce)
{
    auto *Room = GetGameInstance()->GetSubsystem<USpectatorRoom>();
    TArray<TSharedPtr<FJsonValue>> Owners;
    for (const FString &Identity : PreparationFighters)
    {
        const auto Delivery = Room->Observe(Identity);
        auto Owner = MakeShared<FJsonObject>();
        Owner->SetStringField(TEXT("identity"), Identity);
        Owner->SetStringField(TEXT("membership"), Delivery.Membership);
        Owner->SetStringField(TEXT("assignment"), Delivery.Assignment);
        Owner->SetStringField(TEXT("member_role"), Delivery.Role);
        Owner->SetStringField(TEXT("match"), Delivery.Match);
        Owners.Add(MakeShared<FJsonValueObject>(Owner));
    }
    // Read current public fixture state after the observations, which can deliver
    // callbacks. Diagnostic fields never participate in a readiness decision.
    auto *Game = Cast<UNightSkyGameInstance>(GetGameInstance());
    auto *World = GetWorld();
    auto *Actor = IsValid(Battle) ? Battle.Get() : nullptr;
    auto *P1 = Actor ? Actor->GetMainPlayer(true) : nullptr;
    auto *P2 = Actor ? Actor->GetMainPlayer(false) : nullptr;
    auto Checks = MakeShared<FJsonObject>();
    Checks->SetBoolField(TEXT("prepared_flag"), PreparedBattleForStart);
    Checks->SetBoolField(TEXT("game_valid"), Game != nullptr);
    Checks->SetBoolField(TEXT("not_replay"), Game && !Game->IsReplay);
    Checks->SetBoolField(TEXT("battle_valid"), Actor != nullptr);
    Checks->SetBoolField(TEXT("prepared_actor_matches"), PreparedBattle.Get() == Battle.Get());
    Checks->SetBoolField(TEXT("prepared_world_matches"), PreparedWorld.Get() == World);
    Checks->SetBoolField(TEXT("battle_in_current_world"), Actor && Actor->GetWorld() == World);
    Checks->SetBoolField(TEXT("p1_present"), P1 != nullptr);
    Checks->SetBoolField(TEXT("p2_present"), P2 != nullptr);
    auto Event = MakeShared<FJsonObject>();
    Event->SetStringField(TEXT("type"), TEXT("fixture-start-diagnostic"));
    Event->SetStringField(TEXT("preparation_phase"), Phase);
    Event->SetStringField(TEXT("start_request_nonce"), Nonce);
    Event->SetBoolField(TEXT("preparing_flag"), PreparingBattleForStart);
    Event->SetBoolField(TEXT("binding_flag"), BindingBattlePreparation);
    Event->SetBoolField(TEXT("awaiting_public_start"), AwaitingPublicStart);
    Event->SetBoolField(TEXT("public_start_requested"), PublicStartRequested);
    Event->SetBoolField(TEXT("pending_battle_travel"), PendingBattleTravel);
    Event->SetNumberField(TEXT("prepared_actor_id"), PreparedBattle.IsValid() ? int64(PreparedBattle->GetUniqueID()) : int64(-1));
    Event->SetNumberField(TEXT("current_actor_id"), Actor ? int64(Actor->GetUniqueID()) : int64(-1));
    Event->SetNumberField(TEXT("prepared_world_id"), PreparedWorld.IsValid() ? int64(PreparedWorld->GetUniqueID()) : int64(-1));
    if (World)
        Event->SetStringField(TEXT("pending_next_url"), World->NextURL);
    if (Game)
    {
        const auto &Current = Actor ? Actor->FixtureInitialConfiguration() : PreparedConfiguration;
        const bool FirstConfigured = !Current.PlayerListP1.IsEmpty() && Current.PlayerListP1[0].Get() != nullptr;
        const bool SecondConfigured = !Current.PlayerListP2.IsEmpty() && Current.PlayerListP2[0].Get() != nullptr;
        Checks->SetBoolField(TEXT("stage_present"), Current.Stage != nullptr);
        Checks->SetBoolField(TEXT("p1_configuration_present"), FirstConfigured);
        Checks->SetBoolField(TEXT("p2_configuration_present"), SecondConfigured);
        Checks->SetBoolField(TEXT("world_map_matches_stage"), World && Current.Stage &&
                            World->GetOutermost()->GetName() == FURL(nullptr, *Current.Stage->StageURL, TRAVEL_Absolute).Map);
        Checks->SetBoolField(TEXT("p1_character_matches"), P1 && FirstConfigured &&
                            P1->IsA(Current.PlayerListP1[0]->PlayerClass));
        Checks->SetBoolField(TEXT("p2_character_matches"), P2 && SecondConfigured &&
                            P2->IsA(Current.PlayerListP2[0]->PlayerClass));
        Checks->SetBoolField(TEXT("stage_identity_matches"), IsValid(Current.Stage) && IsValid(PreparedConfiguration.Stage) &&
                            FSoftObjectPath(Current.Stage) == FSoftObjectPath(PreparedConfiguration.Stage));
        Checks->SetBoolField(TEXT("p1_list_matches"), Current.PlayerListP1 == PreparedConfiguration.PlayerListP1);
        Checks->SetBoolField(TEXT("p2_list_matches"), Current.PlayerListP2 == PreparedConfiguration.PlayerListP2);
        Checks->SetBoolField(TEXT("seed_matches"), Current.Random.GetSeed() == PreparedSeed);
        Checks->SetBoolField(TEXT("timer_matches"), Current.StartRoundTimer == PreparedConfiguration.StartRoundTimer);
        Checks->SetBoolField(TEXT("round_count_matches"), Current.RoundCount == PreparedConfiguration.RoundCount);
        Checks->SetBoolField(TEXT("round_start_matches"), Current.TimeUntilRoundStart == PreparedConfiguration.TimeUntilRoundStart);
        Checks->SetBoolField(TEXT("training_matches"), Actor && Actor->FixtureInitialTraining() == PreparedTraining);
        Event->SetStringField(TEXT("configured_stage_url"), Current.Stage ? Current.Stage->StageURL : FString());
        Event->SetNumberField(TEXT("prepared_seed"), PreparedSeed);
        Event->SetNumberField(TEXT("configured_timer"), Current.StartRoundTimer);
        Event->SetNumberField(TEXT("prepared_timer"), PreparedConfiguration.StartRoundTimer);
        Event->SetNumberField(TEXT("configured_round_count"), Current.RoundCount);
        Event->SetNumberField(TEXT("prepared_round_count"), PreparedConfiguration.RoundCount);
        Event->SetNumberField(TEXT("configured_round_start"), Current.TimeUntilRoundStart);
        Event->SetNumberField(TEXT("prepared_round_start"), PreparedConfiguration.TimeUntilRoundStart);
        Event->SetBoolField(TEXT("configured_training"), Actor && Actor->FixtureInitialTraining());
        Event->SetBoolField(TEXT("prepared_training"), PreparedTraining);
    }
    Event->SetObjectField(TEXT("preparation_conditions"), Checks);
    Event->SetArrayField(TEXT("preparation_current_owners"), Owners);
    // Existing Emit also records current seed/map/native frame and public active state.
    if (World)
        Emit(Event);
}

bool URoomWorker::BeginBattle()
{
    auto *GameInstance = Cast<UNightSkyGameInstance>(GetGameInstance());
    // Reuse only this completed authored generation; do not repair mismatched native evidence.
    if (PreparedBattleForStart && !GameInstance->IsReplay)
    {
        if (!PreparedBattleMatches())
            return false;
        // This ordinary loading callback may reuse the completed authored setup.
        // Associate that same verified actor for the requested match without
        // reconstructing it, resetting state, or advancing a simulation frame.
        GameInstance->GetSubsystem<USpectatorRoom>()->BindBattle(Battle.Get());
        return true;
    }
    if (PreparingBattleForStart && !GameInstance->IsReplay && IsValid(Battle) && PreparedBattle.Get() == Battle.Get() &&
        PreparedWorld.Get() == GetWorld() && Battle->GetWorld() == GetWorld())
    {
        FinishBattlePreparation();
        return true;
    }
    PreparedBattleForStart = false;
    if (GameInstance->IsReplay)
    {
        PreparingBattleForStart = false;
        AwaitingPublicStart = false;
        PublicStartRequested = false;
        ManualBattleMatch.Empty();
    }
    const FBattleData &Setup = PreparingBattleForStart ? PreparedConfiguration : GameInstance->BattleData;
    if (!IsValid(Setup.Stage))
        return false;
    const FURL Target(nullptr, *Setup.Stage->StageURL, TRAVEL_Absolute);
    if (IsValid(Battle) || GetWorld()->GetOutermost()->GetName() != Target.Map)
    {
        // The native fixture follows a real server travel for rematches, so existing
        // gameplay actors are torn down and each connection is recreated normally.
        if (GetWorld()->GetNetMode() == NM_Client)
            return false;
        PendingBattleTravel = true;
        if (PreparingBattleForStart) PreparationTravel = true;
        FURL TravelURL = Target;
        TravelURL.AddOption(TEXT("game=/Script/NightSkyEngine.RoomWorkerGameMode"));
        return GetWorld()->ServerTravel(TravelURL.ToString());
    }
    if (PreparingBattleForStart)
    {
        // Supply this explicit preparation's authored inputs at construction, including after travel.
        // Never restore staging data after binding or a successful start.
        GameInstance->BattleData = PreparedConfiguration;
        GameInstance->IsTraining = PreparedTraining;
    }
    // Native content replaces the project's Blueprint loading screen, then uses the real battle lifecycle.
    FActorSpawnParameters Params;
    Params.bDeferConstruction = true;
    Battle = GetWorld()->SpawnActor<AReplayFixtureBattle>(
        AReplayFixtureBattle::StaticClass(), FTransform::Identity, Params);
    if (!Battle)
        return false;
    Battle->SetReplicates(false);
    Battle->FinishSpawning(FTransform::Identity);
    if (PreparingBattleForStart)
    {
        PreparedBattle = Battle.Get();
        PreparedWorld = GetWorld();
        // Leave normal native initialization ticks available while pending.
        FinishBattlePreparation();
    }
    else if (!AwaitingPublicStart)
        Battle->SetActorTickEnabled(false); // The process driver advances the published fixed simulation step.
    return true;
}

void URoomWorker::FinishBattlePreparation()
{
    if (BindingBattlePreparation || !PreparingBattleForStart || !IsValid(Battle) || PreparedBattle.Get() != Battle.Get() ||
        PreparedWorld.Get() != GetWorld() || Battle->GetWorld() != GetWorld() ||
        !Battle->GetMainPlayer(true) || !Battle->GetMainPlayer(false))
        return;
    // Binding supplies the observed native players. Normal ticks remain enabled
    // through later initialization and pending public start. Do not reset frames.
    const TWeakObjectPtr<AReplayFixtureBattle> BindingActor(Battle.Get());
    const TWeakObjectPtr<UWorld> BindingWorld(GetWorld());
    const int32 BindingSerial = PreparationSerial;
    BindingBattlePreparation = true;
    GetGameInstance()->GetSubsystem<USpectatorRoom>()->BindBattle(BindingActor.Get());
    BindingBattlePreparation = false;
    // Public binding may synchronously request ordinary travel or a new setup.
    // A returning older binding must not mark that newer setup complete.
    auto *CurrentActor = BindingActor.Get();
    if (BindingSerial != PreparationSerial || !PreparingBattleForStart || !IsValid(CurrentActor) ||
        PreparedBattle.Get() != CurrentActor || Battle.Get() != CurrentActor ||
        PreparedWorld.Get() != BindingWorld.Get() || BindingWorld.Get() != GetWorld() ||
        CurrentActor->GetWorld() != GetWorld())
        return;
    PreparedBattleForStart = true;
    PreparingBattleForStart = false;
}

void URoomWorker::UpdateManualBattleControl()
{
    if (!AwaitingPublicStart || !PublicStartRequested || CheckingPublicStart || PreparationFighters.Num() != 2 ||
        PreviousFighterMatches.Num() != 2 || !IsValid(Battle) || Battle->GetWorld() != GetWorld() ||
        !Battle->GetMainPlayer(true) || !Battle->GetMainPlayer(false) ||
        Cast<UNightSkyGameInstance>(GetGameInstance())->IsReplay)
        return;
    auto *Room = GetGameInstance()->GetSubsystem<USpectatorRoom>();
    if (!Room->IsActive())
        return;
    // Observe is public and read-only. The guard also tolerates implementations
    // that synchronously broadcast a delivery while answering the observation.
    CheckingPublicStart = true;
    const auto First = Room->Observe(PreparationFighters[0]);
    const auto Second = Room->Observe(PreparationFighters[1]);
    CheckingPublicStart = false;
    auto OwnsCurrentSeat = [](const FRoomDelivery &Delivery, const FString &Identity)
    {
        // This fixture always creates its organizer as host. Organizer authority
        // and ownership of one offered fighter seat may coexist in the delivery.
        return !Delivery.Membership.IsEmpty() && !Delivery.Assignment.IsEmpty() &&
               (Delivery.Role == TEXT("fighter") ||
                (Identity == TEXT("host") && Delivery.Role == TEXT("organizer")));
    };
    if (!OwnsCurrentSeat(First, PreparationFighters[0]) ||
        !OwnsCurrentSeat(Second, PreparationFighters[1]) || First.Assignment == Second.Assignment ||
        First.Match.IsEmpty() || First.Match != Second.Match ||
        First.Match == PreviousFighterMatches[0] || First.Match == PreviousFighterMatches[1] ||
        !Room->IsActive() || !IsValid(Battle) || Battle->GetWorld() != GetWorld())
        return;
    ManualBattleMatch = First.Match;
    AwaitingPublicStart = false;
    Battle->SetActorTickEnabled(false); // Public successful start precedes manual driver steps.
}

void URoomWorker::Initialize(FSubsystemCollectionBase &Collection)
{
    Super::Initialize(Collection);
    Enabled = Cast<URoomWorkerGameInstance>(GetGameInstance()) &&
              FParse::Value(FCommandLine::Get(), TEXT("RoomWorkerDir="), Directory) &&
              FParse::Value(FCommandLine::Get(), TEXT("RoomWorkerRole="), Role);
    if (Enabled)
    {
        URoomConnection::AuthorityDelivery.AddUObject(this, &URoomWorker::AuditAuthority);
        FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &URoomWorker::MapLoaded);
        FWorldDelegates::OnWorldTickEnd.AddUObject(this, &URoomWorker::CompleteInputWorldTick);
    }
}

void URoomWorker::Deinitialize()
{
    FWorldDelegates::OnWorldTickEnd.RemoveAll(this);
    AdapterInput.Reset();
    Super::Deinitialize();
}

void URoomWorker::CompleteInputWorldTick(UWorld *World, ELevelTick TickType, float)
{
    // This per-world callback follows its tickable subsystems. The worldless
    // worker tick and process EndFrame cannot certify a newly traveled world.
    const auto *LoadingScreen = GetGameInstance()->GetSubsystem<ULoadingScreenManager>();
    if (World && World == GetWorld() && World->HasBegunPlay() &&
        TickType == LEVELTICK_All && !World->IsPaused() &&
        (!LoadingScreen || LoadingScreen->IsTickable()))
        CompletedInputFrameWorld = World;
}

bool URoomWorker::WidgetInputReady() const
{
    const auto *LoadingScreen = GetGameInstance()->GetSubsystem<ULoadingScreenManager>();
    return CompletedInputFrameWorld.IsValid() && CompletedInputFrameWorld.Get() == GetWorld() &&
           (!LoadingScreen || (LoadingScreen->IsTickable() && !LoadingScreen->GetLoadingScreenDisplayStatus()));
}

void URoomWorker::MapLoaded(UWorld *LoadedWorld)
{
    if (LoadedWorld && LoadedWorld->GetGameInstance() == GetGameInstance())
    {
        CompletedInputFrameWorld.Reset();
        ++CompletedMapLoads;
        if (CastChecked<UNightSkyGameInstance>(GetGameInstance())->IsReplay)
        {
            // Observe the battle created by ordinary saved-stage loading. Take
            // manual tick ownership at the load boundary, before its first frame.
            // Do not replace the authored replay game mode or spawn a second battle.
            OfflineReplayReady = false;
            PendingBattleTravel = false;
            Battle = LoadedWorld->GetGameState<AReplayFixtureBattle>();
            if (IsValid(Battle))
                Battle->SetActorTickEnabled(false);
        }
    }
}

namespace
{
FString DescribeActiveObject(const ABattleObject *Object, ANightSkyGameState *Battle)
{
    const int32 OwnerSide = Object->Player == Battle->GetMainPlayer(true)    ? 0
                            : Object->Player == Battle->GetMainPlayer(false) ? 1
                                                                             : -1;
    return FString::Printf(TEXT("%s|%s|%d|%d|%d|%d|%d|%d|%d"),
                           *Object->ObjectStateName.ToString(),
                           *Object->GetCelName().ToString(),
                           OwnerSide,
                           Object->ActionTime,
                           Object->PosX,
                           Object->PosY,
                           Object->SpeedX,
                           Object->SpeedY,
                           int32(Object->Hitstop));
}

} // namespace

void URoomWorker::Emit(const TSharedPtr<FJsonObject> &Event)
{
    Event->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds());
    Event->SetNumberField(TEXT("configuration_seed"),
                          HasAuthoredConfiguration ? AuthoredConfiguration.Random.GetSeed() :
                          CastChecked<UNightSkyGameInstance>(GetGameInstance())->BattleData.Random.GetSeed());
    Event->SetBoolField(TEXT("owner_channel_ready"), IsValid(Connection));
    const auto *LoadingScreen = GetGameInstance()->GetSubsystem<ULoadingScreenManager>();
    Event->SetBoolField(TEXT("loading_screen_visible"),
                        LoadingScreen && LoadingScreen->GetLoadingScreenDisplayStatus());
    Event->SetBoolField(TEXT("widget_input_ready"), WidgetInputReady());
    if (IsValid(Connection))
    {
        Event->SetBoolField(TEXT("integrity_failed"),
                            ObservedInvalidHistory && !Connection->LastDelivery.Status.TrimStartAndEnd().IsEmpty() &&
                            Connection->LastDelivery.Status != StatusBeforeInvalidHistory);
        Event->SetNumberField(TEXT("received_chunks"), Connection->ReceivedChunks);
        Event->SetNumberField(TEXT("reassembled_deliveries"), Connection->ReassembledDeliveries);
    }
    auto *Room = GetGameInstance()->GetSubsystem<USpectatorRoom>();
    auto Objects = [](ANightSkyGameState *State)
    {
        TArray<FString> Descriptions;
        if (IsValid(State))
            for (const auto *Object : State->Objects)
                if (IsValid(Object) && Object->IsActive)
                    Descriptions.Add(DescribeActiveObject(Object, State));
        Descriptions.Sort();
        TArray<TSharedPtr<FJsonValue>> Result;
        for (const FString &Description : Descriptions)
            Result.Add(MakeShared<FJsonValueString>(Description));
        return Result;
    };
    auto State = [&](ANightSkyGameState *BattleState)
    {
        auto Data = MakeShared<FJsonObject>();
        if (!IsValid(BattleState) || !BattleState->GetMainPlayer(true) || !BattleState->GetMainPlayer(false))
            return Data;
        Data->SetNumberField(TEXT("frame"), BattleState->BattleState.FrameNumber);
        Data->SetNumberField(TEXT("timer"), BattleState->BattleState.RoundTimer);
        TArray<TSharedPtr<FJsonValue>> Players;
        for (int32 Side = 0; Side < 2; ++Side)
        {
            auto *Player = BattleState->GetMainPlayer(Side == 0);
            auto Row = MakeShared<FJsonObject>();
            Row->SetNumberField(TEXT("x"), Player->PosX);
            Row->SetNumberField(TEXT("y"), Player->PosY);
            Row->SetNumberField(TEXT("vx"), Player->SpeedX);
            Row->SetNumberField(TEXT("vy"), Player->SpeedY);
            Row->SetNumberField(TEXT("health"), Player->CurrentHealth);
            Row->SetNumberField(TEXT("meter"), BattleState->BattleState.Meter[Side]);
            Row->SetNumberField(TEXT("inputs"), Player->Inputs);
            Row->SetNumberField(TEXT("phase"), Player->ActionTime);
            Row->SetStringField(TEXT("cel"), Player->GetCelName().ToString());
            Players.Add(MakeShared<FJsonValueObject>(Row));
        }
        Data->SetArrayField(TEXT("players"), Players);
        Data->SetArrayField(TEXT("objects"), Objects(BattleState));
        return Data;
    };
    Event->SetObjectField(TEXT("prediction_state"), State(Room->PresentationBattle()));
    Event->SetObjectField(TEXT("battle_state"), State(IsValid(Battle) ? Battle.Get() : nullptr));
    Event->SetArrayField(TEXT("prediction_objects"), Objects(Room->PresentationBattle()));
    Event->SetArrayField(TEXT("battle_objects"), Objects(IsValid(Battle) ? Battle.Get() : nullptr));
    Event->SetNumberField(TEXT("prediction_frame"), Room->PredictionFrame());
    Event->SetNumberField(TEXT("prediction_confirmed"), Room->PredictionConfirmedFrame());
    Event->SetNumberField(TEXT("prediction_rollbacks"), Room->PredictionRollbacks());
    if (auto *Presented = Room->PresentationBattle(); IsValid(Presented) &&
        Presented->GetMainPlayer(true) && Presented->GetMainPlayer(false))
    {
        Event->SetNumberField(TEXT("prediction_x"), Presented->GetMainPlayer(true)->PosX);
        Event->SetNumberField(TEXT("prediction_health"), Presented->GetMainPlayer(false)->CurrentHealth);
        Event->SetNumberField(TEXT("prediction_p1_health"), Presented->GetMainPlayer(true)->CurrentHealth);
    }
    if (GetWorld()->GetNetMode() != NM_Client)
        Event->SetNumberField(TEXT("authority_tick"),
                              GetGameInstance()->GetSubsystem<USpectatorRoom>()->RoomTick());
    if (IsValid(Battle) && Battle->GetMainPlayer(true) && Battle->GetMainPlayer(false))
    {
        Event->SetNumberField(TEXT("battle_frame"), Battle->BattleState.FrameNumber);
        Event->SetNumberField(TEXT("battle_x"), Battle->GetMainPlayer(true)->PosX);
        Event->SetNumberField(TEXT("battle_health"), Battle->GetMainPlayer(false)->CurrentHealth);
        Event->SetNumberField(TEXT("battle_p1_health"), Battle->GetMainPlayer(true)->CurrentHealth);
    }
    Event->SetNumberField(TEXT("completed_map_loads"), CompletedMapLoads);
    Event->SetNumberField(TEXT("world_id"), GetWorld()->GetUniqueID());
    Event->SetStringField(TEXT("world_map"), GetWorld()->GetOutermost()->GetName());
    Event->SetBoolField(TEXT("active_match"), Room->IsActive());
    Event->SetNumberField(TEXT("prepared_battle_serial"), PreparationSerial);
    Event->SetBoolField(TEXT("prepared_battle_ready"), PreparedBattleMatches());
    Event->SetBoolField(TEXT("preparation_travel"), PreparationTravel);
    Event->SetStringField(TEXT("manual_battle_match"), ManualBattleMatch);
    if (IsValid(Battle) && Battle->GetMainPlayer(true) && Battle->GetMainPlayer(false))
    {
        Event->SetNumberField(TEXT("battle_max_health"), Battle->GetMainPlayer(true)->MaxHealth);
        Event->SetStringField(TEXT("battle_character"),
                              Battle->GetMainPlayer(true)->GetClass()->GetPathName());
    }
    if (IsValid(Connection))
    {
        Event->SetStringField(TEXT("current_membership"), Connection->LastDelivery.Membership);
        Event->SetStringField(TEXT("current_match"), Connection->LastDelivery.Match);
        Event->SetNumberField(TEXT("current_frame"), Connection->LastDelivery.Frame);
    }
    Event->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
    Event->SetStringField(TEXT("role"), Role);
    Event->SetNumberField(TEXT("net_mode"), int32(GetWorld()->GetNetMode()));
    FString Line;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
    FJsonSerializer::Serialize(Event.ToSharedRef(), Writer);
    Line += TEXT("\n");
    FFileHelper::SaveStringToFile(Line,
                                  *(Directory / (TEXT("events-") + Role + TEXT(".jsonl"))),
                                  FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
                                  &IFileManager::Get(),
                                  FILEWRITE_Append | FILEWRITE_AllowRead);
}

void URoomWorker::Present(const FRoomDelivery &Delivery)
{
    auto *Presented = GetGameInstance()->GetSubsystem<USpectatorRoom>()->PresentationBattle();
    if (!Presented)
        Presented = Battle;
    if (!IsValid(Presented) || !Presented->GetMainPlayer(true) || !Presented->GetMainPlayer(false) ||
        Delivery.Frame < 0)
        return;
    auto Event = MakeShared<FJsonObject>();
    Event->SetStringField(TEXT("type"), TEXT("presentation"));
    Event->SetStringField(TEXT("status"), Delivery.Status);
    Event->SetStringField(TEXT("nonce"), Delivery.RequestNonce);
    Event->SetNumberField(TEXT("frame"), Delivery.Frame);
    Event->SetNumberField(TEXT("x"), Presented->GetMainPlayer(true)->PosX);
    Event->SetNumberField(TEXT("health"), Presented->GetMainPlayer(false)->CurrentHealth);
    Emit(Event);
}

void URoomWorker::AuditAuthority(const FRoomDelivery &Delivery)
{
    UpdateManualBattleControl();
    auto Event = MakeShared<FJsonObject>();
    Event->SetStringField(TEXT("type"), TEXT("authority-delivery"));
    Event->SetStringField(TEXT("nonce"), Delivery.RequestNonce);
    Event->SetStringField(TEXT("status"), Delivery.Status);
    Event->SetNumberField(TEXT("export_bytes"), Delivery.Replay.Num());
    Emit(Event);
}

void URoomWorker::Audit(const FRoomDelivery &Delivery)
{
    auto Event = MakeShared<FJsonObject>();
    Event->SetStringField(TEXT("type"), TEXT("delivery"));
    Event->SetStringField(TEXT("membership"), Delivery.Membership);
    Event->SetStringField(TEXT("nonce"), Delivery.RequestNonce);
    Event->SetStringField(TEXT("status"), Delivery.Status);
    Event->SetStringField(TEXT("room"), Delivery.Room);
    Event->SetStringField(TEXT("match"), Delivery.Match);
    Event->SetStringField(TEXT("member_role"), Delivery.Role);
    Event->SetStringField(TEXT("assignment"), Delivery.Assignment);
    Event->SetStringField(TEXT("offer"), Delivery.Offer);
    Event->SetStringField(TEXT("mode"), Delivery.Mode);
    Event->SetStringField(TEXT("outcome"), Delivery.Outcome);
    Event->SetNumberField(TEXT("frame"), Delivery.Frame);
    Event->SetNumberField(TEXT("edge"), Delivery.Edge);
    Event->SetNumberField(TEXT("ack"), Delivery.Acknowledgement);
    Event->SetBoolField(TEXT("locked"), Delivery.Locked);
    Event->SetBoolField(TEXT("paused"), Delivery.Paused);
    Event->SetNumberField(TEXT("input1"), Delivery.Gameplay.Input1);
    Event->SetNumberField(TEXT("input2"), Delivery.Gameplay.Input2);
    if (IsValid(Battle) && Battle->GetMainPlayer(true) && Battle->GetMainPlayer(false))
    {
        Event->SetNumberField(TEXT("present_x"), Battle->GetMainPlayer(true)->PosX);
        Event->SetNumberField(TEXT("present_health"), Battle->GetMainPlayer(false)->CurrentHealth);
    }
    FString DecodedMatch, DecodedState;
    TArray<int32> DecodedInputsA, DecodedInputsB;
    const bool Decoded = USpectatorRoom::DecodeConfirmedInputs(
        Delivery.Gameplay, DecodedMatch, DecodedInputsA, DecodedInputsB, DecodedState);
    if (!Decoded && !Delivery.Gameplay.State.IsEmpty() && !ObservedInvalidHistory)
    {
        ObservedInvalidHistory = true;
        StatusBeforeInvalidHistory = IsValid(Connection) ? Connection->LastDelivery.Status : FString();
    }
    Event->SetBoolField(TEXT("decoded_valid"), Decoded);
    Event->SetStringField(TEXT("decoded_match"), DecodedMatch);
    TArray<TSharedPtr<FJsonValue>> PlayerOneInputs, PlayerTwoInputs;
    for (int32 Input : DecodedInputsA)
        PlayerOneInputs.Add(MakeShared<FJsonValueNumber>(Input));
    for (int32 Input : DecodedInputsB)
        PlayerTwoInputs.Add(MakeShared<FJsonValueNumber>(Input));
    Event->SetArrayField(TEXT("decoded_inputs1"), PlayerOneInputs);
    Event->SetArrayField(TEXT("decoded_inputs2"), PlayerTwoInputs);
    Event->SetStringField(TEXT("gameplay"), FBase64::Encode(Delivery.Gameplay.State));
    Event->SetStringField(TEXT("replay"), FBase64::Encode(Delivery.Replay));
    if (!Delivery.Replay.IsEmpty())
        PendingExportBytes = Delivery.Replay;
    if (!Delivery.Replay.IsEmpty())
    {
        FRoomMatch Replay;
        const bool Valid = USpectatorRoom::ReadReplay(Delivery.Replay, Replay);
        Event->SetBoolField(TEXT("replay_valid"), Valid);
        if (Valid)
        {
            auto Artifact = MakeShared<FJsonObject>();
            Artifact->SetStringField(TEXT("match"), Replay.Id);
            Artifact->SetStringField(TEXT("outcome"), Replay.Outcome);
            Artifact->SetNumberField(TEXT("start_tick"), Replay.StartTick);
            Artifact->SetNumberField(TEXT("outcome_tick"), Replay.OutcomeTick);
            Artifact->SetBoolField(TEXT("training"), Replay.Training);
            Artifact->SetStringField(TEXT("battle_class"), GetPathNameSafe(Replay.BattleClass.Get()));
            Artifact->SetNumberField(TEXT("seed"), Replay.Configuration.Random.GetSeed());
            Artifact->SetBoolField(TEXT("configuration_valid"), Replay.Configuration.bIsValid);
            Artifact->SetNumberField(TEXT("format"), int32(Replay.Configuration.BattleFormat));
            Artifact->SetNumberField(TEXT("round_start"), Replay.Configuration.TimeUntilRoundStart);
            Artifact->SetNumberField(TEXT("round_count"), Replay.Configuration.RoundCount);
            Artifact->SetNumberField(TEXT("round_timer"), Replay.Configuration.StartRoundTimer);
            Artifact->SetStringField(TEXT("stage"), GetPathNameSafe(Replay.Configuration.Stage));
            Artifact->SetStringField(TEXT("music"), Replay.Configuration.MusicName.ToString());
            TArray<TSharedPtr<FJsonValue>> Participants, CharactersOne, CharactersTwo, ColorsOne, ColorsTwo,
                Frames;
            for (const FString &Participant : Replay.Participants)
                Participants.Add(MakeShared<FJsonValueString>(Participant));
            for (const auto &Character : Replay.Configuration.PlayerListP1)
                CharactersOne.Add(MakeShared<FJsonValueString>(Character.ToSoftObjectPath().ToString()));
            for (const auto &Character : Replay.Configuration.PlayerListP2)
                CharactersTwo.Add(MakeShared<FJsonValueString>(Character.ToSoftObjectPath().ToString()));
            for (int32 Color : Replay.Configuration.ColorIndicesP1)
                ColorsOne.Add(MakeShared<FJsonValueNumber>(Color));
            for (int32 Color : Replay.Configuration.ColorIndicesP2)
                ColorsTwo.Add(MakeShared<FJsonValueNumber>(Color));
            for (const FRoomFrame &Frame : Replay.Frames)
            {
                auto InputPair = MakeShared<FJsonObject>();
                InputPair->SetNumberField(TEXT("input1"), Frame.Input1);
                InputPair->SetNumberField(TEXT("input2"), Frame.Input2);
                InputPair->SetNumberField(TEXT("confirmation"), Frame.ConfirmedTick);
                Frames.Add(MakeShared<FJsonValueObject>(InputPair));
            }
            auto ReplayContent = MakeShared<FJsonObject>();
            for (const auto &Entry : Replay.Content)
                ReplayContent->SetStringField(Entry.Key, Entry.Value);
            Artifact->SetObjectField(TEXT("content"), ReplayContent);
            Artifact->SetArrayField(TEXT("participants"), Participants);
            Artifact->SetArrayField(TEXT("characters1"), CharactersOne);
            Artifact->SetArrayField(TEXT("characters2"), CharactersTwo);
            Artifact->SetArrayField(TEXT("colors1"), ColorsOne);
            Artifact->SetArrayField(TEXT("colors2"), ColorsTwo);
            Artifact->SetArrayField(TEXT("frames"), Frames);
            Event->SetObjectField(TEXT("replay_semantics"), Artifact);
        }
    }

    TArray<TSharedPtr<FJsonValue>> Roster;
    for (const auto &R : Delivery.Roster)
        Roster.Add(MakeShared<FJsonValueString>(R));
    Event->SetArrayField(TEXT("roster"), Roster);
    Emit(Event);
}

bool URoomWorker::Execute(const TSharedPtr<FJsonObject> &Command)
{
    const FString Operation = Command->GetStringField(TEXT("op"));
    auto *Room = GetGameInstance()->GetSubsystem<USpectatorRoom>();
    if (Operation == TEXT("capture-room-display"))
    {
        if (!EnsureWidgetInput())
            return false;
        const double CaptureStart = FPlatformTime::Seconds();
        if (!AdapterInput->Reveal(AdapterPanel->GetWidgetFromName(TEXT("RoomStatus"))))
            return false;
        FString Image;
        if (!RoomWidgetCapture::SaveWindow(AdapterInput->GetWindow(), Image))
            return false;
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("type"), TEXT("room-display"));
        Event->SetStringField(TEXT("image"), Image);
        Event->SetNumberField(TEXT("capture_seconds"), FPlatformTime::Seconds() - CaptureStart);
        Event->SetBoolField(TEXT("recovery_pending"), Connection->LastDelivery.Recovering);
        Emit(Event);
        return true;
    }
    if (Operation == TEXT("widget"))
    {
        return ExecuteWidgetCommand(Command, Room);
    }
    if (Operation == TEXT("retry-content"))
    {
        if (!Connection)
            return false;
        auto *GameInstance = Cast<UNightSkyGameInstance>(GetGameInstance());
        Content.Empty();
        for (const auto &Entry : USpectatorRoom::InspectRequiredContent(
                 Connection->LastDelivery.RequiredContent, GameInstance->BattleVersion))
        {
            Content.Add(Entry.Key);
            Content.Add(Entry.Value);
        }
        Connection->Authenticate(Command->GetStringField(TEXT("identity")), TEXT("secret"), Content);
        return true;
    }
    if (Operation == TEXT("release-content-file"))
    {
        // Release package reader handles before the fixture changes a real file.
        // Loaded objects and room state remain available for the healthy controls.
        if (auto *Package = FindPackage(nullptr, *Command->GetStringField(TEXT("package"))))
            ResetLoaders(Package);
        return true;
    }
    if (Operation == TEXT("content"))
    {
        return LoadFixtureContent(Command, Room);
    }
    if (Operation == TEXT("inspect-replay"))
    {
        return InspectSavedReplay(Command, Room);
    }
    if (Operation == TEXT("replay"))
    {
        FString ReplayId;
        TArray<uint8> ExportBytes;
        if (!RoomReplayTestCatalog::Load(Command->GetStringField(TEXT("match")), ReplayId, ExportBytes) ||
            !Room->SavedReplays().Contains(ReplayId))
            return false;
        Room->PlaySavedReplay(ReplayId);
        // The caller waits for actual ordinary native replay initialization.
        return true;
    }
    if (Operation == TEXT("replay-step"))
    {
        return AdvanceSavedReplay(Command, Room);
    }
    if (Operation == TEXT("prepare-battle"))
    {
        if (GetWorld()->GetNetMode() == NM_Client || Room->IsActive())
            return false;
        const TArray<TSharedPtr<FJsonValue>> *Fighters = nullptr;
        if (!Command->TryGetArrayField(TEXT("fighters"), Fighters) || Fighters->Num() != 2 ||
            (*Fighters)[0]->AsString().IsEmpty() || (*Fighters)[1]->AsString().IsEmpty() ||
            (*Fighters)[0]->AsString() == (*Fighters)[1]->AsString())
            return false;
        if (!HasAuthoredConfiguration || AuthoredContent.IsEmpty())
            return false;
        PreparedConfiguration = AuthoredConfiguration;
        PreparedTraining = AuthoredTraining;
        PreparedSeed = AuthoredConfiguration.Random.GetSeed();
        PreparedContent = AuthoredContent;
        auto *Game = CastChecked<UNightSkyGameInstance>(GetGameInstance());
        Game->BattleData = PreparedConfiguration;
        Game->IsTraining = PreparedTraining;
        PreparationFighters = {(*Fighters)[0]->AsString(), (*Fighters)[1]->AsString()};
        PreviousFighterMatches = {Room->Observe(PreparationFighters[0]).Match,
                                 Room->Observe(PreparationFighters[1]).Match};
        ManualBattleMatch.Empty();
        AwaitingPublicStart = true;
        PublicStartRequested = false;
        ++PreparationSerial;
        PreparedBattleForStart = false;
        PreparedBattle.Reset();
        PreparedWorld.Reset();
        PreparingBattleForStart = true;
        PreparationTravel = false;
        return BeginBattle();
    }
    if (Operation == TEXT("battle"))
        return BeginBattle();
    if (Operation == TEXT("step"))
    {
        if (!Battle)
            return false;
        Room->BattleTick(OneFrame);
        return true;
    }
    if (Operation == TEXT("recover"))
        return Room->Recover(Command->GetStringField(TEXT("slot")));
    if (Operation == TEXT("create"))
        return Room->Create(Command->GetStringField(TEXT("slot")),
                            TEXT("host"),
                            TEXT("secret"),
                            int32(Command->GetNumberField(TEXT("delay"))));
    if (Operation == TEXT("clock"))
    {
        const int64 Tick = int64(Command->GetNumberField(TEXT("tick")));
        if (Tick < ControlledTick)
            return false;
        ControlledTick = Tick;
        Room->SetClock(
            [this]()
            {
                return ControlledTick;
            });
        return true;
    }
    if (Operation == TEXT("authenticate"))
    {
        if (!Connection && GetWorld()->GetNetMode() != NM_Client)
        {
            const FString Identity = Command->GetStringField(TEXT("identity"));
            TMap<FString, FString> Revisions;
            for (int32 Index = 0; Index + 1 < Content.Num(); Index += 2)
                Revisions.Add(Content[Index], Content[Index + 1]);
            const bool Authenticated = Room->Authenticate(Identity, TEXT("secret"), Revisions);
            if (Authenticated)
                Audit(Room->Observe(Identity));
            return Authenticated;
        }
        if (!Connection)
            return false;
        Connection->Authenticate(Command->GetStringField(TEXT("identity")), TEXT("secret"), Content);
        return true;
    }
    if (Operation == TEXT("corrupt-transfer"))
    {
        return SendCorruptedHistory(Command, Room);
    }
    if (Operation == TEXT("room"))
    {
        return ExecuteRoomCommand(Command, Room);
    }
    if (Operation == TEXT("travel"))
    {
        if (GetWorld()->GetNetMode() == NM_Client)
            return false;
        return GetWorld()->ServerTravel(
            TEXT("/Engine/Maps/Entry?game=/Script/NightSkyEngine.RoomWorkerGameMode"));
    }
    return Operation == TEXT("snapshot");
}

void URoomWorker::Tick(float)
{
    if (!GetWorld() || !GetWorld()->HasBegunPlay())
        return;
    if (!IsValid(Battle))
    {
        Battle = nullptr;
        PreparedBattleForStart = false;
        PreparedBattle.Reset();
        PreparedWorld.Reset();
    }
    if (Cast<UNightSkyGameInstance>(GetGameInstance())->IsReplay)
    {
        PreparedBattleForStart = false;
        PreparingBattleForStart = false;
        AwaitingPublicStart = false;
        PublicStartRequested = false;
        ManualBattleMatch.Empty();
        PreparedBattle.Reset();
        PreparedWorld.Reset();
    }
    if (!IsValid(Connection))
    {
        Connection = nullptr;
        AdapterInput.Reset();
        if (AdapterPanel)
        {
            AdapterPanel->RemoveFromParent();
            AdapterPanel = nullptr;
        }
    }
    if (PendingBattleTravel && !Battle && GetWorld()->NextURL.IsEmpty() &&
        GEngine->GetWorldContextFromWorldChecked(GetWorld()).TravelURL.IsEmpty())
    {
        PendingBattleTravel = false;
        BeginBattle();
    }
    if (!OfflineReplayReady && Cast<UNightSkyGameInstance>(GetGameInstance())->IsReplay &&
        IsValid(Battle) && Battle->GetWorld() == GetWorld() &&
        Battle->DidCompleteFixtureInitialization() && Battle->GetMainPlayer(true) &&
        Battle->GetMainPlayer(false) && GetWorld()->NextURL.IsEmpty() &&
        GEngine->GetWorldContextFromWorldChecked(GetWorld()).TravelURL.IsEmpty())
    {
        OfflineReplayReady = true;
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("type"), TEXT("offline-ready"));
        Emit(Event);
    }
    FinishBattlePreparation();
    UpdateManualBattleControl();
    if (!Connection)
        for (TActorIterator<ANightSkyPlayerController> It(GetWorld()); It; ++It)
            if (It->IsLocalController())
            {
                Connection = It->FindComponentByClass<URoomConnection>();
                if (Connection)
                {
                    Connection->OnTransportDelivery.AddDynamic(this, &URoomWorker::Audit);
                    Connection->OnDelivery.AddDynamic(this, &URoomWorker::Present);
                }
                break;
            }
    const FString CommandFile = Directory / (TEXT("commands-") + Role + TEXT(".jsonl"));
    TUniquePtr<FArchive> Reader(
        IFileManager::Get().CreateFileReader(*CommandFile, FILEREAD_Silent | FILEREAD_AllowWrite));
    if (Reader)
    {
        const int64 Size = Reader->TotalSize();
        if (Size < CommandOffset)
        {
            CommandOffset = 0;
            PendingCommandBytes.Empty();
        }
        const int64 Added = Size - CommandOffset;
        if (Added > 0 && Added < 64 * 1024 * 1024)
        {
            const int32 Prior = PendingCommandBytes.Num();
            PendingCommandBytes.AddUninitialized(int32(Added));
            Reader->Seek(CommandOffset);
            Reader->Serialize(PendingCommandBytes.GetData() + Prior, Added);
            if (Reader->IsError())
                PendingCommandBytes.SetNum(Prior);
            else
                CommandOffset = Size;
        }
        int32 Consumed = 0;
        for (int32 Index = 0; Index < PendingCommandBytes.Num(); ++Index)
            if (PendingCommandBytes[Index] == '\n')
            {
                FUTF8ToTCHAR Text(
                    reinterpret_cast<const ANSICHAR *>(PendingCommandBytes.GetData() + Consumed),
                    Index - Consumed);
                FString Line(Text.Length(), Text.Get());
                TSharedPtr<FJsonObject> Command;
                auto Event = MakeShared<FJsonObject>();
                Event->SetStringField(TEXT("type"), TEXT("reply"));
                bool Ok = false;
                if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Command) &&
                    Command.IsValid())
                {
                    FString Operation;
                    Command->TryGetStringField(TEXT("op"), Operation);
                    if ((Operation == TEXT("widget") || Operation == TEXT("capture-room-display")) &&
                        !WidgetInputReady())
                    {
                        // Keep this command queued while the normal loading screen owns
                        // input. The next game tick can retire its input preprocessor.
                        // No field edit or action has been attempted, and reply deadlines
                        // stay with the driver that submitted the command.
                        break;
                    }
                    Event->SetNumberField(TEXT("id"), Command->GetNumberField(TEXT("id")));
                    Ok = Execute(Command);
                }
                Consumed = Index + 1;
                Event->SetBoolField(TEXT("ok"), Ok);
                Emit(Event);
            }
        if (Consumed)
            PendingCommandBytes.RemoveAt(0, Consumed, EAllowShrinking::No);
    }

    if (!PendingExportBytes.IsEmpty() &&
        RoomReplayTestCatalog::Record(GetGameInstance()->GetSubsystem<USpectatorRoom>(),
                                      PendingExportBytes, BeforeExportIds))
    {
        FRoomMatch ExportedMatch;
        USpectatorRoom::ReadReplay(PendingExportBytes, ExportedMatch);
        auto Exported = MakeShared<FJsonObject>();
        Exported->SetStringField(TEXT("type"), TEXT("public-export-recorded"));
        Exported->SetStringField(TEXT("match"), ExportedMatch.Id);
        PendingExportBytes.Empty();
        Emit(Exported);
    }
    if (FPlatformTime::Seconds() >= Heartbeat)
    {
        Heartbeat = FPlatformTime::Seconds() + .1;
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("type"), TEXT("heartbeat"));
        auto *Driver = GetWorld()->GetNetDriver();
        Event->SetNumberField(TEXT("connections"), Driver ? Driver->ClientConnections.Num() : 0);
        Event->SetBoolField(TEXT("server_connection"), Driver && Driver->ServerConnection);
        Emit(Event);
    }
}

ARoomWorkerController::ARoomWorkerController()
{
    if (!GetDefaultSubobjectByName(TEXT("RoomConnection")))
    {
        CreateDefaultSubobject<URoomConnection>(TEXT("RoomConnection"));
    }
}

bool URoomWorker::EnsureWidgetInput()
{
    if (!Connection)
        return false;
    if (!IsValid(AdapterPanel) || !AdapterInput.IsValid() || !AdapterInput->GetWindow().IsValid())
    {
        AdapterPanel = IsValid(Connection->Panel) ? Connection->Panel.Get() :
            CreateWidget<URoomPanel>(GetGameInstance(), URoomPanel::StaticClass());
        if (!AdapterPanel)
            return false;
        AdapterPanel->Connection = Connection;
        AdapterInput = MakeShared<RoomWidgetTestAccess::FPanelHost>();
        if (!AdapterInput->Mount(AdapterPanel))
            return false;
    }
    return AdapterInput.IsValid() && AdapterInput->GetWindow().IsValid();
}

bool URoomWorker::ExecuteWidgetCommand(const TSharedPtr<FJsonObject> &Command, USpectatorRoom *Room)
{
    if (!EnsureWidgetInput())
        return false;
    const TSharedPtr<FJsonObject> *Fields = nullptr;
    if (Command->TryGetObjectField(TEXT("fields"), Fields))
        for (const auto &Field : (*Fields)->Values)
        {
            auto *Input = AdapterPanel->GetWidgetFromName(FName(*Field.Key));
            if (!AdapterInput.IsValid() || !AdapterInput->Enter(Input, Field.Value->AsString()))
                return false;
        }
    auto *Button =
        AdapterPanel->GetWidgetFromName(FName(*(TEXT("Action_") + Command->GetStringField(TEXT("action")))));
    if (!AdapterInput.IsValid() || !AdapterInput->Click(Button))
        return false;
    auto Event = MakeShared<FJsonObject>();
    Event->SetStringField(TEXT("type"), TEXT("widget"));
    Event->SetStringField(TEXT("action"), Command->GetStringField(TEXT("action")));
    if (auto *Label = AdapterPanel->GetWidgetFromName(TEXT("RoomStatus")))
    {
        Event->SetBoolField(TEXT("status_visible"), Label->IsVisible());
        Event->SetBoolField(TEXT("status_has_reflected_getter"), Label->FindFunction(TEXT("GetText")) != nullptr);
        Event->SetStringField(TEXT("text"), RoomWidgetTestAccess::GetText(Label).ToString());
    }
    if (!AdapterInput->Reveal(AdapterPanel->GetWidgetFromName(TEXT("RoomStatus"))))
        return false;
    FString Image;
    if (!RoomWidgetCapture::SaveWindow(AdapterInput->GetWindow(), Image))
        return false;
    Event->SetStringField(TEXT("image"), Image);
    Emit(Event);
    return true;
}

bool URoomWorker::LoadFixtureContent(const TSharedPtr<FJsonObject> &Command, USpectatorRoom *Room)
{
    auto *GameInstance = Cast<UNightSkyGameInstance>(GetGameInstance());
    GameInstance->BattleVersion = TEXT("RoomFixture-1");
    FBattleData Configuration{};
    Configuration.ColorIndicesP1 = {0};
    Configuration.ColorIndicesP2 = {0};
    int32 Seed = 9009;
    FParse::Value(FCommandLine::Get(), TEXT("RoomBattleSeed="), Seed);
    Configuration.Random.Reseed(Seed);
    GameInstance->FighterRunner = LocalPlay;
    bool Training = true, Heavy = false, Alternate = false;
    Command->TryGetBoolField(TEXT("training"), Training);
    Command->TryGetBoolField(TEXT("heavy"), Heavy);
    Command->TryGetBoolField(TEXT("alternate_map"), Alternate);
    GameInstance->IsTraining = Training;
    double Timer = 1;
    Command->TryGetNumberField(TEXT("round_seconds"), Timer);
    if (!Training)
    {
        Configuration.StartRoundTimer = FMath::Clamp(int32(Timer), 1, 99);
        Configuration.RoundCount = 1;
        Configuration.TimeUntilRoundStart = 0;
    }
    FString Root;
    if (!Command->TryGetStringField(TEXT("package"), Root) ||
        !FPackageName::IsValidLongPackageName(Root + TEXT("Character")))
        return false;
    const FString Map = Alternate ? Root + TEXT("Arena") : TEXT("/Engine/Maps/Entry");
    // Saved content must construct the same native battle independently of the
    // worker process's command-line game mode, including private replay worlds.
    const FString StageURL = Map + TEXT("?game=/Script/NightSkyEngine.ReplayFixtureRoomGameMode");
    if (Alternate && GetWorld()->GetNetMode() != NM_Client && !FPackageName::DoesPackageExist(Map))
    {
        auto *Package = CreatePackage(*Map);
        // Defer runtime initialization until normal map loading. An initialized
        // Inactive world has no level collections for Unreal's actor tick loop.
        auto *Arena = UWorld::CreateWorld(EWorldType::Inactive,
                                         false,
                                         FName(TEXT("Arena")),
                                         Package,
                                         true,
                                         ERHIFeatureLevel::Num,
                                         nullptr,
                                         true);
        Arena->SetFlags(RF_Public | RF_Standalone);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        const FString File =
            FPackageName::LongPackageNameToFilename(Map, FPackageName::GetMapPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        if (!UPackage::SavePackage(Package, Arena, *File, Args))
            return false;
    }
    auto Load = [&](UClass *Class, const FString &Name) -> UObject *
    {
        const FString PackageName = Root + Name, ObjectPath = PackageName + TEXT(".") + Name;
        if (FPackageName::DoesPackageExist(PackageName) || GetWorld()->GetNetMode() == NM_Client)
            return LoadObject<UObject>(nullptr, *ObjectPath);
        auto *Package = CreatePackage(*PackageName);
        auto *Asset = NewObject<UObject>(Package, Class, *Name, RF_Public | RF_Standalone);
        if (auto *Chara = Cast<UPrimaryCharaData>(Asset))
            Chara->PlayerClass =
                Heavy ? AReplayFixtureHeavyFighter::StaticClass() : AReplayFixtureFighter::StaticClass();
        if (auto *Stage = Cast<UPrimaryStageData>(Asset))
            Stage->StageURL = StageURL;
        const FString File =
            FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Package, Asset, *File, Args) ? Asset : nullptr;
    };
    auto *Chara = Cast<UPrimaryCharaData>(Load(UPrimaryCharaData::StaticClass(), TEXT("Character")));
    auto *Stage = Cast<UPrimaryStageData>(Load(UPrimaryStageData::StaticClass(), TEXT("Stage")));
    const auto ExpectedClass = Heavy ? AReplayFixtureHeavyFighter::StaticClass() : AReplayFixtureFighter::StaticClass();
    if (!Chara || !Stage || Chara->PlayerClass.Get() != ExpectedClass || Stage->StageURL != StageURL)
        return false;
    Configuration.PlayerListP1 = {Chara};
    Configuration.PlayerListP2 = {Chara};
    Configuration.Stage = Stage;
    // A new fixture-content request invalidates earlier preparation provenance.
    // A later product loading callback does not execute this path.
    ++PreparationSerial;
    PreparedBattleForStart = false;
    PreparingBattleForStart = false;
    PreparedBattle.Reset();
    PreparedWorld.Reset();
    AuthoredAssets.AddUnique(Chara);
    AuthoredAssets.AddUnique(Stage);
    AuthoredConfiguration = Configuration;
    AuthoredTraining = Training;
    AuthoredContent = USpectatorRoom::InspectContent(Configuration, GameInstance->BattleVersion);
    HasAuthoredConfiguration = !AuthoredContent.IsEmpty();
    GameInstance->BattleData = Configuration;
    Content.Empty();
    for (const auto &Entry : AuthoredContent)
    {
        Content.Add(Entry.Key);
        Content.Add(Entry.Value);
    }
    return HasAuthoredConfiguration;
}

bool URoomWorker::InspectSavedReplay(const TSharedPtr<FJsonObject> &Command, USpectatorRoom *Room)
{
    const FString Id = Command->GetStringField(TEXT("match"));
    FString ReplayId;
    TArray<uint8> Bytes;
    FRoomMatch Replay;
    if (!RoomReplayTestCatalog::Load(Id, ReplayId, Bytes) ||
        !Room->SavedReplays().Contains(ReplayId) || !USpectatorRoom::ReadReplay(Bytes, Replay))
        return false;
    auto Event = MakeShared<FJsonObject>();
    Event->SetStringField(TEXT("type"), TEXT("artifact"));
    Event->SetStringField(TEXT("match"), Replay.Id);
    Event->SetStringField(TEXT("outcome"), Replay.Outcome);
    Event->SetNumberField(TEXT("frames"), Replay.Frames.Num());
    Event->SetNumberField(TEXT("participants"), Replay.Participants.Num());
    Event->SetNumberField(TEXT("seed"), Replay.Configuration.Random.GetSeed());
    Emit(Event);
    return true;
}

bool URoomWorker::AdvanceSavedReplay(const TSharedPtr<FJsonObject> &Command, USpectatorRoom *Room)
{
    if (!OfflineReplayReady || !IsValid(Battle) || Battle->GetWorld() != GetWorld() ||
        !Cast<UNightSkyGameInstance>(GetGameInstance())->IsReplay || !GetWorld()->NextURL.IsEmpty() ||
        !GEngine->GetWorldContextFromWorldChecked(GetWorld()).TravelURL.IsEmpty())
        return false;
    Battle->Tick(OneFrame);
    auto Event = MakeShared<FJsonObject>();
    Event->SetStringField(TEXT("type"), TEXT("offline-frame"));
    Event->SetNumberField(TEXT("frame"), Battle->LocalFrame);
    Event->SetNumberField(TEXT("x"), Battle->GetMainPlayer(true)->PosX);
    Event->SetNumberField(TEXT("health"), Battle->GetMainPlayer(false)->CurrentHealth);
    Event->SetNumberField(TEXT("seed"),
                          Cast<UNightSkyGameInstance>(GetGameInstance())->BattleData.Random.GetSeed());
    Event->SetNumberField(TEXT("p2x"), Battle->GetMainPlayer(false)->PosX);
    Event->SetNumberField(TEXT("p1health"), Battle->GetMainPlayer(true)->CurrentHealth);
    Event->SetNumberField(TEXT("input1"), Battle->GetMainPlayer(true)->Inputs);
    Event->SetNumberField(TEXT("input2"), Battle->GetMainPlayer(false)->Inputs);
    Event->SetNumberField(TEXT("timer"), Battle->BattleState.RoundTimer);
    Event->SetNumberField(TEXT("rng"), Battle->BattleState.RandomManager.GetSeed());
    Event->SetNumberField(TEXT("meter1"), Battle->BattleState.Meter[0]);
    Event->SetNumberField(TEXT("meter2"), Battle->BattleState.Meter[1]);
    Event->SetNumberField(TEXT("phase"), int32(Battle->BattleState.BattlePhase));
    Emit(Event);
    return true;
}

bool URoomWorker::SendCorruptedHistory(const TSharedPtr<FJsonObject> &Command, USpectatorRoom *Room)
{
    // Damage opaque released history, then use the normal owning transport.
    auto *Driver = GetWorld()->GetNetDriver();
    const int32 Index = int32(Command->GetNumberField(TEXT("client_index")));
    if (GetWorld()->GetNetMode() == NM_Client || !Driver || !Driver->ClientConnections.IsValidIndex(Index))
        return false;
    auto *Controller = Driver->ClientConnections[Index]->PlayerController.Get();
    auto *Recipient = Controller ? Controller->FindComponentByClass<URoomConnection>() : nullptr;
    if (!Recipient)
        return false;
    FRoomDelivery Delivery = Room->Observe(Command->GetStringField(TEXT("identity")));
    if (Delivery.Frame < 0 || Delivery.Gameplay.State.IsEmpty())
        return false;
    Delivery.RequestNonce = Command->GetStringField(TEXT("nonce"));
    Delivery.Gameplay.State.Last() ^= 1;
    Recipient->SendDelivery(Delivery);
    auto Event = MakeShared<FJsonObject>();
    Event->SetStringField(TEXT("type"), TEXT("corrupted-transfer"));
    Event->SetStringField(TEXT("nonce"), Delivery.RequestNonce);
    Event->SetNumberField(TEXT("bytes"), Delivery.Gameplay.State.Num());
    Emit(Event);
    return true;
}

bool URoomWorker::ExecuteRoomCommand(const TSharedPtr<FJsonObject> &Command, USpectatorRoom *Room)
{
    FRoomCommand Request;
    Request.Operation = Command->GetStringField(TEXT("operation"));
    if (Request.Operation == TEXT("export"))
        BeforeExportIds = Room->SavedReplays();
    Command->TryGetStringField(TEXT("value"), Request.Value);
    Command->TryGetStringField(TEXT("membership"), Request.Membership);
    Command->TryGetStringField(TEXT("match"), Request.Match);
    Command->TryGetStringField(TEXT("assignment"), Request.Assignment);
    Command->TryGetStringField(TEXT("nonce"), Request.Nonce);
    double Number = 0;
    Command->TryGetNumberField(TEXT("number"), Number);
    Request.Number = int32(Number);
    if (Request.Operation == TEXT("start"))
        EmitStartPreparationDiagnostic(TEXT("before-start"), Request.Nonce);
    if (Request.Operation == TEXT("start") && AwaitingPublicStart)
    {
        // An already-active match cannot stand in for this expected fresh start.
        if (Room->IsActive())
            return false;
        PublicStartRequested = true;
    }
    if (GetWorld()->GetNetMode() != NM_Client && (!Connection || Connection->LastDelivery.Room.IsEmpty()))
        Audit(Room->Command(TEXT("host"), Request));
    else if (Connection)
        Connection->Submit(Request);
    else
        return false;
    // Synchronous success enters driver control before this command returns and
    // before another native actor simulation step can run.
    UpdateManualBattleControl();
    if (Request.Operation == TEXT("start"))
        EmitStartPreparationDiagnostic(TEXT("after-start"), Request.Nonce);
    return true;
}

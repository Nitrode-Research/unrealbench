#if WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../Relay/RelayArena.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NightSkyEngine/Miscellaneous/ReplayInfo.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayArenaRosterReplay, "NightSky.Relay.Arena.SelectedRosterSavedReplay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRelayArenaRosterReplay::RunTest(const FString&)
{
    auto* Instance = NewObject<URelayFixtureGameInstance>(GEngine);
    Instance->InitializeStandalone();
    auto* Settings = Instance->GetSubsystem<URelayArenaSession>();
    if (!TestNotNull(TEXT("Arena selection subsystem exists for the game instance"), Settings)) return false;
    Settings->Roster = {3,1,2,2,3,1};
    Settings->ApplyRoster();
    UWorld* World = Instance->GetWorld();
    FURL URL;
    URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL);
    World->InitializeActorsForPlay(URL);
    World->BeginPlay();
    World->SpawnActor<ANightSkyPlayerController>();
    World->SpawnActor<ANightSkyPlayerController>();
    auto* Game = World->SpawnActor<ARelayFixtureBattle>();
    const int32 ExpectedHealth[] = {9000,10000,12000,12000,9000,10000};
    for (int32 Index=0; Index<6; ++Index)
    {
        TestEqual(TEXT("Selection order determines immutable fighter identity"), Game->Players[Index]->MaxHealth, ExpectedHealth[Index]);
        Game->Players[Index]->PosX = Index<3 ? -1000000 : 1000000;
    }
    for (int32 Frame=0; Frame<45; ++Frame)
        Game->UpdateGameState(Frame==0 ? RelaySlot2 : Frame==20 ? RelaySlot3 : 0, 0, false);
    TestEqual(TEXT("The selected follow-up becomes main"),Game->GetMainPlayer(true)->TeamIndex,2);
    TestEqual(TEXT("Live recording contains every effective input pair"),Instance->GetCurrentReplay()->LengthInFrames,45);
    Settings->SaveReplay();
    const FString Slot = Settings->SavedReplay;
    TestTrue(TEXT("Arena replay is persisted on disk"),UGameplayStatics::DoesSaveGameExist(Slot,0));
    auto* Loaded=Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(Slot,0));
    if (TestNotNull(TEXT("Saved replay loads as a typed replay"),Loaded))
    {
        TestEqual(TEXT("Replay retains native arena stage identity"),Loaded->BattleData.Stage,static_cast<UPrimaryStageData*>(GetMutableDefault<URelayArenaStage>()));
        TestEqual(TEXT("Replay retains selection order"),Loaded->BattleData.PlayerListP1[0].Get(),static_cast<UPrimaryCharaData*>(GetMutableDefault<URelaySampleThree>()));
        TestEqual(TEXT("Relay entry edge is recorded"),Loaded->InputsP1[0],int32(RelaySlot2));
        TestEqual(TEXT("Route edge is recorded"),Loaded->InputsP1[20],int32(RelaySlot3));
    }
    UGameplayStatics::DeleteGameInSlot(Slot,0);
    Instance->IsReplay=true;
    World->EndPlay(EEndPlayReason::Quit);
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    Instance->Shutdown();
    return true;
}

#endif // WITH_EDITOR: editor automation is excluded from packaged game targets.

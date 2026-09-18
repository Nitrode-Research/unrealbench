#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"

#include "NightSkyEngine/Fixtures/RelayFixture.h"
#include "NightSkyEngine/Fixtures/RelaySample.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "NightSkyEngine/Data/CollisionData.h"
#include "Engine/Engine.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
namespace RelayTests
{
    int32 GetCooldown(const ANightSkyGameState *Game, int32 TeamIndex, int32 SlotIndex)
    {
        const auto Slots = Game->GetRelaySlots(TeamIndex == 0);
        return Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex].Cooldown : -1;
    }

    FString SemanticSource(const ABattleObject *Object)
    {
        if (!Object)
        {
            return TEXT("none");
        }
        const auto Owner = Object->IsPlayer ? Cast<APlayerObject>(Object) : Object->Player;
        if (!Owner)
        {
            return TEXT("expired-projectile");
        }
        return FString::Printf(
            TEXT("%s:%d:%d:%s"), Object->IsPlayer ? TEXT("fighter") : TEXT("projectile"),
            Owner->PlayerIndex, Owner->TeamIndex,
            Object->IsPlayer || !Object->ObjectState ? TEXT("")
                                                     : *Object->ObjectState->Name.ToString());
    }
    FString SemanticContact(const APlayerObject *Player)
    {
        return Player->Hitstop > 0 || (Player->PlayerFlags & PLF_IsThrowLock)
                   ? SemanticSource(Player->AttackOwner)
                   : TEXT("none");
    }
    FString SemanticProjectiles(ANightSkyGameState *Game)
    {
        TArray<FString> Entries;
        for (auto Object : Game->Objects)
        {
            if (Object->IsActive)
            {
                Entries.Add(FString::Printf(TEXT("object:%s:%d:%d:%d:%d:%d:%d:%u"),
                                            *SemanticSource(Object), Object->PosX, Object->PosY,
                                            Object->SpeedX, Object->SpeedY, Object->ActionTime,
                                            Object->Hitstop, Object->AttackFlags));
            }
        }
        Entries.Sort();
        return FString::Join(Entries, TEXT("|"));
    }

    struct FEncounter
    {
        UWorld *World = nullptr;
        URelayFixtureGameInstance *Instance = nullptr;
        ARelayFixtureBattle *Game = nullptr;
        FEncounter(int32 RosterSeed = 0)
        {
            Instance = NewObject<URelayFixtureGameInstance>(GEngine);
            if (!Instance)
            {
                return;
            }
            Instance->InitializeStandalone();
            Instance->IsTraining = true;
            Instance->BattleData.BattleFormat = EBattleFormat::Relay;
            Instance->BattleData.StartRoundTimer = 999;
            // InitializeStandalone and candidate fixture code may populate BattleData. This
            // encounter owns its roster, so establish the exact 3v3 fixture rather than
            // appending to state left by initialization.
            Instance->BattleData.PlayerListP1.Reset();
            Instance->BattleData.PlayerListP2.Reset();
            UPrimaryCharaData *Assets[3] = {GetMutableDefault<URelaySampleOne>(),
                                            GetMutableDefault<URelaySampleTwo>(),
                                            GetMutableDefault<URelaySampleThree>()};
            FRandomStream RosterRandom(RosterSeed);
            for (int Team = 0; Team < 2; ++Team)
            {
                int Order[3] = {0, 1, 2};
                if (RosterSeed)
                {
                    for (int Index = 2; Index > 0; --Index)
                    {
                        Swap(Order[Index], Order[RosterRandom.RandRange(0, Index)]);
                    }
                }
                for (int Index : Order)
                {
                    (Team == 0 ? Instance->BattleData.PlayerListP1
                               : Instance->BattleData.PlayerListP2)
                        .Add(Assets[Index]);
                }
            }
            World = Instance->GetWorld();
            if (!World)
            {
                return;
            }
            FURL URL;
            URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
            World->SetGameMode(URL);
            World->InitializeActorsForPlay(URL);
            World->BeginPlay();
            World->SpawnActor<ANightSkyPlayerController>();
            World->SpawnActor<ANightSkyPlayerController>();
            Game = World->SpawnActor<ARelayFixtureBattle>();
            // Public prematch positioning keeps this timing encounter outside attack range.
            if (Game)
            {
                for (auto Fighter : Game->Players)
                {
                    if (Fighter)
                    {
                        Fighter->PosX = Fighter->PlayerIndex == 0 ? -1000000 : 1000000;
                    }
                }
            }
        }
        ~FEncounter()
        {
            if (Instance)
            {
                Instance->IsReplay = true;
            }
            if (World)
            {
                World->EndPlay(EEndPlayReason::Quit);
                World->DestroyWorld(false);
                GEngine->DestroyWorldContext(World);
            }
            if (Instance)
            {
                Instance->Shutdown();
            }
        }
        FString ValidationError(const TCHAR *Label) const
        {
            if (!Instance || !World || !Game)
            {
                return FString::Printf(TEXT("%s fixture failed to create its encounter"), Label);
            }
            if (Game->Players.Num() != 6)
            {
                return FString::Printf(TEXT("%s fixture created %d fighters; expected exactly six"),
                                       Label, Game->Players.Num());
            }
            for (int32 PlayerIndex = 0; PlayerIndex < Game->Players.Num(); ++PlayerIndex)
            {
                if (!Game->Players[PlayerIndex])
                {
                    return FString::Printf(TEXT("%s fixture fighter %d is null"), Label,
                                           PlayerIndex);
                }
            }
            return FString();
        }
        bool Validate(FAutomationTestBase &Test, const TCHAR *Label) const
        {
            const FString Error = ValidationError(Label);
            if (!Error.IsEmpty())
            {
                Test.AddError(Error);
                return false;
            }
            return true;
        }
        void Frame(int32 A = 0, int32 B = 0)
        {
            Game->UpdateGameState(A, B, false);
        }
        APlayerObject *GetFighter(int32 TeamIndex, int32 Index) const
        {
            return Game->Players[TeamIndex * 3 + Index];
        }
    };
} // namespace RelayTests

#define RELAY_REQUIRE_VALID_ENCOUNTER(Encounter)                                               \
    do                                                                                          \
    {                                                                                           \
        if (!(Encounter).Validate(*this, TEXT(#Encounter)))                                    \
        {                                                                                       \
            return false;                                                                       \
        }                                                                                       \
    } while (false)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayExactBoundary, "NightSky.Relay.ExactBoundaries",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayExactBoundary::RunTest(const FString &)
{
    for (int32 RouteAt : {-1, 18, 27})
    {
        RelayTests::FEncounter Encounter;
        RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
        const int32 End = RouteAt < 0 ? 34 : RouteAt == 18 ? 37 : 46;
        int32 BeforeControl[3] = {};
        for (int32 FrameIndex = 0; FrameIndex <= End + 1; ++FrameIndex)
        {
            Encounter.Frame(FrameIndex == 0         ? RelaySlot2
                            : FrameIndex == RouteAt ? RelaySlot3
                            : FrameIndex == End + 1 ? INP_Right
                                                    : 0);
            const auto Status = Encounter.Game->GetRelayStatus(true);
            if (FrameIndex == 0)
            {
                TestEqual(TEXT("acceptance age zero"), Status.ElapsedFrames, 0);
                TestTrue(TEXT("reserve exposed"), Encounter.GetFighter(0, 1)->IsOnScreen());
            }
            if (FrameIndex == 6)
            {
                TestTrue(TEXT("synchronized starts at six"),
                         Status.Stage == ERelayPhase::Synchronized && Status.ElapsedFrames == 0);
                const auto Machine = FGameplayTag::RequestGameplayTag(TEXT("StateMachine.Primary"));
                const auto Sync =
                    FGameplayTag::RequestGameplayTag(TEXT("State.Relay.Synchronized"));
                TestEqual(TEXT("main executes authored synchronized state"),
                          Encounter.GetFighter(0, 0)->GetCurrentStateName(Machine), Sync);
                TestEqual(TEXT("reserve executes authored synchronized state"),
                          Encounter.GetFighter(0, 1)->GetCurrentStateName(Machine), Sync);
            }
            if (FrameIndex == 18)
            {
                TestTrue(TEXT("route opens at eighteen"),
                         Status.Stage == ERelayPhase::Route && Status.ElapsedFrames == 0);
            }
            if (FrameIndex == RouteAt + 1 && RouteAt >= 0)
            {
                TestTrue(TEXT("followup on next frame"),
                         Status.Stage == ERelayPhase::Followup && Status.ElapsedFrames == 0);
            }
            TestEqual(TEXT("single payment"), Status.Resource, 100);
            if (FrameIndex < End)
            {
                TestTrue(TEXT("main retained until termination"),
                         Encounter.GetFighter(0, 0)->IsMainPlayer());
            }
            if (FrameIndex == End)
            {
                TestEqual(TEXT("cooldown at termination"),
                          RelayTests::GetCooldown(Encounter.Game, 0, 1), 120);
                TestTrue(TEXT("finished"), Status.Stage == ERelayPhase::Idle);
                TestTrue(TEXT("correct main"),
                         Encounter.GetFighter(0, RouteAt < 0 ? 0 : 2)->IsMainPlayer());
            }
            if (FrameIndex == End)
            {
                for (int Index = 0; Index < 3; ++Index)
                {
                    BeforeControl[Index] = Encounter.GetFighter(0, Index)->PosX;
                }
            }
            if (FrameIndex == End + 1)
            {
                TestEqual(TEXT("cooldown starts next frame"),
                          RelayTests::GetCooldown(Encounter.Game, 0, 1), 119);
                const int Main = RouteAt < 0 ? 0 : 2;
                for (int Index = 0; Index < 3; ++Index)
                {
                    TestEqual(TEXT("only promoted main receives next ordinary input"),
                              Encounter.GetFighter(0, Index)->PosX,
                              BeforeControl[Index] + (Index == Main ? 1000 : 0));
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayIndependentTeams, "NightSky.Relay.IndependentTeams",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayIndependentTeams::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
    for (int32 FrameIndex = 0; FrameIndex <= 50; ++FrameIndex)
    {
        Encounter.Frame(FrameIndex == 0    ? RelaySlot2
                        : FrameIndex == 18 ? RelaySlot3
                                           : 0,
                        FrameIndex == 4    ? RelaySlot3
                        : FrameIndex == 31 ? RelaySlot3
                                           : 0);
        TestEqual(TEXT("independent A payment each frame"),
                  Encounter.Game->GetRelayStatus(true).Resource, 100);
        TestEqual(TEXT("independent B payment each frame"),
                  Encounter.Game->GetRelayStatus(false).Resource, FrameIndex < 4 ? 200 : 100);
        if (FrameIndex == 4)
        {
            TestEqual(TEXT("A age"), Encounter.Game->GetRelayStatus(true).ElapsedFrames, 4);
            TestEqual(TEXT("B age"), Encounter.Game->GetRelayStatus(false).ElapsedFrames, 0);
        }
        if (FrameIndex == 37)
        {
            TestTrue(TEXT("A promotes independently"), Encounter.GetFighter(0, 2)->IsMainPlayer());
            TestTrue(TEXT("B retains main until own exit"),
                     Encounter.GetFighter(1, 0)->IsMainPlayer());
        }
    }
    TestTrue(TEXT("B participant routed"), Encounter.GetFighter(1, 2)->IsMainPlayer());
    TestEqual(TEXT("A cooldown"), RelayTests::GetCooldown(Encounter.Game, 0, 1), 107);
    TestEqual(TEXT("B cooldown"), RelayTests::GetCooldown(Encounter.Game, 1, 2), 120);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayFreezeEdges, "NightSky.Relay.FreezeEdgesAndRecovery",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayFreezeEdges::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
    Encounter.GetFighter(0, 2)->CurrentHealth = 100;
    Encounter.GetFighter(0, 2)->RecoverableHealth = 20;
    Encounter.Game->BattleState.SuperFreezeDuration = 8;
    for (int32 Index = 0; Index < 8; ++Index)
    {
        Encounter.Frame(Index == 0 ? RelaySlot1 : Index >= 2 ? RelaySlot2 : 0);
    }
    TestEqual(TEXT("no frozen recovery"), Encounter.GetFighter(0, 2)->CurrentHealth, 100);
    TestEqual(TEXT("no frozen payment"), Encounter.Game->GetRelayStatus(true).Resource, 200);
    Encounter.Game->BattleState.SuperFreezeDuration = 0;
    Encounter.Frame(RelaySlot2);
    TestEqual(TEXT("valid queued edge after invalid edge"),
              Encounter.Game->GetRelayStatus(true).Resource, 100);
    TestEqual(TEXT("recovery resumes"), Encounter.GetFighter(0, 2)->CurrentHealth, 101);
    for (int32 Index = 0; Index < 200; ++Index)
    {
        Encounter.Frame(RelaySlot2);
    }
    TestEqual(TEXT("held input cannot spend again"), Encounter.Game->GetRelayStatus(true).Resource,
              100);
    TestEqual(TEXT("recovery consumes pool"), Encounter.GetFighter(0, 2)->CurrentHealth, 120);
    // Exercise the documented player-facing restart; restoration may be shared
    // with RoundInit rather than implemented by any particular relay helper.
    Encounter.Frame(INP_ResetTraining | RelaySlot2);
    TestEqual(TEXT("reset restores damaged roster to full health"),
              Encounter.GetFighter(0, 2)->CurrentHealth, Encounter.GetFighter(0, 2)->MaxHealth);
    Encounter.Frame(RelaySlot2);
    TestEqual(TEXT("reset suppresses held edge"), Encounter.Game->GetRelayStatus(true).Resource,
              200);
    Encounter.Frame();
    Encounter.Frame(RelaySlot2);
    TestEqual(TEXT("fresh edge works after reset"), Encounter.Game->GetRelayStatus(true).Resource,
              100);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelaySeededLifecycle, "NightSky.Relay.SeededLifecycle",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelaySeededLifecycle::RunTest(const FString &)
{
    // Independent deadline model: all expectations use acceptance and route frame numbers.
    int32 FirstSeed = 39001, SeedCount = 32;
    FParse::Value(FCommandLine::Get(), TEXT("RelaySeedStart="), FirstSeed);
    FParse::Value(FCommandLine::Get(), TEXT("RelaySeedCount="), SeedCount);
    SeedCount = FMath::Clamp(SeedCount, 1, 128);
    FString EpisodeDirectory = FPaths::ProjectSavedDir() / TEXT("RelayEpisodes");
    FParse::Value(FCommandLine::Get(), TEXT("RelayEpisodeDir="), EpisodeDirectory);
    IFileManager::Get().MakeDirectory(*EpisodeDirectory, true);
    for (int32 Seed = FirstSeed; Seed < FirstSeed + SeedCount; ++Seed)
    {
        RelayTests::FEncounter Encounter(Seed);
        RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
        Encounter.Game->BattleState.RoundStartPos = 1000000;
        FRandomStream Random(Seed);
        FString Episode = TEXT(
            "frame,input_a,input_b,team,slot,health,recoverable,visible,main,cooldown,resource\n");
        struct FSaveEpisode
        {
            FString &Data;
            FString Path;
            ~FSaveEpisode()
            {
                FFileHelper::SaveStringToFile(Data, *Path);
            }
        } Save{Episode, EpisodeDirectory / FString::Printf(TEXT("seed-%d.csv"), Seed)};
        struct FExpectedTeam
        {
            int Main = 0, Resource = 200, Start = -1, End = -1, Participant = -1, Route = -1;
            int Cooldown[3] = {}, Health[3] = {}, Recovery[3] = {}, Maximum[3] = {};
            int Previous = 0;
        } ExpectedTeams[2];
        FString Identities[2][3];
        for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
        {
            auto &ExpectedTeam = ExpectedTeams[TeamIndex];
            const int Resources[3] = {99, 100, 200};
            ExpectedTeam.Resource = Resources[Random.RandRange(0, 2)];
            Encounter.Game->SetInitialRelayResource(TeamIndex == 0, ExpectedTeam.Resource);
            for (int Index = 0; Index < 3; ++Index)
            {
                ExpectedTeam.Maximum[Index] = Encounter.GetFighter(TeamIndex, Index)->MaxHealth;
                ExpectedTeam.Health[Index] =
                    Random.RandRange(Index == 0 ? 1 : 0, ExpectedTeam.Maximum[Index]);
                if (Index > 0 && Random.RandRange(0, 3) == 0)
                {
                    ExpectedTeam.Health[Index] = Random.RandRange(0, 1);
                }
                ExpectedTeam.Recovery[Index] =
                    ExpectedTeam.Health[Index] > 0
                        ? Random.RandRange(0,
                                           ExpectedTeam.Maximum[Index] - ExpectedTeam.Health[Index])
                        : 0;
                Encounter.GetFighter(TeamIndex, Index)->SetHealth(ExpectedTeam.Health[Index]);
                Encounter.GetFighter(TeamIndex, Index)
                    ->SetRecoverableHealth(ExpectedTeam.Recovery[Index]);
                Identities[TeamIndex][Index] =
                    (TeamIndex == 0 ? Encounter.Instance->BattleData.PlayerListP1
                                    : Encounter.Instance->BattleData.PlayerListP2)[Index]
                        ->CharaName.ToString();
            }
        }
        for (int32 FrameIndex = 0; FrameIndex < 600; ++FrameIndex)
        {
            int Inputs[2] = {};
            bool VisibleBefore[2][3] = {};
            const bool ResetEpisode = FrameIndex == 200 || FrameIndex == 400;
            if (ResetEpisode)
            {
                for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
                {
                    const auto Old = ExpectedTeams[TeamIndex];
                    ExpectedTeams[TeamIndex] = FExpectedTeam();
                    for (int Index = 0; Index < 3; ++Index)
                    {
                        ExpectedTeams[TeamIndex].Health[Index] =
                            ExpectedTeams[TeamIndex].Maximum[Index] = Old.Maximum[Index];
                    }
                }
            }
            for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
            {
                auto &ExpectedTeam = ExpectedTeams[TeamIndex];
                for (auto &Cooldown : ExpectedTeam.Cooldown)
                {
                    if (Cooldown > 0)
                    {
                        --Cooldown;
                    }
                }
                for (int Index = 0; Index < 3; ++Index)
                {
                    VisibleBefore[TeamIndex][Index] =
                        Index == ExpectedTeam.Main ||
                        (ExpectedTeam.Start >= 0 &&
                         (Index == ExpectedTeam.Participant || Index == ExpectedTeam.Route));
                }
                if (ExpectedTeam.End == FrameIndex)
                {
                    ExpectedTeam.Cooldown[ExpectedTeam.Main] = 120;
                    ExpectedTeam.Cooldown[ExpectedTeam.Participant] = 120;
                    if (ExpectedTeam.Route >= 0)
                    {
                        ExpectedTeam.Cooldown[ExpectedTeam.Route] = 120;
                        ExpectedTeam.Main = ExpectedTeam.Route;
                    }
                    ExpectedTeam.Start = -1;
                    ExpectedTeam.End = -1;
                }
                if (Random.RandRange(0, 6) == 0)
                {
                    Inputs[TeamIndex] = RelaySlot1 << Random.RandRange(0, 2);
                }
                if (Random.RandRange(0, 9) == 0)
                {
                    Inputs[TeamIndex] = RelaySlot2 | RelaySlot3;
                }
                if (ResetEpisode)
                {
                    Inputs[TeamIndex] = INP_ResetTraining;
                }
                int Edges = Inputs[TeamIndex] & ~ExpectedTeam.Previous;
                ExpectedTeam.Previous = Inputs[TeamIndex];
                for (int Index = 0; Index < 3; ++Index)
                {
                    if (!(Edges & (RelaySlot1 << Index)) || Index == ExpectedTeam.Main ||
                        ExpectedTeam.Health[Index] <= 0)
                    {
                        continue;
                    }
                    if (ExpectedTeam.Start < 0)
                    {
                        if (ExpectedTeam.Resource < 100 || ExpectedTeam.Cooldown[Index] > 0)
                        {
                            continue;
                        }
                        ExpectedTeam.Resource -= 100;
                        ExpectedTeam.Start = FrameIndex;
                        ExpectedTeam.End = FrameIndex + 34;
                        ExpectedTeam.Participant = Index;
                        ExpectedTeam.Route = -1;
                        break;
                    }
                    if (ExpectedTeam.Route < 0 && FrameIndex >= ExpectedTeam.Start + 18 &&
                        FrameIndex <= ExpectedTeam.Start + 27 &&
                        (Index == ExpectedTeam.Participant || ExpectedTeam.Cooldown[Index] == 0))
                    {
                        ExpectedTeam.Route = Index;
                        ExpectedTeam.End = FrameIndex + 19;
                        break;
                    }
                }
            }
            Encounter.Frame(Inputs[0], Inputs[1]);
            for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
            {
                const auto Views = Encounter.Game->GetRelaySlots(TeamIndex == 0);
                if (!TestEqual(FString::Printf(TEXT("seed %d frame %d team %d roster"), Seed,
                                               FrameIndex, TeamIndex),
                               Views.Num(), 3))
                {
                    return false;
                }
                auto &ExpectedTeam = ExpectedTeams[TeamIndex];
                for (const auto &SlotView : Views)
                {
                    Episode += FString::Printf(
                        TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n"), FrameIndex, Inputs[0],
                        Inputs[1], TeamIndex, SlotView.Slot, SlotView.Health, SlotView.Recoverable,
                        SlotView.Visible, SlotView.Main, SlotView.Cooldown,
                        Encounter.Game->GetRelayStatus(TeamIndex == 0).Resource);
                }
                if (!TestEqual(FString::Printf(TEXT("seed %d frame %d team %d resource ledger"),
                                               Seed, FrameIndex, TeamIndex),
                               Encounter.Game->GetRelayStatus(TeamIndex == 0).Resource,
                               ExpectedTeam.Resource))
                {
                    return false;
                }
                for (int Index = 0; Index < 3; ++Index)
                {
                    if (!TestEqual(TEXT("immutable randomized identity"), Views[Index].Identity,
                                   Identities[TeamIndex][Index]))
                    {
                        return false;
                    }
                    const bool Visible =
                        Index == ExpectedTeam.Main ||
                        (ExpectedTeam.Start >= 0 &&
                         (Index == ExpectedTeam.Participant || Index == ExpectedTeam.Route));
                    // Recovery ordering relative to an exposure transition is unspecified.
                    // Permit exactly zero or one conserved point on that frame; every
                    // stable off-screen/visible frame still has an exact expectation.
                    const int BeforeHealth = ExpectedTeam.Health[Index];
                    const int BeforeRecovery = ExpectedTeam.Recovery[Index];
                    const bool CanRecover = BeforeHealth > 0 && BeforeRecovery > 0 &&
                                            BeforeHealth < ExpectedTeam.Maximum[Index];
                    const bool Transition = VisibleBefore[TeamIndex][Index] != Visible;
                    const int Minimum = CanRecover && !Visible && !Transition ? 1 : 0;
                    const int Maximum = CanRecover && (!Visible || Transition) ? 1 : 0;
                    const int Recovered = Views[Index].Health - BeforeHealth;
                    if (!TestTrue(TEXT("recovery obeys exposure and one-point boundary"),
                                  Recovered >= Minimum && Recovered <= Maximum) ||
                        !TestEqual(TEXT("recovery consumes exactly the gained health"),
                                   Views[Index].Recoverable, BeforeRecovery - Recovered))
                    {
                        return false;
                    }
                    ExpectedTeam.Health[Index] = Views[Index].Health;
                    ExpectedTeam.Recovery[Index] = Views[Index].Recoverable;
                    if (!TestEqual(TEXT("independent exposure ledger"), Views[Index].Visible,
                                   Visible))
                    {
                        return false;
                    }
                    if (!TestEqual(TEXT("identity cooldown ledger"), Views[Index].Cooldown,
                                   ExpectedTeam.Cooldown[Index]))
                    {
                        return false;
                    }
                    if (!TestEqual(TEXT("main ledger"), Views[Index].Main,
                                   Index == ExpectedTeam.Main))
                    {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayTimeout, "NightSky.Relay.TeamTimeoutAndInputLock",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayTimeout::RunTest(const FString &)
{
    for (int PhaseFrame : {0, 6, 18, 19, 31})
    {
        for (bool Tie : {false, true})
        {
            RelayTests::FEncounter Encounter;
            RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
            for (int FrameIndex = 0; FrameIndex <= PhaseFrame; ++FrameIndex)
            {
                Encounter.Frame(FrameIndex == 0 ? RelaySlot2 : FrameIndex == 18 ? RelaySlot3 : 0);
            }
            // Team A has the greater absolute sum but the lower main HP and lower
            // sum of health fractions; equal absolute sums must still draw.
            Encounter.GetFighter(0, 0)->SetHealth(100);
            Encounter.GetFighter(0, 1)->SetHealth(6000);
            Encounter.GetFighter(0, 2)->SetHealth(100);
            Encounter.GetFighter(1, 0)->SetHealth(1000);
            Encounter.GetFighter(1, 1)->SetHealth(100);
            Encounter.GetFighter(1, 2)->SetHealth(Tie ? 5100 : 4900);
            Encounter.Game->BattleState.RoundTimer = 1;
            Encounter.Frame(RelaySlot3, RelaySlot2);
            TestTrue(TEXT("sum of team health wins timeout"),
                     Encounter.Game->BattleState.CurrentWinSide == (Tie ? WIN_Draw : WIN_P1));
            TestTrue(TEXT("relay cancelled by timeout"),
                     Encounter.Game->GetRelayStatus(true).Stage == ERelayPhase::Idle);
            int Resource = Encounter.Game->GetRelayStatus(false).Resource;
            Encounter.Frame();
            Encounter.Frame(RelaySlot2, RelaySlot2 | INP_A);
            TestEqual(TEXT("end locks resource"), Encounter.Game->GetRelayStatus(false).Resource,
                      Resource);
            Encounter.Frame(INP_Rematch | RelaySlot2, INP_Rematch | RelaySlot2);
            Encounter.Frame(RelaySlot2, RelaySlot2);
            TestTrue(TEXT("rematch clears result"),
                     Encounter.Game->BattleState.CurrentWinSide == WIN_None);
            for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
            {
                TestEqual(TEXT("held rematch edge cannot auto-charge"),
                          Encounter.Game->GetRelayStatus(TeamIndex == 0).Resource, 200);
                TestTrue(TEXT("rematch restores initial main"),
                         Encounter.GetFighter(TeamIndex, 0)->IsMainPlayer());
                for (int Index = 0; Index < 3; ++Index)
                {
                    TestEqual(TEXT("rematch restores full roster health"),
                              Encounter.GetFighter(TeamIndex, Index)->CurrentHealth,
                              Encounter.GetFighter(TeamIndex, Index)->MaxHealth);
                }
            }
            Encounter.Frame();
            Encounter.Frame(RelaySlot2, RelaySlot2);
            for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
            {
                TestEqual(TEXT("fresh post-rematch request succeeds"),
                          Encounter.Game->GetRelayStatus(TeamIndex == 0).Resource, 100);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayRealContact, "NightSky.Relay.StrikeInterruptsExposedReserve",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayRealContact::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
    for (auto Fighter : Encounter.Game->Players)
    {
        Fighter->PosX = Fighter->PlayerIndex == 0 ? -150000 : 150000;
    }
    int MainHealth = Encounter.GetFighter(0, 0)->CurrentHealth,
        ReserveHealth = Encounter.GetFighter(0, 1)->CurrentHealth;
    Encounter.Frame(RelaySlot2, INP_A);
    TestTrue(TEXT("accepted reserve is visible"), Encounter.GetFighter(0, 1)->IsOnScreen());
    for (int FrameIndex = 1; FrameIndex < 6; ++FrameIndex)
    {
        Encounter.Frame();
    }
    TestEqual(TEXT("ordinary strike main damage"), Encounter.GetFighter(0, 0)->CurrentHealth,
              MainHealth - 500);
    TestEqual(TEXT("same attack hits exposed reserve"), Encounter.GetFighter(0, 1)->CurrentHealth,
              ReserveHealth - 270);
    TestEqual(TEXT("reserve damage is fully recoverable"),
              Encounter.GetFighter(0, 1)->RecoverableHealth, 270);
    TestTrue(TEXT("reserve has real attack owner"),
             Encounter.GetFighter(0, 1)->AttackOwner == Encounter.GetFighter(1, 0));
    TestTrue(TEXT("contact cancels before synchronized move"),
             Encounter.Game->GetRelayStatus(true).Stage == ERelayPhase::Idle);
    TestTrue(TEXT("reaction remains on screen"), Encounter.GetFighter(0, 1)->IsOnScreen());
    TestEqual(TEXT("no refund"), Encounter.Game->GetRelayStatus(true).Resource, 100);
    TestTrue(TEXT("main remains selected"), Encounter.GetFighter(0, 0)->IsMainPlayer());
    for (int FrameIndex = 0; FrameIndex < 110; ++FrameIndex)
    {
        Encounter.Frame();
    }
    TestFalse(TEXT("reserve leaves after reaction"), Encounter.GetFighter(0, 1)->IsOnScreen());
    int X = Encounter.GetFighter(0, 0)->PosX;
    Encounter.Frame(INP_Right);
    TestTrue(TEXT("ordinary control restored"), Encounter.GetFighter(0, 0)->PosX > X);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayRollbackLifecycle, "NightSky.Relay.RollbackContinuation",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayRollbackLifecycle::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
    for (int FrameIndex = 0; FrameIndex < 18; ++FrameIndex)
    {
        Encounter.Frame(FrameIndex == 0 ? RelaySlot2 : 0, FrameIndex == 4 ? RelaySlot3 : 0);
    }
    FRollbackData Saved;
    int32 Checksum = 0;
    Encounter.Game->SaveGameState(Saved, &Checksum);
    struct FFrame
    {
        TArray<FRelaySlotView> A, B;
        int32 ResourceA, ResourceB, Timer;
        ERelayPhase PhaseA, PhaseB;
        int32 AgeA, AgeB;
    };
    TArray<FFrame> Ledger;
    for (int FrameIndex = 18; FrameIndex < 80; ++FrameIndex)
    {
        Encounter.Frame(FrameIndex == 18 ? RelaySlot3 : 0, FrameIndex == 31 ? RelaySlot3 : 0);
        const auto &A = Encounter.Game->GetRelayStatus(true);
        const auto &B = Encounter.Game->GetRelayStatus(false);
        Ledger.Add({Encounter.Game->GetRelaySlots(true), Encounter.Game->GetRelaySlots(false),
                    A.Resource, B.Resource, Encounter.Game->BattleState.RoundTimer, A.Stage,
                    B.Stage, A.ElapsedFrames, B.ElapsedFrames});
    }
    Encounter.Game->LoadGameState(Saved);
    for (int FrameIndex = 18; FrameIndex < 80; ++FrameIndex)
    {
        Encounter.Game->UpdateGameState(FrameIndex == 18 ? RelaySlot3 : 0,
                                        FrameIndex == 31 ? RelaySlot3 : 0, true);
        const auto &L = Ledger[FrameIndex - 18];
        for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
        {
            const auto V = Encounter.Game->GetRelaySlots(TeamIndex == 0);
            const auto &Expected = TeamIndex == 0 ? L.A : L.B;
            if (!TestEqual(TEXT("rollback roster"), V.Num(), 3))
            {
                return false;
            }
            for (int Index = 0; Index < 3; ++Index)
            {
                TestEqual(TEXT("rollback main"), V[Index].Main, Expected[Index].Main);
                TestEqual(TEXT("rollback visible"), V[Index].Visible, Expected[Index].Visible);
                TestEqual(TEXT("rollback health"), V[Index].Health, Expected[Index].Health);
                TestEqual(TEXT("rollback recovery"), V[Index].Recoverable,
                          Expected[Index].Recoverable);
                TestEqual(TEXT("rollback cooldown"), V[Index].Cooldown, Expected[Index].Cooldown);
            }
            const auto Status = Encounter.Game->GetRelayStatus(TeamIndex == 0);
            TestEqual(TEXT("rollback payment"), Status.Resource,
                      TeamIndex == 0 ? L.ResourceA : L.ResourceB);
            TestEqual(TEXT("rollback phase"), int32(Status.Stage),
                      int32(TeamIndex == 0 ? L.PhaseA : L.PhaseB));
            TestEqual(TEXT("rollback age"), Status.ElapsedFrames, TeamIndex == 0 ? L.AgeA : L.AgeB);
        }
        TestEqual(TEXT("rollback clock"), Encounter.Game->BattleState.RoundTimer, L.Timer);
    }
    Encounter.Game->LoadGameState(Saved);
    for (int FrameIndex = 18; FrameIndex < 40; ++FrameIndex)
    {
        Encounter.Game->UpdateGameState(FrameIndex == 18 ? RelaySlot2 : 0, 0, true);
    }
    TestTrue(TEXT("changed late route produces different main"),
             Encounter.GetFighter(0, 1)->IsMainPlayer());
    TestFalse(TEXT("old prediction does not survive correction"),
              Encounter.GetFighter(0, 2)->IsMainPlayer());
    // Reallocation follows two real histories: authored expiration versus a
    // speculative allocation discarded by the public checkpoint API.
    RelayTests::FEncounter Expired, Rewound;
    RELAY_REQUIRE_VALID_ENCOUNTER(Expired);
    RELAY_REQUIRE_VALID_ENCOUNTER(Rewound);
    auto Spawn = [](RelayTests::FEncounter &Scene) {
        auto State = NewObject<URelayFixtureProjectile>(Scene.GetFighter(0, 0));
        return Scene.Game->AddBattleObject(State, -1000000, 0, DIR_Right, 0, false,
                                           Scene.GetFighter(0, 0));
    };
    auto Old = Spawn(Expired);
    for (int FrameIndex = 0; FrameIndex < 92; ++FrameIndex)
    {
        Expired.Frame();
        Rewound.Frame();
    }
    TestFalse(TEXT("authored projectile expiration returns object to pool"), Old->IsActive);
    FRollbackData Empty;
    Rewound.Game->SaveGameState(Empty, &Checksum);
    auto Predicted = Spawn(Rewound);
    Rewound.Frame();
    TestTrue(TEXT("speculative allocation is active before restore"), Predicted->IsActive);
    Rewound.Game->LoadGameState(Empty);
    TestFalse(TEXT("restore removes speculative allocation"), Predicted->IsActive);
    auto Fresh = Spawn(Expired);
    auto Corrected = Spawn(Rewound);
    TestEqual(TEXT("reallocated projectile retains logical originating fighter"),
              RelayTests::SemanticSource(Fresh), RelayTests::SemanticSource(Corrected));
    TestEqual(TEXT("new allocation starts at authored frame zero"), Fresh->ActionTime, 0);
    TestEqual(TEXT("rollback history cannot alter new allocation action"), Corrected->ActionTime,
              Fresh->ActionTime);
    for (int FrameIndex = 0; FrameIndex < 5; ++FrameIndex)
    {
        Expired.Frame();
        Rewound.Frame();
        TestEqual(TEXT("reallocated projectile motion agrees"), Fresh->PosX, Corrected->PosX);
        TestEqual(TEXT("reallocated projectile action agrees"), Fresh->ActionTime,
                  Corrected->ActionTime);
        TestEqual(TEXT("authored projectile initializes real damage"), Fresh->NormalHit.Damage,
                  300);
    }
    // A consumed projectile hurt contact must remain consumed after speculative
    // expiration. This exercises the dynamic exclusion array outside raw ObjSync.
    RelayTests::FEncounter ContactScene;
    RELAY_REQUIRE_VALID_ENCOUNTER(ContactScene);
    auto Attacker = ContactScene.GetFighter(0, 0);
    auto TargetState = NewObject<URelayFixtureProjectile>(ContactScene.GetFighter(1, 0));
    auto Target = ContactScene.Game->AddBattleObject(
        TargetState, Attacker->PosX + 180000, 0, DIR_Left, 0, false, ContactScene.GetFighter(1, 0));
    Attacker->SetFacing(DIR_Right);
    Attacker->SetAttacking(true);
    Attacker->EnableHit(true);
    Attacker->NormalHit.Hitstop = 3;
    Attacker->NormalHit.Damage = 0;
    Attacker->NormalHit.Hitstun = 0;
    Attacker->NormalHit.GroundHitAction = HACT_Custom;
    Attacker->NormalHit.AirHitAction = HACT_Custom;
    Attacker->NormalHit.CustomHitAction =
        FGameplayTag::RequestGameplayTag(TEXT("State.Universal.Stand"));
    Attacker->CounterHit = Attacker->NormalHit;
    auto Contact = [&]() {
        Attacker->SetCelName(FGameplayTag::RequestGameplayTag(TEXT("State.Universal.Throw")));
        Target->SetCelName(FGameplayTag::RequestGameplayTag(TEXT("State.Universal.Stand")));
        Attacker->HandleHitCollision(Target);
    };
    Contact();
    if (!TestEqual(TEXT("actual owned-object hurt contact creates hitstop"), Target->Hitstop, 3))
    {
        return false;
    }
    FRollbackData AfterContact;
    ContactScene.Game->SaveGameState(AfterContact, &Checksum);
    ContactScene.Frame();
    Contact();
    TestEqual(TEXT("ordinary repeated projectile contact remains consumed"), Target->Hitstop, 2);
    for (int FrameIndex = 0; FrameIndex < 100; ++FrameIndex)
    {
        ContactScene.Frame();
    }
    TestFalse(TEXT("authored expiration clears the speculative projectile"), Target->IsActive);
    ContactScene.Game->LoadGameState(AfterContact);
    ContactScene.Frame();
    Contact();
    TestEqual(TEXT("restored projectile contact cannot renew hitstop after expiration"),
              Target->Hitstop, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayThrowInterrupt, "NightSky.Relay.ThrowInterruption",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayThrowInterrupt::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
    for (auto Fighter : Encounter.Game->Players)
    {
        Fighter->PosX = Fighter->PlayerIndex == 0 ? -150000 : 150000;
    }
    Encounter.Frame(RelaySlot2);
    Encounter.Frame(0, INP_B);
    TestTrue(TEXT("ordinary throw contact locks participant"),
             (Encounter.GetFighter(0, 0)->PlayerFlags & PLF_IsThrowLock) != 0);
    TestTrue(TEXT("throw retains real owner"),
             Encounter.GetFighter(0, 0)->AttackOwner == Encounter.GetFighter(1, 0));
    TestTrue(TEXT("throw cancels relay"),
             Encounter.Game->GetRelayStatus(true).Stage == ERelayPhase::Idle);
    TestEqual(TEXT("throw refunds nothing"), Encounter.Game->GetRelayStatus(true).Resource, 100);
    for (int FrameIndex = 0; FrameIndex < 40; ++FrameIndex)
    {
        Encounter.Frame();
    }
    TestFalse(TEXT("authored throw releases"),
              (Encounter.GetFighter(0, 0)->PlayerFlags & PLF_IsThrowLock) != 0);
    TestTrue(TEXT("single original main"), Encounter.GetFighter(0, 0)->IsMainPlayer());
    TestFalse(TEXT("reserve removed"), Encounter.GetFighter(0, 1)->IsOnScreen());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayContactKO, "NightSky.Relay.ContactKOPromotesSurvivor",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayContactKO::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
    for (auto Fighter : Encounter.Game->Players)
    {
        Fighter->PosX = Fighter->PlayerIndex == 0 ? -150000 : 150000;
    }
    Encounter.GetFighter(0, 0)->SetHealth(1);
    Encounter.GetFighter(0, 1)->SetHealth(1);
    Encounter.Frame(RelaySlot2, INP_A);
    for (int FrameIndex = 0; FrameIndex < 8; ++FrameIndex)
    {
        Encounter.Frame();
    }
    TestEqual(TEXT("main KO from actual contact"), Encounter.GetFighter(0, 0)->CurrentHealth, 0);
    TestEqual(TEXT("reserve KO from actual contact"), Encounter.GetFighter(0, 1)->CurrentHealth, 0);
    TestEqual(TEXT("KO recoverable main"), Encounter.GetFighter(0, 0)->RecoverableHealth, 0);
    TestEqual(TEXT("KO recoverable reserve"), Encounter.GetFighter(0, 1)->RecoverableHealth, 0);
    TestTrue(TEXT("lowest surviving immutable slot promoted"),
             Encounter.GetFighter(0, 2)->IsMainPlayer());
    TestTrue(TEXT("surviving team continues"),
             Encounter.Game->BattleState.CurrentWinSide == WIN_None);
    for (int FrameIndex = 0; FrameIndex < 150; ++FrameIndex)
    {
        Encounter.Frame();
    }
    TestEqual(TEXT("KO remains permanent"), Encounter.GetFighter(0, 1)->CurrentHealth, 0);
    int X = Encounter.GetFighter(0, 2)->PosX;
    Encounter.Frame(INP_Right);
    TestTrue(TEXT("survivor receives ordinary input"), Encounter.GetFighter(0, 2)->PosX > X);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelaySimultaneousElimination,
                                 "NightSky.Relay.SimultaneousElimination",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelaySimultaneousElimination::RunTest(const FString &)
{
    for (bool BothLost : {false, true})
    {
        RelayTests::FEncounter Encounter;
        RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
        Encounter.Game->BattleState.RoundTimer = 4;
        for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
        {
            Encounter.GetFighter(TeamIndex, 0)->SetHealth(TeamIndex == 1 && !BothLost ? 1000 : 1);
            Encounter.GetFighter(TeamIndex, 1)->SetHealth(0);
            Encounter.GetFighter(TeamIndex, 2)->SetHealth(0);
            for (int Index = 0; Index < 3; ++Index)
            {
                Encounter.GetFighter(TeamIndex, Index)->PosX = TeamIndex == 0 ? -150000 : 150000;
            }
            // Each authored tip overlaps the opposing hurtbox, but the two tips are
            // disjoint: this encounter tests simultaneous lethal contacts, not a clash.
            auto &Hit =
                Encounter.GetFighter(TeamIndex, 0)->CollisionData->CollisionFrames[1].Boxes.Last();
            Hit.SizeX = 10000;
            Hit.PosX = 300000;
        }
        Encounter.Frame(INP_A, INP_A);
        for (int FrameIndex = 0; FrameIndex < 8; ++FrameIndex)
        {
            Encounter.Frame();
        }
        TestEqual(TEXT("due P1 contact resolved"), Encounter.GetFighter(0, 0)->CurrentHealth, 0);
        TestEqual(TEXT("due P2 contact resolved"), Encounter.GetFighter(1, 0)->CurrentHealth,
                  BothLost ? 0 : 500);
        TestTrue(TEXT("simultaneous elimination draws"),
                 Encounter.Game->BattleState.CurrentWinSide == (BothLost ? WIN_Draw : WIN_P2));
        Encounter.Frame(INP_A | RelaySlot2, INP_A | RelaySlot3);
        TestTrue(TEXT("result stable"),
                 Encounter.Game->BattleState.CurrentWinSide == (BothLost ? WIN_Draw : WIN_P2));
        Encounter.Frame(INP_Rematch | RelaySlot2, INP_Rematch | RelaySlot2);
        for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
        {
            TestTrue(TEXT("elimination rematch clears result and restores main"),
                     Encounter.Game->BattleState.CurrentWinSide == WIN_None &&
                         Encounter.GetFighter(TeamIndex, 0)->IsMainPlayer());
            for (int Index = 0; Index < 3; ++Index)
            {
                TestEqual(TEXT("elimination rematch restores roster health"),
                          Encounter.GetFighter(TeamIndex, Index)->CurrentHealth,
                          Encounter.GetFighter(TeamIndex, Index)->MaxHealth);
            }
        }
        Encounter.Frame();
        Encounter.Frame(RelaySlot2, RelaySlot2);
        TestEqual(TEXT("fresh relay after elimination rematch"),
                  Encounter.Game->GetRelayStatus(true).Resource, 100);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayEligibility, "NightSky.Relay.EligibilityBoundaries",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayEligibility::RunTest(const FString &)
{
    for (int Resource : {99, 100, 101})
    {
        RelayTests::FEncounter Encounter;
        RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
        TestTrue(TEXT("prematch resource setup"),
                 Encounter.Game->SetInitialRelayResource(true, Resource));
        Encounter.Frame(RelaySlot2 | RelaySlot3);
        TestEqual(TEXT("exact resource threshold"), Encounter.Game->GetRelayStatus(true).Resource,
                  Resource < 100 ? Resource : Resource - 100);
        TestEqual(TEXT("lower eligible slot preference"), Encounter.GetFighter(0, 1)->IsOnScreen(),
                  Resource >= 100);
        TestFalse(TEXT("other request does not expose another fighter"),
                  Encounter.GetFighter(0, 2)->IsOnScreen());
        if (Resource < 100)
        {
            TestTrue(TEXT("HUD rejection reason"),
                     Encounter.Game->GetRelayStatus(true).Rejection == ERelayRejection::Resource);
        }
    }
    for (int Health : {0, 1})
    {
        RelayTests::FEncounter Encounter;
        RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
        Encounter.GetFighter(0, 1)->SetHealth(Health);
        Encounter.Frame(RelaySlot2);
        TestEqual(TEXT("dead versus one health eligibility"),
                  Encounter.Game->GetRelayStatus(true).Resource, Health == 0 ? 200 : 100);
    }
    RelayTests::FEncounter Encounter;
    RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
    Encounter.Game->SetTeamCooldown(true, 1, 1);
    TestTrue(TEXT("one frame cooldown rejects"),
             Encounter.Game->RelayEligibility(true, 2) == ERelayRejection::Cooldown);
    Encounter.Frame();
    TestTrue(TEXT("zero cooldown becomes eligible"),
             Encounter.Game->RelayEligibility(true, 2) == ERelayRejection::None);
    Encounter.Frame(RelaySlot2);
    TestEqual(TEXT("eligible edge spends"), Encounter.Game->GetRelayStatus(true).Resource, 100);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayTrainingReset, "NightSky.Relay.TrainingResetAllPhases",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayTrainingReset::RunTest(const FString &)
{
    for (int ResetAt : {0, 5, 6, 17, 18, 19, 30, 31, 36})
    {
        RelayTests::FEncounter Encounter;
        RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
        for (int FrameIndex = 0; FrameIndex <= ResetAt; ++FrameIndex)
        {
            Encounter.Frame(FrameIndex == 0 ? RelaySlot2 : FrameIndex == 18 ? RelaySlot3 : 0);
        }
        if (ResetAt % 2)
        {
            Encounter.Game->StartSuperFreeze(8, 8, Encounter.GetFighter(0, 0));
        }
        Encounter.Frame(INP_ResetTraining | RelaySlot2);
        const auto V = Encounter.Game->GetRelaySlots(true);
        if (!TestEqual(TEXT("reset selected roster"), V.Num(), 3))
        {
            return false;
        }
        TestEqual(TEXT("reset resource"), Encounter.Game->GetRelayStatus(true).Resource, 200);
        TestTrue(TEXT("reset initial main"), Encounter.GetFighter(0, 0)->IsMainPlayer());
        for (int Index = 0; Index < 3; ++Index)
        {
            TestEqual(TEXT("reset full health"), V[Index].Health,
                      Encounter.GetFighter(0, Index)->MaxHealth);
            TestEqual(TEXT("reset recoverable"), V[Index].Recoverable, 0);
            TestEqual(TEXT("reset cooldown"), V[Index].Cooldown, 0);
        }
        for (int FrameIndex = 0; FrameIndex < 100; ++FrameIndex)
        {
            Encounter.Frame(RelaySlot2);
        }
        TestEqual(TEXT("held button does not launch after restart"),
                  Encounter.Game->GetRelayStatus(true).Resource, 200);
        TestEqual(TEXT("old move lifetime cleared"), Encounter.GetFighter(1, 0)->CurrentHealth,
                  Encounter.GetFighter(1, 0)->MaxHealth);
        Encounter.Frame();
        Encounter.Frame(RelaySlot2);
        TestEqual(TEXT("fresh edge starts another relay"),
                  Encounter.Game->GetRelayStatus(true).Resource, 100);
    }
    for (int Stimulus = 0; Stimulus < 3; ++Stimulus)
    {
        RelayTests::FEncounter Encounter;
        RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
        for (auto Fighter : Encounter.Game->Players)
        {
            Fighter->PosX = Fighter->PlayerIndex == 0 ? -150000 : 150000;
        }
        if (Stimulus == 2)
        {
            Encounter.GetFighter(0, 1)->SetHealth(1);
        }
        Encounter.Frame(RelaySlot2, Stimulus == 1 ? 0 : INP_A);
        if (Stimulus == 1)
        {
            Encounter.Frame(0, INP_B);
        }
        else
        {
            for (int FrameIndex = 1; FrameIndex <= 3; ++FrameIndex)
            {
                Encounter.Frame();
            }
        }
        TestTrue(TEXT("reset encounter contains real contact ownership"),
                 Encounter.GetFighter(0, 0)->AttackOwner == Encounter.GetFighter(1, 0));
        if (Stimulus == 1)
        {
            TestTrue(TEXT("reset during actual throw lock"),
                     (Encounter.GetFighter(0, 0)->PlayerFlags & PLF_IsThrowLock) != 0);
        }
        if (Stimulus == 2)
        {
            TestEqual(TEXT("reset follows actual reserve KO"),
                      Encounter.GetFighter(0, 1)->CurrentHealth, 0);
        }
        Encounter.Frame(INP_ResetTraining | RelaySlot2);
        for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
        {
            for (int Index = 0; Index < 3; ++Index)
            {
                auto Fighter = Encounter.GetFighter(TeamIndex, Index);
                TestEqual(TEXT("reaction reset restores full health"), Fighter->CurrentHealth,
                          Fighter->MaxHealth);
                TestFalse(TEXT("reaction reset clears all retained reactions"),
                          Fighter->CheckIsStunned());
                TestEqual(TEXT("reaction reset clears cooldown"),
                          RelayTests::GetCooldown(Encounter.Game, TeamIndex, Index), 0);
            }
        }
        for (int FrameIndex = 0; FrameIndex < 100; ++FrameIndex)
        {
            Encounter.Frame(RelaySlot2);
        }
        TestEqual(TEXT("held reset input cannot spend later"),
                  Encounter.Game->GetRelayStatus(true).Resource, 200);
        TestEqual(TEXT("no old contact survives reset"), Encounter.GetFighter(1, 0)->CurrentHealth,
                  Encounter.GetFighter(1, 0)->MaxHealth);
        Encounter.Frame();
        Encounter.Frame(RelaySlot2);
        TestEqual(TEXT("fresh relay succeeds after reaction reset"),
                  Encounter.Game->GetRelayStatus(true).Resource, 100);
    }
    return true;
}

#include "RelayPythonAutomation.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayRenderedSample, "NightSkyIntegration.Relay.RenderedSample",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayRenderedSample::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FRelayPythonCommand(this, TEXT("relay_rendered_sample.py")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayGGPOPeers, "NightSkyIntegration.Relay.GGPOPeers",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayGGPOPeers::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(
        FRelayPythonCommand(this, TEXT("relay_ggpo_campaign.py"), TEXT(""), 660.));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayStrikePhaseMatrix, "NightSky.Relay.StrikePhaseMatrix",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayStrikePhaseMatrix::RunTest(const FString &)
{
    for (bool Routed : {false, true})
    {
        for (int Due : {1, 2, 5, 6, 11, 17, 18, 19, 22, 24, 27, 28, 30, 31, 33, 36})
        {
            if (!Routed && Due > 33)
            {
                continue;
            }
            RelayTests::FEncounter Encounter;
            RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
            // Authored long-range strike content keeps the opposing relay's short melee out
            // of range. The ordinary input attack's three startup frames still cause contact.
            auto &Hit = Encounter.GetFighter(1, 0)->CollisionData->CollisionFrames[1].Boxes.Last();
            Hit.SizeX = 20000;
            Hit.PosX = 2030000;
            for (int FrameIndex = FMath::Min(0, Due - 3); FrameIndex <= Due; ++FrameIndex)
            {
                Encounter.Frame(FrameIndex == 0                           ? RelaySlot2
                                : FrameIndex == 18 && Routed && Due >= 19 ? RelaySlot3
                                                                          : 0,
                                FrameIndex == Due - 3 ? INP_A : 0);
                if (FrameIndex >= 0 && FrameIndex < Due)
                {
                    TestTrue(FString::Printf(TEXT("phase %d no premature interruption at %d"), Due,
                                             FrameIndex),
                             Encounter.Game->GetRelayStatus(true).Stage != ERelayPhase::Idle);
                }
            }
            const auto Status = Encounter.Game->GetRelayStatus(true);
            TestTrue(FString::Printf(TEXT("real due strike cancels phase %d"), Due),
                     Status.Stage == ERelayPhase::Idle);
            TestEqual(TEXT("no refund"), Status.Resource, 100);
            TestEqual(TEXT("termination cooldown"), RelayTests::GetCooldown(Encounter.Game, 0, 1),
                      120);
            TestTrue(TEXT("surviving original main retained"),
                     Encounter.GetFighter(0, 0)->IsMainPlayer());
            TestTrue(TEXT("reserve keeps actual hit reaction"),
                     Encounter.GetFighter(0, 1)->CheckIsStunned());
            TestTrue(TEXT("actual contact owner"),
                     Encounter.GetFighter(0, 1)->AttackOwner == Encounter.GetFighter(1, 0));
            for (auto Object : Encounter.Game->Objects)
            {
                TestFalse(TEXT("interrupted sequence projectile cleared"),
                          Object->IsActive && Object->Player && Object->Player->PlayerIndex == 0);
            }
            if (Routed && Due >= 19)
            {
                TestTrue(TEXT("routed third took real contact"),
                         Encounter.GetFighter(0, 2)->CurrentHealth <
                             Encounter.GetFighter(0, 2)->MaxHealth);
                TestEqual(TEXT("routed cooldown"), RelayTests::GetCooldown(Encounter.Game, 0, 2),
                          120);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayAllocationRestore, "NightSky.Relay.CameraAllocationRestore",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayAllocationRestore::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
    Encounter.Frame();
    FInputCondition Query;
    Query.Sequence.Add(FInputBitmask(INP_A));
    Query.DisallowedInputs.Add(INP_C);
    Query.Sequence[0].DisallowedInputs.Add(INP_C);
    Encounter.GetFighter(0, 0)->WriteInputCondition(Query);
    TestTrue(TEXT("checkpoint has a usable buffered input"),
             Encounter.GetFighter(0, 0)->CheckInput(Query));
    FRollbackData Saved;
    int32 Checksum = 0;
    Encounter.Game->SaveGameState(Saved, &Checksum);
    for (int Cycle = 0; Cycle < 16; ++Cycle)
    {
        for (int FrameIndex = 0; FrameIndex <= 19; ++FrameIndex)
        {
            Encounter.Frame(FrameIndex == 0    ? RelaySlot2
                            : FrameIndex == 18 ? RelaySlot3
                                               : 0,
                            FrameIndex == 0    ? RelaySlot2
                            : FrameIndex == 18 ? RelaySlot3
                                               : 0);
        }
        int Exposed = 0;
        for (auto Fighter : Encounter.Game->Players)
        {
            Exposed += Fighter->IsOnScreen();
        }
        if (!TestEqual(TEXT("normal relay expansion exposes six fighters"), Exposed, 6))
        {
            return false;
        }
        auto LargeQuery = Query;
        for (int Index = 0; Index < 40; ++Index)
        {
            LargeQuery.DisallowedInputs.Add(INP_C);
            LargeQuery.Sequence[0].DisallowedInputs.Add(INP_C);
        }
        Encounter.GetFighter(0, 0)->CheckInput(LargeQuery);
        Encounter.Game->LoadGameState(Saved);
        TestTrue(TEXT("restored fixed input history survives allocating condition queries"),
                 Encounter.GetFighter(0, 0)->CheckInput(Query));
        Encounter.Game->UpdateGameState(0, 0, true);
        Exposed = 0;
        for (auto Fighter : Encounter.Game->Players)
        {
            Exposed += Fighter->IsOnScreen();
        }
        TestEqual(TEXT("restored checkpoint has two exposed mains"), Exposed, 2);
        TestEqual(TEXT("restored first team resource"),
                  Encounter.Game->GetRelayStatus(true).Resource, 200);
        TestEqual(TEXT("restored second team resource"),
                  Encounter.Game->GetRelayStatus(false).Resource, 200);
        for (auto Fighter : Encounter.Game->Players)
        {
            if (Fighter->IsOnScreen())
            {
                TestTrue(TEXT("restored camera includes each exposed fighter"),
                         Encounter.Game->BattleState.ScreenData.TargetObjects.Contains(Fighter));
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayThrowPhaseMatrix, "NightSky.Relay.ThrowPhaseMatrix",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayThrowPhaseMatrix::RunTest(const FString &)
{
    for (bool Routed : {false, true})
    {
        for (int Due : {1, 2, 5, 6, 11, 17, 18, 19, 22, 24, 27, 28, 30, 31, 33, 36})
        {
            if (!Routed && Due > 33)
            {
                continue;
            }
            RelayTests::FEncounter Encounter;
            RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
            CastChecked<ARelayFixtureFighter>(Encounter.GetFighter(1, 0))->SampleThrowRange =
                3000000;
            for (int FrameIndex = 0; FrameIndex <= Due; ++FrameIndex)
            {
                Encounter.Frame(FrameIndex == 0                           ? RelaySlot2
                                : FrameIndex == 18 && Routed && Due >= 19 ? RelaySlot3
                                                                          : 0,
                                FrameIndex == Due ? INP_B : 0);
            }
            TestTrue(FString::Printf(TEXT("real throw locks main during phase %d"), Due),
                     (Encounter.GetFighter(0, 0)->PlayerFlags & PLF_IsThrowLock) != 0);
            TestTrue(TEXT("ordinary throw preserves owner"),
                     Encounter.GetFighter(0, 0)->AttackOwner == Encounter.GetFighter(1, 0));
            TestTrue(TEXT("throw cancels current relay"),
                     Encounter.Game->GetRelayStatus(true).Stage == ERelayPhase::Idle);
            TestEqual(TEXT("throw termination cooldown"),
                      RelayTests::GetCooldown(Encounter.Game, 0, 1), 120);
            TestEqual(TEXT("throw no refund"), Encounter.Game->GetRelayStatus(true).Resource, 100);
            for (int FrameIndex = 0; FrameIndex < 40; ++FrameIndex)
            {
                Encounter.Frame();
            }
            TestFalse(TEXT("normal authored throw releases"),
                      (Encounter.GetFighter(0, 0)->PlayerFlags & PLF_IsThrowLock) != 0);
            TestTrue(TEXT("original survivor remains main"),
                     Encounter.GetFighter(0, 0)->IsMainPlayer());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayAsymmetricClocks, "NightSky.Relay.AsymmetricHitstopClocks",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayAsymmetricClocks::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
    for (auto Fighter : Encounter.Game->Players)
    {
        Fighter->PosX = Fighter->PlayerIndex == 0 ? -150000 : 150000;
    }
    Encounter.GetFighter(0, 2)->SetHealth(100);
    Encounter.GetFighter(0, 2)->SetRecoverableHealth(50);
    Encounter.Game->SetTeamCooldown(true, 2, 30);
    const int Timer = Encounter.Game->BattleState.RoundTimer;
    bool SawHitstop = false, SawAsymmetric = false;
    for (int FrameIndex = 0; FrameIndex <= 34; ++FrameIndex)
    {
        Encounter.Frame(FrameIndex == 0 ? RelaySlot2 : 0);
        const auto Status = Encounter.Game->GetRelayStatus(true);
        if (Encounter.GetFighter(0, 0)->Hitstop > 0 || Encounter.GetFighter(0, 1)->Hitstop > 0)
        {
            SawHitstop = true;
        }
        if (Encounter.GetFighter(0, 0)->Hitstop > 0 &&
            Encounter.GetFighter(1, 0)->Hitstop > Encounter.GetFighter(0, 0)->Hitstop)
        {
            SawAsymmetric = true;
        }
        TestEqual(TEXT("ordinary hitstop leaves round clock advancing"),
                  Encounter.Game->BattleState.RoundTimer, Timer - FrameIndex - 1);
        TestEqual(TEXT("ordinary hitstop leaves reserve healing advancing"),
                  Encounter.GetFighter(0, 2)->CurrentHealth, 101 + FrameIndex);
        TestEqual(TEXT("ordinary hitstop leaves cooldown advancing"),
                  RelayTests::GetCooldown(Encounter.Game, 0, 2), FMath::Max(0, 29 - FrameIndex));
        if (FrameIndex >= 6 && FrameIndex < 18)
        {
            TestEqual(TEXT("real synchronized hitstop does not pause phase age"),
                      Status.ElapsedFrames, FrameIndex - 6);
        }
        if (FrameIndex == 34)
        {
            TestTrue(TEXT("real contact sequence still terminates exactly at34"),
                     Status.Stage == ERelayPhase::Idle);
        }
    }
    TestTrue(TEXT("authored synchronized attacks caused real hitstop"), SawHitstop);
    TestTrue(TEXT("real receiver and attacker hitstop differ"), SawAsymmetric);
    TestTrue(TEXT("ordinary opponent suffered actual authored damage"),
             Encounter.GetFighter(1, 0)->CurrentHealth < Encounter.GetFighter(1, 0)->MaxHealth);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayFreezeCombat, "NightSky.Relay.FreezeMetamorphicCombat",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayFreezeCombat::RunTest(const FString &)
{
    auto Observe = [](const RelayTests::FEncounter &Encounter) {
        FString Row = FString::Printf(TEXT("%d/%d"), Encounter.Game->BattleState.RoundTimer,
                                      int(Encounter.Game->BattleState.CurrentWinSide));
        for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
        {
            const auto Status = Encounter.Game->GetRelayStatus(TeamIndex == 0);
            Row += FString::Printf(TEXT("|%d/%d/%d"), int(Status.Stage), Status.ElapsedFrames,
                                   Status.Resource);
            for (int Index = 0; Index < 3; ++Index)
            {
                auto Fighter = Encounter.GetFighter(TeamIndex, Index);
                Row += FString::Printf(
                    TEXT(";%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%s"), Fighter->CurrentHealth,
                    Fighter->RecoverableHealth, Fighter->IsMainPlayer(), Fighter->IsOnScreen(),
                    RelayTests::GetCooldown(Encounter.Game, TeamIndex, Index), Fighter->PosX,
                    Fighter->PosY, Fighter->ActionTime, Fighter->Hitstop, Fighter->ComboCounter,
                    *RelayTests::SemanticContact(Fighter));
            }
        }
        Row += TEXT("|") + RelayTests::SemanticProjectiles(Encounter.Game);
        return Row;
    };
    TArray<FString> Reference;
    {
        RelayTests::FEncounter Encounter;
        RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
        for (auto Fighter : Encounter.Game->Players)
        {
            Fighter->PosX = Fighter->PlayerIndex == 0 ? -150000 : 150000;
        }
        Encounter.GetFighter(0, 2)->SetHealth(5000);
        Encounter.GetFighter(0, 2)->SetRecoverableHealth(90);
        for (int FrameIndex = 0; FrameIndex < 90; ++FrameIndex)
        {
            Encounter.Frame(FrameIndex == 0 ? RelaySlot2 : FrameIndex == 18 ? RelaySlot3 : 0);
            Reference.Add(Observe(Encounter));
        }
        TestTrue(TEXT("control has real authored damage"),
                 Encounter.GetFighter(1, 0)->CurrentHealth < Encounter.GetFighter(1, 0)->MaxHealth);
        TestTrue(TEXT("control completes actual routed handoff"),
                 Encounter.GetFighter(0, 2)->IsMainPlayer());
        TestEqual(TEXT("control paid resource"), Encounter.Game->GetRelayStatus(true).Resource,
                  100);
    }
    for (int At : {0, 5, 6, 17, 18, 19, 27, 30, 31, 36})
    {
        RelayTests::FEncounter Encounter;
        RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
        for (auto Fighter : Encounter.Game->Players)
        {
            Fighter->PosX = Fighter->PlayerIndex == 0 ? -150000 : 150000;
        }
        Encounter.GetFighter(0, 2)->SetHealth(5000);
        Encounter.GetFighter(0, 2)->SetRecoverableHealth(90);
        for (int FrameIndex = 0; FrameIndex < 90; ++FrameIndex)
        {
            if (FrameIndex == At)
            {
                const FString Before = Observe(Encounter);
                Encounter.GetFighter(0, 0)->StartSuperFreeze(7, 7);
                for (int Frozen = 0; Frozen < 7; ++Frozen)
                {
                    Encounter.Frame();
                    if (!TestEqual(
                            FString::Printf(
                                TEXT("boundary %d frozen frame %d preserves combat and clocks"), At,
                                Frozen),
                            Observe(Encounter), Before))
                    {
                        return false;
                    }
                }
            }
            Encounter.Frame(FrameIndex == 0 ? RelaySlot2 : FrameIndex == 18 ? RelaySlot3 : 0);
            if (!TestEqual(
                    FString::Printf(TEXT("boundary %d resumed gameplay frame %d"), At, FrameIndex),
                    Observe(Encounter), Reference[FrameIndex]))
            {
                return false;
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayOverlappingProjectiles,
                                 "NightSky.Relay.OverlappingProjectileCancellation",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayOverlappingProjectiles::RunTest(const FString &)
{
    for (int InterruptedTeam : {0, 1})
    {
        RelayTests::FEncounter Encounter;
        RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
        const int Other = 1 - InterruptedTeam;
        ABattleObject *Incoming = nullptr;
        int VictimHealth[3] = {};
        for (int Index = 0; Index < 3; ++Index)
        {
            VictimHealth[Index] = Encounter.GetFighter(InterruptedTeam, Index)->CurrentHealth;
        }
        for (int FrameIndex = 0; FrameIndex <= 60; ++FrameIndex)
        {
            if (FrameIndex == 25)
            {
                bool OwnProjectile = false;
                for (auto Object : Encounter.Game->Objects)
                {
                    if (Object->IsActive && Object->Player)
                    {
                        if (Object->Player->PlayerIndex == InterruptedTeam)
                        {
                            OwnProjectile = true;
                            Object->PosY = 500000;
                        }
                        else
                        {
                            Incoming = Object;
                        }
                    }
                }
                if (!TestTrue(TEXT("both authored sequences launched actual projectiles"),
                              OwnProjectile && Incoming))
                {
                    return false;
                }
                // Public encounter placement schedules a launched projectile's real contact.
                // Its ordinary Update and collision path determine damage and interruption.
                Incoming->PosX = Encounter.GetFighter(InterruptedTeam, 0)->PosX +
                                 (Incoming->Direction == DIR_Left ? 150000 : -150000);
            }
            int Inputs[2] = {};
            Inputs[InterruptedTeam] = FrameIndex == 0    ? RelaySlot2
                                      : FrameIndex == 18 ? RelaySlot3
                                                         : 0;
            Inputs[Other] = FrameIndex == 4 ? RelaySlot3 : FrameIndex == 31 ? RelaySlot3 : 0;
            Encounter.Frame(Inputs[0], Inputs[1]);
            if (FrameIndex == 25)
            {
                TestTrue(TEXT("actual opponent projectile cancels only struck sequence"),
                         Encounter.Game->GetRelayStatus(InterruptedTeam == 0).Stage ==
                             ERelayPhase::Idle);
                TestTrue(TEXT("opposing independently aged route remains open"),
                         Encounter.Game->GetRelayStatus(Other == 0).Stage == ERelayPhase::Route);
                TestTrue(TEXT("opponent projectile is not erased by foreign cancellation"),
                         Incoming->IsActive);
                for (int Index = 0; Index < 3; ++Index)
                {
                    TestTrue(TEXT("each enrolled victim receives actual contact"),
                             Encounter.GetFighter(InterruptedTeam, Index)->CurrentHealth <
                                 VictimHealth[Index]);
                    TestTrue(TEXT("contact retains the projectile owner"),
                             Encounter.GetFighter(InterruptedTeam, Index)->AttackOwner == Incoming);
                    TestEqual(TEXT("each interrupted participant cooldown"),
                              RelayTests::GetCooldown(Encounter.Game, InterruptedTeam, Index), 120);
                }
                for (auto Object : Encounter.Game->Objects)
                {
                    TestFalse(TEXT("only interrupted sequence projectiles canceled"),
                              Object->IsActive && Object->Player &&
                                  Object->Player->PlayerIndex == InterruptedTeam);
                }
            }
            if (FrameIndex == 32)
            {
                TestTrue(TEXT("uninterrupted team begins followup after late route"),
                         Encounter.Game->GetRelayStatus(Other == 0).Stage == ERelayPhase::Followup);
            }
            if (FrameIndex == 50)
            {
                TestEqual(TEXT("uninterrupted termination cooldown independent"),
                          RelayTests::GetCooldown(Encounter.Game, Other, 2), 120);
            }
        }
        TestTrue(TEXT("interrupted original main retained"),
                 Encounter.GetFighter(InterruptedTeam, 0)->IsMainPlayer());
        TestTrue(TEXT("opponent promoted its late route"),
                 Encounter.GetFighter(Other, 2)->IsMainPlayer());
        for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
        {
            TestEqual(TEXT("independent single payment"),
                      Encounter.Game->GetRelayStatus(TeamIndex == 0).Resource, 100);
        }
        auto A = Encounter.Game->GetMainPlayer(true);
        auto B = Encounter.Game->GetMainPlayer(false);
        const int AX = A->PosX, BX = B->PosX;
        Encounter.Frame(INP_Right, INP_Right);
        TestTrue(TEXT("both resulting mains regain ordinary control"),
                 A->PosX > AX && B->PosX > BX);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayComboOwnership,
                                 "NightSky.Relay.ComboOwnershipAndPromotedRecovery",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayComboOwnership::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
    for (auto Fighter : Encounter.Game->Players)
    {
        Fighter->PosX = Fighter->PlayerIndex == 0 ? -150000 : 150000;
        CastChecked<ARelayFixtureFighter>(Fighter)->SampleHitstun = 90;
        auto &Hit = Fighter->CollisionData->CollisionFrames[1].Boxes.Last();
        Hit.SizeX = 4000000;
        Hit.PosX = 1000000;
    }
    ABattleObject *Projectile = nullptr;
    for (int FrameIndex = 0; FrameIndex <= 36; ++FrameIndex)
    {
        Encounter.Frame(FrameIndex == 0 ? RelaySlot2 : FrameIndex == 18 ? RelaySlot3 : 0);
        for (auto Object : Encounter.Game->Objects)
        {
            if (Object->IsActive && Object->Player == Encounter.GetFighter(0, 1))
            {
                Projectile = Object;
            }
        }
        // Authored order: projectile300, melee400, melee400, followup700.
        // Forced80% with combo rate60% gives 300,153,122,172 damage.
        const int ExpectedHealth[5] = {10000, 9700, 9547, 9425, 9253};
        const int ExpectedProration[5] = {10000, 8000, 6400, 5120, 4096};
        const int Hits = Encounter.GetFighter(0, 0)->ComboCounter;
        if (Hits >= 0 && Hits <= 4)
        {
            TestEqual(TEXT("independent per-hit cumulative damage"),
                      Encounter.GetFighter(1, 0)->CurrentHealth, ExpectedHealth[Hits]);
            TestEqual(TEXT("independent per-hit scaling"),
                      Encounter.GetFighter(1, 0)->TotalProration, ExpectedProration[Hits]);
        }
        if (FrameIndex == 8 || FrameIndex == 20 || FrameIndex == 25 || FrameIndex == 36)
        {
            AddInfo(FString::Printf(
                TEXT("Combo frame=%d counter=%d targetHP=%d stun=%d state=%s action=%d sample=%d "
                     "received=%d"),
                FrameIndex, Encounter.GetFighter(0, 0)->ComboCounter,
                Encounter.GetFighter(1, 0)->CurrentHealth, Encounter.GetFighter(1, 0)->StunTime,
                *Encounter.GetFighter(1, 0)->PrimaryStateMachine.CurrentState->Name.ToString(),
                Encounter.GetFighter(1, 0)->ActionTime,
                CastChecked<ARelayFixtureFighter>(Encounter.GetFighter(1, 0))->SampleHitstun,
                Encounter.GetFighter(1, 0)->ReceivedHit.Hitstun));
        }
    }
    if (!TestTrue(TEXT("real reserve-owned projectile exists before handoff"),
                  Projectile && Projectile->IsActive))
    {
        return false;
    }
    TestEqual(TEXT("projectile plus two synchronized owners plus followup share four hits"),
              Encounter.GetFighter(0, 0)->ComboCounter, 4);
    TestEqual(TEXT("four authored 80-percent proration steps"),
              Encounter.GetFighter(1, 0)->TotalProration, 4096);
    const int Counter = Encounter.GetFighter(0, 0)->ComboCounter,
              Proration = Encounter.GetFighter(1, 0)->TotalProration;
    Encounter.Frame();
    TestTrue(TEXT("third fighter actually becomes main"),
             Encounter.GetFighter(0, 2)->IsMainPlayer());
    TestEqual(TEXT("handoff preserves active combo count"),
              Encounter.GetFighter(0, 2)->ComboCounter, Counter);
    TestEqual(TEXT("handoff preserves victim scaling"), Encounter.GetFighter(1, 0)->TotalProration,
              Proration);
    TestTrue(TEXT("old projectile remains owned by its original reserve"),
             Projectile->IsActive && Projectile->Player == Encounter.GetFighter(0, 1));
    const int Before = Encounter.GetFighter(1, 0)->CurrentHealth,
              Recovery = Encounter.GetFighter(1, 0)->RecoverableHealth;
    Encounter.Frame(INP_A);
    for (int FrameIndex = 39; FrameIndex <= 41; ++FrameIndex)
    {
        Encounter.Frame();
    }
    const int Expected = 500 * (Proration * 90 / 100) * 60 / 10000 / 100;
    TestTrue(TEXT("expected continuation is positively scaled"), Expected > 0 && Expected < 500);
    TestEqual(TEXT("new main ordinary strike continues team scaling"),
              Encounter.GetFighter(1, 0)->CurrentHealth, Before - Expected);
    TestEqual(TEXT("ordinary main recovery uses authored fraction"),
              Encounter.GetFighter(1, 0)->RecoverableHealth, Recovery + Expected * 25 / 100);
    TestEqual(TEXT("new main continues active hit count"), Encounter.GetFighter(0, 2)->ComboCounter,
              5);
    for (int FrameIndex = 42; FrameIndex < 150; ++FrameIndex)
    {
        Encounter.Frame();
    }
    const int PromotedHealth = Encounter.GetFighter(0, 2)->CurrentHealth,
              PromotedRecovery = Encounter.GetFighter(0, 2)->RecoverableHealth;
    Encounter.Frame(0, INP_A);
    for (int FrameIndex = 151; FrameIndex <= 154; ++FrameIndex)
    {
        Encounter.Frame();
    }
    TestEqual(TEXT("promoted main receives real opponent strike"),
              Encounter.GetFighter(0, 2)->CurrentHealth, PromotedHealth - 500);
    TestEqual(TEXT("promoted former reserve now uses main recovery fraction"),
              Encounter.GetFighter(0, 2)->RecoverableHealth, PromotedRecovery + 125);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayGuardWhiff, "NightSky.Relay.GuardedContactAndWhiff",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayGuardWhiff::RunTest(const FString &)
{
    for (bool GuardContact : {false, true})
    {
        RelayTests::FEncounter Encounter;
        RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
        auto &Hit = Encounter.GetFighter(1, 0)->CollisionData->CollisionFrames[1].Boxes.Last();
        // A narrow tip reaches the hurtbox from behind its outgoing hitbox, avoiding
        // a clash; the control translates only that tip so the same attack whiffs.
        Hit.SizeX = 20000;
        Hit.PosX = GuardContact ? 2030000 : 2500000;
        bool SawBlock = false;
        for (int FrameIndex = 0; FrameIndex <= 35; ++FrameIndex)
        {
            if (FrameIndex == 8)
            {
                for (int Index = 0; Index < 2; ++Index)
                {
                    auto &Armor = Encounter.GetFighter(0, Index)->SuperArmorData;
                    Armor.Type = ARM_Guard;
                    Armor.bArmorMid = true;
                    Armor.bArmorOverhead = true;
                    Armor.bArmorLow = true;
                    Armor.bArmorStrike = true;
                    Armor.ArmorHits = 1;
                    Armor.ArmorDamagePercent = 0;
                    Armor.bArmorTakeChipDamage = false;
                    Armor.bArmorDisableIncomingHit = false;
                }
            }
            Encounter.Frame(FrameIndex == 0 ? RelaySlot2 : 0, FrameIndex == 5 ? INP_A : 0);
            if (FrameIndex == 8)
            {
                for (int Index = 0; Index < 2; ++Index)
                {
                    TestEqual(TEXT("authored guard/whiff leaves health unchanged"),
                              Encounter.GetFighter(0, Index)->CurrentHealth,
                              Encounter.GetFighter(0, Index)->MaxHealth);
                    TestEqual(TEXT("guard consumption proves actual contact or positive whiff"),
                              Encounter.GetFighter(0, Index)->SuperArmorData.ArmorHits,
                              GuardContact ? 0 : 1);
                    if (GuardContact)
                    {
                        TestTrue(TEXT("blocked strike retains actual owner and hitstop"),
                                 Encounter.GetFighter(0, Index)->AttackOwner ==
                                         Encounter.GetFighter(1, 0) &&
                                     Encounter.GetFighter(0, Index)->Hitstop > 0);
                    }
                }
                SawBlock = Encounter.GetFighter(1, 0)->Hitstop > 0;
            }
            if (FrameIndex < 34)
            {
                TestTrue(TEXT("blocked or whiffed strike does not cancel relay"),
                         Encounter.Game->GetRelayStatus(true).Stage != ERelayPhase::Idle);
            }
            if (FrameIndex == 34)
            {
                TestTrue(TEXT("guard hitstop preserves exact no-route termination"),
                         Encounter.Game->GetRelayStatus(true).Stage == ERelayPhase::Idle);
            }
        }
        TestEqual(TEXT("guard is an actual blocked-contact positive control"), SawBlock,
                  GuardContact);
        TestEqual(TEXT("one payment survives both encounters"),
                  Encounter.Game->GetRelayStatus(true).Resource, 100);
    }
    for (bool BackHeld : {false, true})
    {
        RelayTests::FEncounter Encounter;
        RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
        for (auto Fighter : Encounter.Game->Players)
        {
            Fighter->PosX = Fighter->PlayerIndex == 0 ? -150000 : 150000;
        }
        for (int FrameIndex = 0; FrameIndex < 5; ++FrameIndex)
        {
            Encounter.Frame(BackHeld ? INP_Left : INP_Right, FrameIndex == 0 ? INP_A : 0);
        }
        TestTrue(TEXT("ordinary directional block control has actual strike ownership"),
                 Encounter.GetFighter(0, 0)->AttackOwner == Encounter.GetFighter(1, 0));
        TestEqual(TEXT("back input blocks authored zero-chip attack; forward control takes500"),
                  Encounter.GetFighter(0, 0)->CurrentHealth,
                  Encounter.GetFighter(0, 0)->MaxHealth - (BackHeld ? 0 : 500));
        TestEqual(TEXT("ordinary back input enters real blockstun"),
                  Encounter.GetFighter(0, 0)->PrimaryStateMachine.CurrentState->StateType ==
                      EStateType::Blockstun,
                  BackHeld);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayMixedRollbackFuzz, "NightSky.Relay.MixedCombatRollbackFuzz",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayMixedRollbackFuzz::RunTest(const FString &)
{
    struct FCommand
    {
        int32 A = 0, B = 0, Delay = 0;
    };
    struct FOutcome
    {
        bool Pass = true, Contact = false, Handoff = false, Rollback = false;
        int FailFrame = -1;
        FString Error, Ledger;
    };
    auto Observe = [](ANightSkyGameState *Game) {
        FString Observation = FString::Printf(TEXT("%d,%d,%d,%d"), Game->BattleState.RoundTimer,
                                              int(Game->BattleState.CurrentWinSide),
                                              Game->BattleState.SuperFreezeDuration,
                                              Game->BattleState.SuperFreezeSelfDuration);
        for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
        {
            const auto &Status = Game->GetRelayStatus(TeamIndex == 0);
            Observation += FString::Printf(TEXT(",%d,%d,%d"), Status.Resource, int(Status.Stage),
                                           Status.ElapsedFrames);
            for (auto Fighter : Game->GetTeam(TeamIndex == 0))
            {
                Observation += FString::Printf(
                    TEXT(",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s"), Fighter->TeamIndex,
                    Fighter->IsMainPlayer(), Fighter->IsOnScreen(), Fighter->CurrentHealth,
                    Fighter->RecoverableHealth,
                    RelayTests::GetCooldown(Game, TeamIndex, Fighter->TeamIndex), Fighter->PosX,
                    Fighter->PosY, Fighter->Hitstop, Fighter->ComboCounter, Fighter->TotalProration,
                    *RelayTests::SemanticContact(Fighter),
                    *Fighter->PrimaryStateMachine.CurrentState->Name.ToString());
            }
        }
        Observation += TEXT("|") + RelayTests::SemanticProjectiles(Game);
        return Observation;
    };
    auto Run = [&](int Seed, const TArray<FCommand> &Commands, int InitialHealth) {
        FOutcome Out;
        Out.Ledger = FString::Printf(TEXT("seed=%d,initialHealth=%d,frames=%d,asset=native-relay-"
                                          "v2\nframe,a,b,delay,semantic_state\n"),
                                     Seed, InitialHealth, Commands.Num());
        RelayTests::FEncounter Clean(Seed), Corrected(Seed);
        const FString CleanError = Clean.ValidationError(TEXT("Clean"));
        const FString CorrectedError = Corrected.ValidationError(TEXT("Corrected"));
        if (!CleanError.IsEmpty() || !CorrectedError.IsEmpty())
        {
            Out.Pass = false;
            Out.Error = CleanError.IsEmpty() ? CorrectedError : CleanError;
            return Out;
        }
        for (auto Encounter : {&Clean, &Corrected})
        {
            Encounter->Instance->BattleData.StartRoundTimer = 2 + Seed % 3;
            Encounter->Game->BattleState.RoundTimer =
                Encounter->Instance->BattleData.StartRoundTimer * 60;
        }
        for (auto Encounter : {&Clean, &Corrected})
        {
            for (auto Fighter : Encounter->Game->Players)
            {
                Fighter->SetHealth(FMath::Min(Fighter->MaxHealth, InitialHealth));
            }
        }
        int PreviousHealth[6] = {};
        for (int Index = 0; Index < 6; ++Index)
        {
            PreviousHealth[Index] = Clean.Game->Players[Index]->CurrentHealth;
        }
        for (int FrameIndex = 0; FrameIndex < Commands.Num(); ++FrameIndex)
        {
            const auto C = Commands[FrameIndex];
            // Public encounter placement at the start of a training segment. The first
            // routed sequence remains out of range as an analytic payment/handoff control.
            if (FrameIndex % 200 == 48)
            {
                for (auto Encounter : {&Clean, &Corrected})
                {
                    for (auto Fighter : Encounter->Game->Players)
                    {
                        Fighter->PosX = Fighter->PlayerIndex == 0 ? -150000 : 150000;
                    }
                }
            }
            if (C.Delay > 0)
            {
                FRollbackData Saved;
                int32 Checksum = 0;
                Corrected.Game->SaveGameState(Saved, &Checksum);
                const FString Before = Observe(Corrected.Game);
                for (int D = 0; D < C.Delay; ++D)
                {
                    Corrected.Game->UpdateGameState(INP_A | RelaySlot2, INP_B | RelaySlot3, true);
                }
                Out.Rollback |= Observe(Corrected.Game) != Before;
                Corrected.Game->LoadGameState(Saved);
            }
            Clean.Frame(C.A, C.B);
            Corrected.Game->UpdateGameState(C.A, C.B, C.Delay > 0);
            const FString Expected = Observe(Clean.Game), Actual = Observe(Corrected.Game);
            Out.Ledger +=
                FString::Printf(TEXT("%d,%d,%d,%d,%s\n"), FrameIndex, C.A, C.B, C.Delay, *Actual);
            if (Expected != Actual)
            {
                Out.Pass = false;
                Out.FailFrame = FrameIndex;
                Out.Error = FString::Printf(
                    TEXT("clean/rollback semantic divergence\nExpected:%s\nActual:%s"), *Expected,
                    *Actual);
                Out.Ledger += TEXT("EXPECTED,") + Expected + TEXT("\n");
                break;
            }
            for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
            {
                const auto Views = Clean.Game->GetRelaySlots(TeamIndex == 0);
                int Mains = 0;
                const int Resource = Clean.Game->GetRelayStatus(TeamIndex == 0).Resource;
                if (Resource < 0 || Resource > 200 || Resource % 100)
                {
                    Out.Pass = false;
                    Out.Error = TEXT("resource violates independent single-payment units");
                }
                for (int SlotIndex = 0; SlotIndex < Views.Num(); ++SlotIndex)
                {
                    const auto V = Views[SlotIndex];
                    const int PlayerIndex = TeamIndex * 3 + SlotIndex;
                    auto Fighter = Clean.Game->Players[PlayerIndex];
                    Mains += V.Main;
                    Out.Handoff |= V.Main && SlotIndex != 0;
                    Out.Contact |=
                        V.Health < PreviousHealth[PlayerIndex] && Fighter->AttackOwner != nullptr;
                    if (V.Health < 0 || V.Health > Fighter->MaxHealth || V.Recoverable < 0 ||
                        V.Health + V.Recoverable > Fighter->MaxHealth ||
                        (V.Health == 0 && V.Recoverable != 0) || V.Cooldown < 0 ||
                        V.Cooldown > 120 || (V.Main && !V.Visible))
                    {
                        Out.Pass = false;
                        Out.Error = TEXT("health/role/cooldown conservation violated");
                    }
                    if (!(C.A & INP_ResetTraining) && !(C.B & INP_ResetTraining) &&
                        PreviousHealth[PlayerIndex] == 0 && V.Health != 0)
                    {
                        Out.Pass = false;
                        Out.Error = TEXT("KO healed without reset");
                    }
                    PreviousHealth[PlayerIndex] = V.Health;
                }
                if (Mains != 1)
                {
                    Out.Pass = false;
                    Out.Error = TEXT("team must have exactly one selected main");
                }
                if (FrameIndex == 0 || FrameIndex == 4 || FrameIndex == 18 || FrameIndex == 31)
                {
                    const int Wanted = TeamIndex == 0 || FrameIndex >= 4 ? 100 : 200;
                    if (Resource != Wanted)
                    {
                        Out.Pass = false;
                        Out.Error = TEXT("independent staggered opening payment failed");
                    }
                }
            }
            if (!Out.Pass)
            {
                Out.FailFrame = FrameIndex;
                break;
            }
        }
        return Out;
    };
    FString ReproPath;
    if (FParse::Value(FCommandLine::Get(), TEXT("RelayMixedRepro="), ReproPath))
    {
        TArray<FString> Lines;
        if (!TestTrue(TEXT("load retained mixed input trace"),
                      FFileHelper::LoadFileToStringArray(Lines, *ReproPath)))
        {
            return false;
        }
        int Seed = 0, Health = 0;
        if (Lines.Num() < 3 || !FParse::Value(*Lines[0], TEXT("seed="), Seed) ||
            !FParse::Value(*Lines[0], TEXT("health="), Health))
        {
            AddError(TEXT("invalid mixed trace metadata"));
            return false;
        }
        TArray<FCommand> Commands;
        for (int Index = 2; Index < Lines.Num(); ++Index)
        {
            TArray<FString> Cells;
            Lines[Index].ParseIntoArray(Cells, TEXT(","), false);
            if (Cells.Num() != 4 || FCString::Atoi(*Cells[0]) != Commands.Num())
            {
                AddError(TEXT("invalid mixed trace command"));
                return false;
            }
            FCommand C;
            C.A = FCString::Atoi(*Cells[1]);
            C.B = FCString::Atoi(*Cells[2]);
            C.Delay = FCString::Atoi(*Cells[3]);
            if (C.Delay < 0 || C.Delay > 8)
            {
                AddError(TEXT("invalid mixed trace delay"));
                return false;
            }
            Commands.Add(C);
        }
        const auto Result = Run(Seed, Commands, Health);
        if (!Result.Pass)
        {
            AddError(FString::Printf(TEXT("retained mixed trace diverged at %d: %s"),
                                     Result.FailFrame, *Result.Error));
            return false;
        }
        return TestTrue(TEXT("retained trace keeps contact, handoff and changed rollback"),
                        Result.Contact && Result.Handoff && Result.Rollback);
    }
    int First = 39001, Count = 32;
    FParse::Value(FCommandLine::Get(), TEXT("RelayMixedFirstSeed="), First);
    FParse::Value(FCommandLine::Get(), TEXT("RelayMixedSeedCount="), Count);
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/RelayMixed");
    IFileManager::Get().MakeDirectory(*Directory, true);
    for (int Seed = First; Seed < First + Count; ++Seed)
    {
        FRandomStream Random(Seed);
        TArray<FCommand> Commands;
        Commands.SetNum(Random.RandRange(600, 900));
        for (int FrameIndex = 0; FrameIndex < Commands.Num(); ++FrameIndex)
        {
            auto &C = Commands[FrameIndex];
            const int Age = FrameIndex % 200;
            if (Age == 0 && FrameIndex > 0)
            {
                C.A = C.B = INP_ResetTraining;
            }
            else if (Age == 0 || (Age == 1 && FrameIndex > 0))
            {
                C.A = RelaySlot2;
            }
            if (Age == 4)
            {
                C.B = RelaySlot3;
            }
            if (Age == 18)
            {
                C.A = RelaySlot3;
            }
            if (Age == 31)
            {
                C.B = RelaySlot3;
            }
            if (Age >= 48)
            {
                for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
                {
                    int32 &Input = TeamIndex == 0 ? C.A : C.B;
                    const int Choice = Random.RandRange(0, 11);
                    Input = Choice < 3    ? INP_A
                            : Choice == 3 ? INP_B
                            : Choice == 4 ? INP_C
                            : Choice == 5 ? INP_Right
                            : Choice == 6 ? INP_Left
                            : Choice < 10 ? (RelaySlot1 << Random.RandRange(0, 2))
                                          : 0;
                }
            }
            if (FrameIndex >= 48 && Random.RandRange(0, 12) == 0)
            {
                C.Delay = Random.RandRange(2, 8);
            }
        }
        const int Health = Random.RandRange(6000, 9000);
        FOutcome Result = Run(Seed, Commands, Health);
        const FString Base = Directory / FString::Printf(TEXT("seed-%d"), Seed);
        FFileHelper::SaveStringToFile(Result.Ledger, *(Base + TEXT("-ledger.csv")));
        if (!Result.Pass)
        {
            Commands.SetNum(Result.FailFrame + 1);
            int ShrunkHealth = Health;
            // Bounded command/delay/value reduction reruns actual engine encounters.
            // Accept a reduction only if contact, handoff and changed speculative
            // simulation remain, so removing the failure's positive stimulus is invalid.
            for (int Trial = 0; Trial < 18; ++Trial)
            {
                auto Candidate = Commands;
                const int Index = 48 + Trial * FMath::Max(1, (Candidate.Num() - 48) / 18);
                if (Index >= Candidate.Num())
                {
                    continue;
                }
                if (Trial % 3 == 0)
                {
                    Candidate[Index].A = Candidate[Index].B = 0;
                }
                else if (Trial % 3 == 1)
                {
                    Candidate[Index].Delay = Candidate[Index].Delay > 2 ? 2 : 0;
                }
                const int CandidateHealth =
                    Trial % 3 == 2 ? FMath::Max(1, ShrunkHealth / 2) : ShrunkHealth;
                const auto Probe = Run(Seed, Candidate, CandidateHealth);
                if (!Probe.Pass && Probe.Contact && Probe.Handoff && Probe.Rollback)
                {
                    Candidate.SetNum(Probe.FailFrame + 1);
                    Commands = MoveTemp(Candidate);
                    ShrunkHealth = CandidateHealth;
                }
            }
            FString Repro =
                FString::Printf(TEXT("seed=%d,health=%d,asset=native-relay-v2\nframe,a,b,delay\n"),
                                Seed, ShrunkHealth);
            for (int FrameIndex = 0; FrameIndex < Commands.Num(); ++FrameIndex)
            {
                Repro += FString::Printf(TEXT("%d,%d,%d,%d\n"), FrameIndex, Commands[FrameIndex].A,
                                         Commands[FrameIndex].B, Commands[FrameIndex].Delay);
            }
            FFileHelper::SaveStringToFile(Repro, *(Base + TEXT("-reduced.csv")));
            AddError(FString::Printf(
                TEXT("mixed seed %d frame %d: %s; retained actual-engine reduced input trace %s"),
                Seed, Result.FailFrame, *Result.Error, *Base));
            return false;
        }
        if (!TestTrue(TEXT("mixed episode includes real contact, actual handoff and changed "
                           "speculative rollback"),
                      Result.Contact && Result.Handoff && Result.Rollback))
        {
            return false;
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayParticipantKOMatrix,
                                 "NightSky.Relay.ParticipantKOPhaseMatrix",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayParticipantKOMatrix::RunTest(const FString &)
{
    for (bool Routed : {false, true})
    {
        for (int Victim : {0, 1})
        {
            for (int Due : {1, 2, 5, 6, 11, 17, 18, 19, 22, 24, 27, 28, 30, 31, 33, 36})
            {
                if (!Routed && Due > 33)
                {
                    continue;
                }
                RelayTests::FEncounter Encounter;
                RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
                Encounter.GetFighter(0, Victim)->SetHealth(1);
                auto &Hit =
                    Encounter.GetFighter(1, 0)->CollisionData->CollisionFrames[1].Boxes.Last();
                Hit.SizeX = 20000;
                Hit.PosX = 2030000;
                for (int FrameIndex = FMath::Min(0, Due - 3); FrameIndex <= Due; ++FrameIndex)
                {
                    Encounter.Frame(FrameIndex == 0                           ? RelaySlot2
                                    : FrameIndex == 18 && Routed && Due >= 19 ? RelaySlot3
                                                                              : 0,
                                    FrameIndex == Due - 3 ? INP_A : 0);
                }
                TestEqual(TEXT("scheduled ordinary contact kills selected participant"),
                          Encounter.GetFighter(0, Victim)->CurrentHealth, 0);
                TestTrue(TEXT("KO preserves actual attack ownership"),
                         Encounter.GetFighter(0, Victim)->AttackOwner ==
                             Encounter.GetFighter(1, 0));
                TestEqual(TEXT("KO never retains recovery"),
                          Encounter.GetFighter(0, Victim)->RecoverableHealth, 0);
                TestTrue(TEXT("participant KO cancels the active relay"),
                         Encounter.Game->GetRelayStatus(true).Stage == ERelayPhase::Idle);
                TestEqual(TEXT("KO interruption does not refund"),
                          Encounter.Game->GetRelayStatus(true).Resource, 100);
                const int Survivor = Victim == 0 ? 1 : 0;
                TestTrue(TEXT("lowest surviving slot is main even when just placed on cooldown"),
                         Encounter.GetFighter(0, Survivor)->IsMainPlayer());
                TestEqual(TEXT("survivor keeps its identity cooldown"),
                          RelayTests::GetCooldown(Encounter.Game, 0, Survivor), 120);
                TestTrue(TEXT("remaining roster prevents premature loss"),
                         Encounter.Game->BattleState.CurrentWinSide == WIN_None);
                const int EnemyHealth = Encounter.GetFighter(1, 0)->CurrentHealth;
                for (int FrameIndex = 0; FrameIndex < 110; ++FrameIndex)
                {
                    Encounter.Frame();
                }
                TestEqual(TEXT("KO cannot heal over former projectile lifetime"),
                          Encounter.GetFighter(0, Victim)->CurrentHealth, 0);
                TestEqual(TEXT("cancelled sequence cannot produce later damage"),
                          Encounter.GetFighter(1, 0)->CurrentHealth, EnemyHealth);
                const int X = Encounter.GetFighter(0, Survivor)->PosX;
                Encounter.Frame(INP_Right);
                TestEqual(TEXT("survivor regains ordinary movement"),
                          Encounter.GetFighter(0, Survivor)->PosX, X + 1000);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelaySlotRelabel, "NightSky.Relay.EqualStatSlotRelabeling",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelaySlotRelabel::RunTest(const FString &)
{
    RelayTests::FEncounter Original, Relabeled;
    RELAY_REQUIRE_VALID_ENCOUNTER(Original);
    RELAY_REQUIRE_VALID_ENCOUNTER(Relabeled);
    for (auto Encounter : {&Original, &Relabeled})
    {
        for (auto Fighter : Encounter->Game->Players)
        {
            Fighter->MaxHealth = 10000;
            Fighter->SetHealth(10000);
        }
    }
    auto Map = [](int Index) { return Index == 0 ? 0 : 3 - Index; };
    auto InputMap = [](int Input) {
        return (Input & ~(RelaySlot2 | RelaySlot3)) | ((Input & RelaySlot2) ? RelaySlot3 : 0) |
               ((Input & RelaySlot3) ? RelaySlot2 : 0);
    };
    bool Damage = false, Handoff = false;
    for (int FrameIndex = 0; FrameIndex < 140; ++FrameIndex)
    {
        if (FrameIndex == 48)
        {
            for (auto Encounter : {&Original, &Relabeled})
            {
                for (auto Fighter : Encounter->Game->Players)
                {
                    Fighter->PosX = Fighter->PlayerIndex == 0 ? -150000 : 150000;
                }
            }
        }
        const int A = FrameIndex == 0     ? RelaySlot2
                      : FrameIndex == 18  ? RelaySlot3
                      : FrameIndex == 50  ? INP_A
                      : FrameIndex == 110 ? INP_Right
                                          : 0;
        const int B = FrameIndex == 4    ? RelaySlot3
                      : FrameIndex == 31 ? RelaySlot3
                      : FrameIndex == 80 ? INP_A
                                         : 0;
        Original.Frame(A, B);
        Relabeled.Frame(InputMap(A), InputMap(B));
        for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
        {
            auto X = Original.Game->GetRelaySlots(TeamIndex == 0),
                 Y = Relabeled.Game->GetRelaySlots(TeamIndex == 0);
            if (!TestEqual(TEXT("original relabel roster contains all selected fighters"), X.Num(),
                           3))
            {
                return false;
            }
            if (!TestEqual(TEXT("permuted relabel roster contains all selected fighters"), Y.Num(),
                           3))
            {
                return false;
            }
            const auto &RX = Original.Game->GetRelayStatus(TeamIndex == 0);
            const auto &RY = Relabeled.Game->GetRelayStatus(TeamIndex == 0);
            TestEqual(TEXT("equal-stat relabel preserves team resource"), RX.Resource, RY.Resource);
            TestEqual(TEXT("equal-stat relabel preserves phase"), int(RX.Stage), int(RY.Stage));
            TestEqual(TEXT("equal-stat relabel preserves age"), RX.ElapsedFrames, RY.ElapsedFrames);
            for (int Index = 0; Index < 3; ++Index)
            {
                const int J = Map(Index);
                const auto Fighter = Original.GetFighter(TeamIndex, Index),
                           Q = Relabeled.GetFighter(TeamIndex, J);
                TestEqual(TEXT("relabel maps active role"), X[Index].Main, Y[J].Main);
                TestEqual(TEXT("relabel maps exposure"), X[Index].Visible, Y[J].Visible);
                TestEqual(TEXT("relabel maps exact health"), X[Index].Health, Y[J].Health);
                TestEqual(TEXT("relabel maps recovery"), X[Index].Recoverable, Y[J].Recoverable);
                TestEqual(TEXT("relabel maps identity cooldown"), X[Index].Cooldown, Y[J].Cooldown);
                TestEqual(TEXT("relabel preserves resulting movement"), Fighter->PosX, Q->PosX);
                TestEqual(TEXT("relabel preserves combo scaling"), Fighter->TotalProration,
                          Q->TotalProration);
                if (Fighter->AttackOwner || Q->AttackOwner)
                {
                    if (!Fighter->AttackOwner || !Q->AttackOwner)
                    {
                        AddError(TEXT("relabel attack-owner presence differs"));
                    }
                    else if (!Fighter->AttackOwner->Player || !Q->AttackOwner->Player)
                    {
                        // Candidate code owns this pointer graph. A missing player must be a
                        // normal behavioral failure, not a null dereference that truncates the
                        // remaining automation suite.
                        AddError(TEXT("relabel attack owner has no originating player"));
                    }
                    else
                    {
                        TestEqual(TEXT("relabel maps actual attack owner"),
                                  Map(Fighter->AttackOwner->Player->TeamIndex),
                                  Q->AttackOwner->Player->TeamIndex);
                    }
                }
                Damage |= X[Index].Health < 10000;
                Handoff |= X[Index].Main && Index != 0;
            }
        }
    }
    TestTrue(TEXT("relabel equivalence includes actual contact damage and handoff"),
             Damage && Handoff);
    TestEqual(TEXT("relabel control paid actual resource"),
              Original.Game->GetRelayStatus(true).Resource, 100);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayRouteCommandEdges, "NightSky.Relay.RouteCommandEdges",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayRouteCommandEdges::RunTest(const FString &)
{
    // Commands just outside either side of the route window must be discarded,
    // including a press held across the opening boundary.
    for (int InvalidAt : {1, 6, 17, 28, 33})
    {
        RelayTests::FEncounter Encounter;
        for (int FrameIndex = 0; FrameIndex <= 34; ++FrameIndex)
        {
            Encounter.Frame(FrameIndex == 0 ? RelaySlot2
                            : FrameIndex >= InvalidAt ? RelaySlot3 : 0);
        }
        TestTrue(TEXT("out-of-phase edge cannot produce delayed route"),
                 Encounter.GetFighter(0, 0)->IsMainPlayer());
        TestTrue(TEXT("discarded route keeps exact no-route termination"),
                 Encounter.Game->GetRelayStatus(true).Stage == ERelayPhase::Idle);
        TestEqual(TEXT("invalid route spends no extra resource"),
                  Encounter.Game->GetRelayStatus(true).Resource, 100);
        TestEqual(TEXT("discarded target was never enrolled"),
                  RelayTests::GetCooldown(Encounter.Game, 0, 2), 0);
    }
    for (bool Simultaneous : {false, true})
    {
        RelayTests::FEncounter Encounter;
        const int RouteAt = Simultaneous ? 18 : 19;
        for (int FrameIndex = 0; FrameIndex <= RouteAt + 19; ++FrameIndex)
        {
            int Input = FrameIndex == 0 ? RelaySlot2 : 0;
            if (FrameIndex == 18)
                Input = Simultaneous ? RelaySlot2 | RelaySlot3 : RelaySlot1;
            if (!Simultaneous && FrameIndex == 19)
                Input = RelaySlot3;
            // A later competing edge cannot replace the accepted target.
            if (FrameIndex == RouteAt + 2)
                Input = Simultaneous ? RelaySlot3 : RelaySlot2;
            Encounter.Frame(Input);
        }
        TestTrue(TEXT("first valid route wins; simultaneous routes prefer lower slot"),
                 Encounter.GetFighter(0, Simultaneous ? 1 : 2)->IsMainPlayer());
        TestEqual(TEXT("routing an exposed participant needs no second payment"),
                  Encounter.Game->GetRelayStatus(true).Resource, 100);
        TestTrue(TEXT("accepted route terminates on authored deadline"),
                 Encounter.Game->GetRelayStatus(true).Stage == ERelayPhase::Idle);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayResourceExhaustion, "NightSky.Relay.ResourceExhaustion",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayResourceExhaustion::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    for (int Sequence = 0; Sequence < 2; ++Sequence)
    {
        Encounter.Frame(RelaySlot2);
        TestEqual(TEXT("each accepted sequence spends exactly one hundred"),
                  Encounter.Game->GetRelayStatus(true).Resource, 100 - Sequence * 100);
        for (int FrameIndex = 0; FrameIndex < 160; ++FrameIndex)
            Encounter.Frame();
        TestEqual(TEXT("fighter cooldown has fully expired"),
                  RelayTests::GetCooldown(Encounter.Game, 0, 1), 0);
    }
    TestTrue(TEXT("empty resource explains rejection after cooldown expires"),
             Encounter.Game->RelayEligibility(true, 2) == ERelayRejection::Resource);
    Encounter.Frame(RelaySlot2);
    TestEqual(TEXT("third request cannot overdraw resource"),
              Encounter.Game->GetRelayStatus(true).Resource, 0);
    TestTrue(TEXT("rejected request leaves team idle"),
             Encounter.Game->GetRelayStatus(true).Stage == ERelayPhase::Idle);
    TestFalse(TEXT("rejected request does not expose reserve"),
              Encounter.GetFighter(0, 1)->IsOnScreen());
    for (int FrameIndex = 0; FrameIndex < 240; ++FrameIndex)
        Encounter.Frame();
    TestEqual(TEXT("resource does not regenerate during the round"),
              Encounter.Game->GetRelayStatus(true).Resource, 0);
    TestEqual(TEXT("other team resource remains independent"),
              Encounter.Game->GetRelayStatus(false).Resource, 200);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayRejectedEntry, "NightSky.Relay.RejectedEntryConditions",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayRejectedEntry::RunTest(const FString &)
{
    for (int Condition = 0; Condition < 4; ++Condition)
    {
        RelayTests::FEncounter Encounter;
        auto Main = Encounter.GetFighter(0, 0);
        ERelayRejection Reason = ERelayRejection::Main;
        int Input = RelaySlot1;
        if (Condition == 1)
        {
            Main->PosY = 100000;
            Reason = ERelayRejection::Airborne;
            Input = RelaySlot2;
        }
        else if (Condition == 2)
        {
            Main->JumpToStatePrimary(
                FGameplayTag::RequestGameplayTag(TEXT("State.Relay.Synchronized")));
            Reason = ERelayRejection::Busy;
            Input = RelaySlot2;
        }
        else if (Condition == 3)
        {
            // This published tag exists as a projectile state but is absent from
            // the sample fighters' primary authored move states.
            Encounter.Game->ConfigureRelayMoves(
                FGameplayTag::RequestGameplayTag(TEXT("State.Relay.Projectile")),
                FGameplayTag::RequestGameplayTag(TEXT("State.Relay.Followup")));
            Reason = ERelayRejection::MissingMove;
            Input = RelaySlot2;
        }
        TestTrue(TEXT("eligibility explains entry rejection"),
                 Encounter.Game->RelayEligibility(true, Condition == 0 ? 1 : 2) == Reason);
        Encounter.Frame(Input);
        TestEqual(TEXT("rejected entry keeps resource"),
                  Encounter.Game->GetRelayStatus(true).Resource, 200);
        TestTrue(TEXT("rejected entry does not create sequence"),
                 Encounter.Game->GetRelayStatus(true).Stage == ERelayPhase::Idle);
        TestTrue(TEXT("rejected entry preserves selected main"), Main->IsMainPlayer());
        for (int Slot = 1; Slot < 3; ++Slot)
        {
            TestFalse(TEXT("rejected entry exposes no reserve"),
                      Encounter.GetFighter(0, Slot)->IsOnScreen());
            TestEqual(TEXT("rejected entry assigns no cooldown"),
                      RelayTests::GetCooldown(Encounter.Game, 0, Slot), 0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayRecoveryConservation, "NightSky.Relay.RecoveryConservation",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayRecoveryConservation::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    for (int Slot = 0; Slot < 3; ++Slot)
    {
        auto Fighter = Encounter.GetFighter(0, Slot);
        Fighter->SetHealth(Fighter->MaxHealth - 2);
        Fighter->SetRecoverableHealth(20);
    }
    for (int FrameIndex = 1; FrameIndex <= 5; ++FrameIndex)
    {
        Encounter.Frame();
        for (int Slot = 0; Slot < 3; ++Slot)
        {
            auto Fighter = Encounter.GetFighter(0, Slot);
            const int Recovered = Slot == 0 ? 0 : FMath::Min(FrameIndex, 2);
            TestEqual(TEXT("only off-screen reserves recover one point until full"),
                      Fighter->CurrentHealth, Fighter->MaxHealth - 2 + Recovered);
            TestEqual(TEXT("recoverable pool clamps to missing health and is consumed"),
                      Fighter->RecoverableHealth, 2 - Recovered);
            TestTrue(TEXT("health plus recoverable never exceeds maximum"),
                     Fighter->CurrentHealth + Fighter->RecoverableHealth <= Fighter->MaxHealth);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayDeepFrozenQueue, "NightSky.Relay.DeepFrozenQueue",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayDeepFrozenQueue::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
    const int32 Timer = Encounter.Game->BattleState.RoundTimer;
    Encounter.Game->BattleState.SuperFreezeDuration = 64;
    // Twenty refused edges: slot 1 is the main, so each is queued and rejected on the resumed
    // frame. A queue with a small capacity drops or merges these and loses what follows them.
    for (int32 Index = 0; Index < 20; ++Index)
    {
        Encounter.Frame(RelaySlot1);
        Encounter.Frame();
    }
    TestEqual(TEXT("refused queued edges spend nothing"),
              Encounter.Game->GetRelayStatus(true).Resource, 200);
    TestEqual(TEXT("deep queue does not advance the round clock"),
              Encounter.Game->BattleState.RoundTimer, Timer);
    // The two eligible edges arrive last, higher slot first.
    Encounter.Frame(RelaySlot3);
    Encounter.Frame();
    Encounter.Frame(RelaySlot2);
    Encounter.Frame();
    Encounter.Game->BattleState.SuperFreezeDuration = 0;
    Encounter.Frame();
    TestTrue(TEXT("arrival order holds beyond a shallow queue"),
             Encounter.GetFighter(0, 2)->IsOnScreen());
    TestFalse(TEXT("the later lower slot is still discarded"),
              Encounter.GetFighter(0, 1)->IsOnScreen());
    TestEqual(TEXT("a deep queue spends exactly once"),
              Encounter.Game->GetRelayStatus(true).Resource, 100);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayFrozenArrivalOrder, "NightSky.Relay.FrozenArrivalOrder",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayFrozenArrivalOrder::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    const int32 Timer = Encounter.Game->BattleState.RoundTimer;
    Encounter.Game->BattleState.SuperFreezeDuration = 8;
    Encounter.Frame(RelaySlot3);
    Encounter.Frame();
    Encounter.Frame(RelaySlot2);
    Encounter.Frame();
    TestEqual(TEXT("queued commands spend nothing while frozen"),
              Encounter.Game->GetRelayStatus(true).Resource, 200);
    TestEqual(TEXT("freeze suspends round clock"), Encounter.Game->BattleState.RoundTimer, Timer);
    Encounter.Game->BattleState.SuperFreezeDuration = 0;
    Encounter.Frame();
    TestTrue(TEXT("earlier higher slot edge wins over later lower slot edge"),
             Encounter.GetFighter(0, 2)->IsOnScreen());
    TestFalse(TEXT("later edge is discarded in entry phase"),
              Encounter.GetFighter(0, 1)->IsOnScreen());
    TestEqual(TEXT("resumed arrival queue spends exactly once"),
              Encounter.Game->GetRelayStatus(true).Resource, 100);
    for (int FrameIndex = 1; FrameIndex <= 34; ++FrameIndex)
        Encounter.Frame();
    TestTrue(TEXT("discarded frozen edge cannot route when window opens"),
             Encounter.GetFighter(0, 0)->IsMainPlayer());
    TestEqual(TEXT("discarded frozen target receives no cooldown"),
              RelayTests::GetCooldown(Encounter.Game, 0, 1), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayKOScope, "NightSky.Relay.KOCancellationScope",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayKOScope::RunTest(const FString &)
{
    for (bool CurrentParticipant : {false, true})
    {
        RelayTests::FEncounter Encounter;
        Encounter.Frame(RelaySlot2);
        // Isolate the first exposed reserve from its main before ordinary contact.
        Encounter.GetFighter(0, 1)->PosX = -150000;
        Encounter.GetFighter(1, 0)->PosX = 150000;
        Encounter.Frame(0, INP_A);
        for (int FrameIndex = 0; FrameIndex < 3; ++FrameIndex)
            Encounter.Frame();
        if (!TestTrue(TEXT("real reserve hit cancelled the earlier sequence"),
                      Encounter.Game->GetRelayStatus(true).Stage == ERelayPhase::Idle) ||
            !TestTrue(TEXT("former participant remains exposed in hit reaction"),
                      Encounter.GetFighter(0, 1)->IsOnScreen()) ||
            !TestTrue(TEXT("former participant has actual opponent contact ownership"),
                      Encounter.GetFighter(0, 1)->AttackOwner == Encounter.GetFighter(1, 0)))
            return false;
        Encounter.Frame(RelaySlot3);
        if (!TestTrue(TEXT("unharmed main starts a new sequence with the other reserve"),
                      Encounter.Game->GetRelayStatus(true).Stage == ERelayPhase::Entry))
            return false;
        // Public health setup isolates KO scope from another hit interruption.
        // The old reserve is visible, but only slot 3 belongs to this sequence.
        const int Victim = CurrentParticipant ? 2 : 1;
        Encounter.GetFighter(0, Victim)->SetHealth(0);
        Encounter.Frame();
        TestEqual(TEXT("KO consumes all recoverable health"),
                  Encounter.GetFighter(0, Victim)->RecoverableHealth, 0);
        TestEqual(TEXT("KO does not refund either accepted sequence"),
                  Encounter.Game->GetRelayStatus(true).Resource, 0);
        TestTrue(TEXT("scope follows current enrollment rather than all visible fighters"),
                 Encounter.Game->GetRelayStatus(true).Stage ==
                     (CurrentParticipant ? ERelayPhase::Idle : ERelayPhase::Entry));
        TestTrue(TEXT("surviving original main remains selected"),
                 Encounter.GetFighter(0, 0)->IsMainPlayer());
        TestTrue(TEXT("a reserve KO does not end the match"),
                 Encounter.Game->BattleState.CurrentWinSide == WIN_None);
    }
    return true;
}

// ---------------------------------------------------------------------------
// QA additions (2026-09-15): reward coverage for the public entry points declared
// in RELAY_INTERFACE.md and for the HUD paint callback. Mutation matrix on
// 8b27f5ed showed the 29-test suite passes with RelayButton / RelaySampleSlots
// rejecting every input (m13, m14) and with the overlay paint loop emptied (m10).
// ---------------------------------------------------------------------------
#include "EngineUtils.h"
#include "Internationalization/Regex.h"
#include "NightSkyEngine/UI/RelayOverlay.h"
#include "Blueprint/UserWidget.h"
#include "Input/HittestGrid.h"
#include "Types/PaintArgs.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Styling/WidgetStyle.h"

namespace RelayTests
{
    static bool ContainsNumberToken(const FString &Text, int32 Value)
    {
        FRegexPattern Pattern(FString::Printf(TEXT("(^|[^0-9])%d([^0-9]|$)"), Value));
        FRegexMatcher Matcher(Pattern, Text);
        return Matcher.FindNext();
    }
} // namespace RelayTests

// The documented Blueprint/console binding must drive the same relay command edges
// as the input word: press starts a relay and pays once, holding does not retrigger,
// release clears the bit, out-of-range slots do nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayPublicButtonBinding, "NightSky.Relay.PublicButtonBinding",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayPublicButtonBinding::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    TArray<ANightSkyPlayerController *> Controllers;
    for (TActorIterator<ANightSkyPlayerController> It(Encounter.World); It; ++It)
    {
        Controllers.Add(*It);
    }
    if (!TestEqual(TEXT("encounter spawns two player controllers"), Controllers.Num(), 2))
    {
        return false;
    }
    ANightSkyPlayerController *P1 = Controllers[0];
    P1->Inputs = 0;
    P1->RelayButton(0, true);
    P1->RelayButton(4, true);
    TestEqual(TEXT("out-of-range slots set no relay bit"), P1->Inputs & RelayInputMask, 0);
    P1->RelayButton(2, true);
    TestEqual(TEXT("press maps slot two to its input bit"), P1->Inputs & RelayInputMask, RelaySlot2);
    Encounter.Frame(P1->Inputs, 0);
    auto Status = Encounter.Game->GetRelayStatus(true);
    TestTrue(TEXT("public press starts a relay on its frame"),
             Status.Stage == ERelayPhase::Entry && Status.ElapsedFrames == 0);
    TestEqual(TEXT("public press pays the relay cost once"), Status.Resource, 100);
    TestTrue(TEXT("public press exposes the selected reserve"), Encounter.GetFighter(0, 1)->IsOnScreen());
    for (int32 Frame = 0; Frame < 4; ++Frame)
    {
        Encounter.Frame(P1->Inputs, 0); // still held
    }
    TestEqual(TEXT("held button does not retrigger or pay again"),
              Encounter.Game->GetRelayStatus(true).Resource, 100);
    P1->RelayButton(2, false);
    TestEqual(TEXT("release clears the slot bit"), P1->Inputs & RelayInputMask, 0);
    P1->RelayButton(3, true);
    TestEqual(TEXT("press maps slot three"), P1->Inputs & RelayInputMask, RelaySlot3);
    P1->RelayButton(3, false);
    P1->RelayButton(1, true);
    TestEqual(TEXT("press maps slot one"), P1->Inputs & RelayInputMask, RelaySlot1);
    P1->RelayButton(1, false);
    TestEqual(TEXT("all releases leave no relay bit"), P1->Inputs & RelayInputMask, 0);
    return true;
}

// The documented sample entry command must select both rosters in slot order and
// reject out-of-range choices without touching the configured battle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelaySampleSlotSelection, "NightSky.Relay.SampleSlotSelection",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelaySampleSlotSelection::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter; // pending level travel is discarded with the encounter's world
    UNightSkyGameInstance *Instance = Encounter.Instance;
    UPrimaryCharaData *One = GetMutableDefault<URelaySampleOne>();
    UPrimaryCharaData *Two = GetMutableDefault<URelaySampleTwo>();
    UPrimaryCharaData *Three = GetMutableDefault<URelaySampleThree>();
    const int32 BeforeP1 = Instance->BattleData.PlayerListP1.Num();
    const UPrimaryCharaData *BeforeFirst = Instance->BattleData.PlayerListP1.Num() ? Instance->BattleData.PlayerListP1[0].Get() : nullptr;
    Instance->RelaySampleSlots(0, 2, 3, 1, 2, 3, true);
    Instance->RelaySampleSlots(1, 2, 3, 1, 4, 3, true);
    TestEqual(TEXT("invalid choices leave the roster size untouched"), Instance->BattleData.PlayerListP1.Num(), BeforeP1);
    TestTrue(TEXT("invalid choices leave the roster untouched"),
             (Instance->BattleData.PlayerListP1.Num() ? Instance->BattleData.PlayerListP1[0].Get() : nullptr) == BeforeFirst);
    Instance->RelaySampleSlots(3, 1, 2, 2, 3, 1, true);
    if (TestEqual(TEXT("first team has three slots"), Instance->BattleData.PlayerListP1.Num(), 3) &&
        TestEqual(TEXT("second team has three slots"), Instance->BattleData.PlayerListP2.Num(), 3))
    {
        TestTrue(TEXT("first team slot one"), Instance->BattleData.PlayerListP1[0].Get() == Three);
        TestTrue(TEXT("first team slot two"), Instance->BattleData.PlayerListP1[1].Get() == One);
        TestTrue(TEXT("first team slot three"), Instance->BattleData.PlayerListP1[2].Get() == Two);
        TestTrue(TEXT("second team slot one"), Instance->BattleData.PlayerListP2[0].Get() == Two);
        TestTrue(TEXT("second team slot two"), Instance->BattleData.PlayerListP2[1].Get() == Three);
        TestTrue(TEXT("second team slot three"), Instance->BattleData.PlayerListP2[2].Get() == One);
    }
    TestTrue(TEXT("sample entry selects the relay format"), Instance->BattleData.BattleFormat == EBattleFormat::Relay);
    TestTrue(TEXT("sample entry honours the training flag"), Instance->IsTraining);
    Instance->RelaySampleSlots(1, 2, 3, 1, 2, 3, false);
    TestFalse(TEXT("sample entry can select a match"), Instance->IsTraining);
    return true;
}

// The overlay's paint callback must submit the team resource and every fighter's
// health, and must change when the battle state changes. Numbers are matched as
// whole tokens; no labels, vocabulary or layout are required.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayOverlayReflectsRolesAndStatus,
                                 "NightSky.Relay.OverlayReflectsRolesAndStatus",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayOverlayReflectsRolesAndStatus::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    RELAY_REQUIRE_VALID_ENCOUNTER(Encounter);
    URelayOverlay *Overlay =
        CreateWidget<URelayOverlay>(Encounter.World, URelayOverlay::StaticClass());
    if (!TestNotNull(TEXT("overlay widget"), Overlay))
    {
        return false;
    }
    Overlay->Battle = Encounter.Game;
    Overlay->TakeWidget();
    auto Paint = [&]() {
        FHittestGrid Grid;
        FPaintArgs Args(nullptr, Grid, FVector2f::ZeroVector, 0.0, 0.0f);
        const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(1280.f, 720.f), FSlateLayoutTransform());
        TSharedPtr<SWindow> NoWindow;
        FSlateWindowElementList Elements(NoWindow);
        FWidgetStyle Style;
        Overlay->TakeWidget()->Paint(Args, Geometry, FSlateRect(0.f, 0.f, 1280.f, 720.f), Elements,
                                     0, Style, true);
        return FString::Join(Overlay->PaintedText, TEXT("\n"));
    };
    // Every per-slot number the interface requires the HUD to show. Vocabulary is not required,
    // so a HUD that words its labels differently still passes; the values must be present.
    auto PaintShowsEverySlotNumber = [&](const TCHAR *Label) {
        const FString Painted = Paint();
        for (int32 Team = 0; Team < 2; ++Team)
        {
            for (const auto &View : Encounter.Game->GetRelaySlots(Team == 0))
            {
                TestTrue(FString::Printf(TEXT("%s shows health for slot %d"), Label, View.Slot),
                         RelayTests::ContainsNumberToken(Painted, View.Health));
                TestTrue(FString::Printf(TEXT("%s shows recoverable health for slot %d"), Label, View.Slot),
                         RelayTests::ContainsNumberToken(Painted, View.Recoverable));
                TestTrue(FString::Printf(TEXT("%s shows cooldown for slot %d"), Label, View.Slot),
                         RelayTests::ContainsNumberToken(Painted, View.Cooldown));
            }
            TestTrue(FString::Printf(TEXT("%s shows team %d resource"), Label, Team + 1),
                     RelayTests::ContainsNumberToken(Painted, Encounter.Game->GetRelayStatus(Team == 0).Resource));
        }
        return Painted;
    };

    Encounter.Frame();
    PaintShowsEverySlotNumber(TEXT("at rest"));

    // "Rejection changes nothing and the HUD explains why". Slot 1 is the main, so requesting it
    // is refused. The interface reports the *last* rejection, so it persists by design and this
    // does not assert when it clears; what it does assert is that nothing moved.
    const int32 ResourceBefore = Encounter.Game->GetRelayStatus(true).Resource;
    Encounter.Frame(RelaySlot1);
    PaintShowsEverySlotNumber(TEXT("after a refusal"));
    TestTrue(TEXT("the refusal is reported as the last rejection"),
             Encounter.Game->GetRelayStatus(true).Rejection != ERelayRejection::None);
    TestEqual(TEXT("a refusal spends nothing"),
              Encounter.Game->GetRelayStatus(true).Resource, ResourceBefore);
    TestFalse(TEXT("a refusal exposes nobody"), Encounter.GetFighter(0, 1)->IsOnScreen());

    // Acceptance changes roles: the main hands off and a reserve becomes exposed. The HUD must
    // follow, and every number must still be shown for the new arrangement.
    Encounter.Frame(RelaySlot2);
    const FString Accepted = PaintShowsEverySlotNumber(TEXT("after acceptance"));
    TestTrue(TEXT("the accepted reserve is exposed"), Encounter.GetFighter(0, 1)->IsOnScreen());
    TestTrue(TEXT("the payment is shown"), RelayTests::ContainsNumberToken(Accepted, 100));

    // Eligibility is state, not decoration: while the team is mid-relay its reserves are not
    // eligible, and the HUD must not still be showing the resting arrangement.
    bool AnyEligible = false;
    for (const auto &View : Encounter.Game->GetRelaySlots(true))
    {
        AnyEligible = AnyEligible || View.Eligible;
    }
    TestFalse(TEXT("no reserve is eligible while the relay is running"), AnyEligible);

    // The second team is untouched throughout, so its own numbers must never have moved.
    for (const auto &View : Encounter.Game->GetRelaySlots(false))
    {
        TestEqual(TEXT("the uninvolved team keeps its cooldown"), View.Cooldown, 0);
    }
    TestEqual(TEXT("the uninvolved team keeps its resource"),
              Encounter.Game->GetRelayStatus(false).Resource, 200);

    // A KO changes a role to a terminal one; the HUD must reflect that too.
    Encounter.GetFighter(0, 1)->CurrentHealth = 0;
    Encounter.Frame();
    const FString AfterKO = PaintShowsEverySlotNumber(TEXT("after a KO"));
    TestTrue(TEXT("the KO'd fighter's health is shown"),
             RelayTests::ContainsNumberToken(AfterKO, 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelayOverlayPaintsTeams, "NightSky.Relay.OverlayPaintsTeams",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FRelayOverlayPaintsTeams::RunTest(const FString &)
{
    RelayTests::FEncounter Encounter;
    URelayOverlay *Overlay = CreateWidget<URelayOverlay>(Encounter.World, URelayOverlay::StaticClass());
    if (!TestNotNull(TEXT("overlay widget"), Overlay))
    {
        return false;
    }
    Overlay->Battle = Encounter.Game;
    Overlay->TakeWidget();
    auto Paint = [&]() {
        FHittestGrid Grid;
        FPaintArgs Args(nullptr, Grid, FVector2f::ZeroVector, 0.0, 0.0f);
        const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(1280.f, 720.f), FSlateLayoutTransform());
        TSharedPtr<SWindow> NoWindow;
        FSlateWindowElementList Elements(NoWindow);
        FWidgetStyle Style;
        // Paint through the public Slate entry point (SObjectWidget routes to UUserWidget::NativePaint);
        // NativePaint itself is protected on UE 5.8.
        Overlay->TakeWidget()->Paint(Args, Geometry, FSlateRect(0.f, 0.f, 1280.f, 720.f), Elements, 0, Style, true);
        return FString::Join(Overlay->PaintedText, TEXT("\n"));
    };
    Encounter.Frame();
    const FString Before = Paint();
    TestTrue(TEXT("paint submits HUD text"), Overlay->PaintedText.Num() > 0);
    TestEqual(TEXT("paint records the painted frame"), Overlay->PaintedFrame, Encounter.Game->BattleState.FrameNumber);
    TestTrue(TEXT("paint shows the full team resource"), RelayTests::ContainsNumberToken(Before, 200));
    for (int32 Team = 0; Team < 2; ++Team)
    {
        for (const auto &View : Encounter.Game->GetRelaySlots(Team == 0))
        {
            TestTrue(TEXT("paint shows each fighter's health value"), RelayTests::ContainsNumberToken(Before, View.Health));
        }
    }
    Encounter.Frame(RelaySlot2);
    const FString After = Paint();
    TestTrue(TEXT("paint reflects the paid resource"), RelayTests::ContainsNumberToken(After, 100));
    TestTrue(TEXT("paint changes with the battle state"), After != Before);
    return true;
}

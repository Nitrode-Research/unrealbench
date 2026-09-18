#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NightSkyEngine/Fixtures/NSE005Fixture.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Battle/Playtest/SweptProjectilePlaytest.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSweptPlaytestFireTest, "UnrealBench.NSE005.Playtest.FireAndReuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSweptPlaytestFireTest::RunTest(const FString&)
{
    FNSE005Battle B;
    B.Projectile->ResetObject();
    B.Game->GameInstance->bSweptProjectilePlaytest = true;
    B.Game->Players[0]->PosX = -297500;
    B.Game->Players[0]->PosY = 0;
    B.Targets[0]->PosX = 297500;
    B.Targets[0]->PosY = 240000;
    NSEPlaytest::Prepare(B.Game);
    auto* Controller = B.World->SpawnActor<ANightSkyPlayerController>();
    Controller->PressPlaytestFire();
    TestTrue(TEXT("fire binding sets battle input"), (Controller->Inputs & NSEPlaytest::FireInput) != 0);
    B.Step(Controller->Inputs);
    TestEqual(TEXT("one press spawns one shot"), B.Game->BattleState.PlaytestShots, 1);
    TestTrue(TEXT("spawned projectile opts in to sweep"), B.Projectile->SweptProjectile);
    B.Step(Controller->Inputs);
    TestEqual(TEXT("first moving endpoint is before target"), B.Targets[0]->CurrentHealth, 10000);
    B.Step(Controller->Inputs);
    TestEqual(TEXT("crossed target takes ordinary swept damage"), B.Targets[0]->CurrentHealth, 9600);
    TestEqual(TEXT("one actual contact callback"), B.Game->BattleState.PlaytestHits, 1);
    for (int32 I = 0; I < 12; ++I) B.Step(Controller->Inputs);
    TestEqual(TEXT("holding fire does not repeat"), B.Game->BattleState.PlaytestShots, 1);
    Controller->ReleasePlaytestFire();
    B.Step(Controller->Inputs);
    Controller->PressPlaytestFire();
    for (int32 I = 0; I < 3; ++I) B.Step(Controller->Inputs);
    TestEqual(TEXT("release and press fires again"), B.Game->BattleState.PlaytestShots, 2);
    TestEqual(TEXT("reused projectile can hit same target"), B.Game->BattleState.PlaytestHits, 2);
    TestEqual(TEXT("second shot deals damage"), B.Targets[0]->CurrentHealth, 9200);
    B.Game->GameInstance->IsTraining = true;
    Controller->ReleasePlaytestFire();
    Controller->ResetTraining();
    B.Step(Controller->Inputs);
    TestEqual(TEXT("reset restores target health"), B.Targets[0]->CurrentHealth, 10000);
    TestEqual(TEXT("reset clears shot count"), B.Game->BattleState.PlaytestShots, 0);
    TestEqual(TEXT("reset clears contact count"), B.Game->BattleState.PlaytestHits, 0);
    TestFalse(TEXT("reset clears pooled projectile"), B.Projectile->IsActive);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSweptPlaytestRollbackTest, "UnrealBench.NSE005.Playtest.InputRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSweptPlaytestRollbackTest::RunTest(const FString&)
{
    FNSE005Battle B;
    B.Projectile->ResetObject();
    B.Game->GameInstance->bSweptProjectilePlaytest = true;
    NSEPlaytest::Prepare(B.Game);
    FRollbackData Snapshot;
    int32 Checksum = 0;
    B.Game->SaveGameState(Snapshot, &Checksum);
    B.Step(NSEPlaytest::FireInput);
    TestEqual(TEXT("first continuation fires"), B.Game->BattleState.PlaytestShots, 1);
    B.Game->LoadGameState(Snapshot);
    TestEqual(TEXT("shot counter restores"), B.Game->BattleState.PlaytestShots, 0);
    B.Step(NSEPlaytest::FireInput);
    TestEqual(TEXT("restored input edge fires again"), B.Game->BattleState.PlaytestShots, 1);
    B.Game->LoadGameState(Snapshot);
    B.Step();
    TestEqual(TEXT("changed continuation keeps no discarded shot"), B.Game->BattleState.PlaytestShots, 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSweptPlaytestPresentationTest, "UnrealBench.NSE005.Playtest.CasterPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSweptPlaytestPresentationTest::RunTest(const FString&)
{
    FNSE005Battle B;
    B.Projectile->ResetObject();
    B.Game->GameInstance->bSweptProjectilePlaytest = true;
    float Time = -1;
    auto* Shooter = B.Game->BattleState.MainPlayer[0];
    auto* Target = B.Game->BattleState.MainPlayer[1];
    TestFalse(TEXT("idle shooter has no cast override"), NSEPlaytest::CastPoseTime(Shooter, Time));
    B.Game->UpdateGameState(NSEPlaytest::FireInput | INP_A, INP_A, false);
    TestEqual(TEXT("shared F mapping cannot attack as the target"), Target->Inputs & INP_A, 0);
    TestEqual(TEXT("fire suppresses the competing normal attack"), Shooter->Inputs & INP_A, 0);
    TestTrue(TEXT("fire animates the shooter"), NSEPlaytest::CastPoseTime(Shooter, Time));
    TestFalse(TEXT("victim never receives the casting animation"), NSEPlaytest::CastPoseTime(Target, Time));
    B.Game->GameInstance->IsTraining = true;
    Target->PrimaryStateMachine.CurrentState->StateType = EStateType::Hitstun;
    Target->PlayerFlags |= PLF_IsStunned;
    Target->Update();
    TestEqual(TEXT("stunned training dummy must not auto-counterattack"), Target->Inputs & INP_A, 0);
    const FVector Right = NSEPlaytest::FistPosition(Shooter);
    Shooter->Direction = DIR_Left;
    const FVector Left = NSEPlaytest::FistPosition(Shooter);
    TestTrue(TEXT("meshless fallback mirrors with facing"), !Left.Equals(Right));
    B.Game->BattleState.FrameNumber = B.Game->BattleState.PlaytestLastShotFrame + 26;
    TestFalse(TEXT("casting override ends after recovery"), NSEPlaytest::CastPoseTime(Shooter, Time));
    B.Game->BattleState.PlaytestShots = 0;
    B.Game->BattleState.FrameNumber = B.Game->BattleState.PlaytestLastShotFrame;
    TestFalse(TEXT("reset discards presentation"), NSEPlaytest::CastPoseTime(Shooter, Time));
    return true;
}
#endif

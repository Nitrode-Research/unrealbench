#if WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../Modifiers/ModifierArena.h"
#include "NightSkyEngine/UI/ModifierSetupWidget.h"
#include "NightSkyEngine/Miscellaneous/ReplayInfo.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SOverlay.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierArenaAuthoring,"NightSky.Modifiers.ArenaAuthoringAndReplay",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FModifierArenaAuthoring::RunTest(const FString&)
{
 auto* GI=NewObject<UModifierFixtureGameInstance>(GEngine);GI->InitializeStandalone();
 auto* W=GI->GetWorld();auto* Viewport=NewObject<UGameViewportClient>(GEngine);GI->GetWorldContext()->GameViewport=Viewport;Viewport->Init(*GI->GetWorldContext(),GI,false);Viewport->SetViewportOverlayWidget(nullptr,SNew(SOverlay));
 GI->IsTraining=true;GI->FighterRunner=LocalPlay;GI->BattleData.PlayerListP1={GetMutableDefault<UModifierArenaCharacter>()};GI->BattleData.PlayerListP2=GI->BattleData.PlayerListP1;GI->BattleData.Stage=GetMutableDefault<UModifierArenaStage>();GI->BattleData.BattleFormat=EBattleFormat::Rounds;GI->BattleData.TimeUntilRoundStart=0;GI->BattleData.StartRoundTimer=99;
 FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();W->SpawnActor<ANightSkyPlayerController>();W->SpawnActor<ANightSkyPlayerController>();
 auto* B=W->SpawnActor<AModifierArenaBattle>();W->SetGameState(B);B->SetPaused(true);
 auto* UI=CreateWidget<UModifierSetupWidget>(GI);UI->AllowAuthoring=true;
 FModifierConfiguration C;FModifierDefinition D;D.Identifier=TEXT("AuthoredDamage");D.DisplayName=TEXT("Damage amplifier");D.Revision=7;D.Operations={{EModifierOperation::Add,7,1,false},{EModifierOperation::Multiply,3,2,false}};C.Definitions.Add(D);FModifierInterval I;I.Identifier=D.Identifier;I.Revision=7;I.Duration=120;C.Schedule.Add(I);
 UI->ConfigureForMatch(C);UI->TakeWidget();
 TestTrue(TEXT("public UI edits schedule"),UI->SetInterval(0,1,0,0));TestFalse(TEXT("zero-duration UI blocks gameplay"),UI->StartSelectedMatch());TestTrue(TEXT("rejection names authored rule"),UI->GetSetupError().ToString().Contains(D.Identifier));TestTrue(TEXT("battle remains paused"),B->bPauseGame);
 UI->SetInterval(0,1,0,120);TestTrue(TEXT("authoring installs exact custom definition and starts match"),UI->StartSelectedMatch());
 TestEqual(TEXT("native round extension provides visible starting meter"),B->BattleState.Meter[0],1000);
 const int32 Before=B->GetMainPlayer(false)->CurrentHealth;
 for(int32 F=0;F<20;++F)B->UpdateGameState(F<1?INP_A:0,0,false);
 TestEqual(TEXT("real authored attack uses add then multiply"),Before-B->GetMainPlayer(false)->CurrentHealth,42);
 const FString Slot=TEXT("AUTOMATION_MOD125_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
 TestTrue(TEXT("save native arena replay"),GI->SaveRecordedReplay(Slot));auto* Tape=Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(Slot,0));
 if(TestNotNull(TEXT("persisted arena tape"),Tape)){TestEqual(TEXT("persist exact rules and revisions"),Tape->BattleData.Modifiers.Canonical(),C.Canonical());TestTrue(TEXT("native arena stage survives save"),Tape->BattleData.Stage==GetMutableDefault<UModifierArenaStage>());TestEqual(TEXT("input pairs persisted"),Tape->LengthInFrames,20);}
 GI->IsTraining=true;B->MatchInit();TestEqual(TEXT("rematch restores full meter"),B->BattleState.Meter[0],1000);TestEqual(TEXT("rematch clears schedule frame"),B->GetPlayableRoundFrame(),-1);
 UGameplayStatics::DeleteGameInSlot(Slot,0);GI->IsReplay=true;W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);GEngine->DestroyWorldContext(W);GI->Shutdown();return true;
}
#endif

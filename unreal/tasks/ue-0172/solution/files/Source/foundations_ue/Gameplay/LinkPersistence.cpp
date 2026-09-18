#include "Gameplay/LinkPlayerController.h"
#include "Gameplay/LinkCharacter.h"
#include "Gameplay/LinkSession.h"
#include "Gameplay/LinkSaveGame.h"
#include "Gameplay/LinkInteractionActor.h"
#include "Gameplay/LinkSceneLayout.h"
#include "Engine/GameInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
const FString ManualSlot=TEXT("Foundations-Manual");const FString AutomaticSlot=TEXT("Foundations-Auto");
bool IsFixtureRun(){return GIsAutomationTesting||FParse::Param(FCommandLine::Get(),TEXT("FoundationsSmoke"))||FParse::Param(FCommandLine::Get(),TEXT("FoundationsInteractionCameraCheck"));}
bool WriteSnapshot(const FLinkSaveSnapshot& Snapshot,const FString& Slot)
{
    auto* Save=Cast<ULinkSaveGame>(UGameplayStatics::CreateSaveGameObject(ULinkSaveGame::StaticClass()));
    Save->Payload=LinkSave::Encode(Snapshot);return UGameplayStatics::SaveGameToSlot(Save,Slot,0);
}
}
bool ALinkPlayerController::SaveToSlot(const FString& Slot)
{
    if(!GetCharacterAt(0)||!GetCharacterAt(1)){return false;}
    auto* Session=GetGameInstance()->GetSubsystem<ULinkSession>();FLinkSaveSnapshot Snapshot;
    Session->CaptureFacts(Snapshot);Snapshot.Map=UGameplayStatics::GetCurrentLevelName(this);
    for(int32 Index=0;Index<2;++Index)
    {
        Snapshot.Positions[Index]=GetCharacterAt(Index)->GetActorLocation();Snapshot.Yaws[Index]=GetCharacterAt(Index)->GetActorRotation().Yaw;
    }
    return WriteSnapshot(Snapshot,Slot);
}
bool ALinkPlayerController::LoadFromSlot(const FString& Slot)
{
    const auto* Save=Cast<ULinkSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
    auto* Session=GetGameInstance()->GetSubsystem<ULinkSession>();FLinkSaveSnapshot Snapshot;FString Error;
    if(!Save||!LinkSave::Decode(Save->Payload,Session->GetDatabase(),Snapshot,Error))
    {
        Feedback=Save?Error:TEXT("No saved game is available.");++ViewRevision;return false;
    }
    const FName Map(*Snapshot.Map);Session->StageRestore(MoveTemp(Snapshot));SetPause(false);
    UGameplayStatics::OpenLevel(this,Map);return true;
}
void ALinkPlayerController::QuickSave(){if(bTransitioning){return;}Feedback=SaveToSlot(ManualSlot)?TEXT("Game saved."):TEXT("Could not save the game.");++ViewRevision;}
void ALinkPlayerController::QuickLoad()
{
    if(bTransitioning){return;}
    const FString Slot=UGameplayStatics::DoesSaveGameExist(ManualSlot,0)?ManualSlot:AutomaticSlot;LoadFromSlot(Slot);
}
void ALinkPlayerController::AutoSave()
{
    // Automation must never read or overwrite a player's normal save slots.
    if(!IsFixtureRun()&&!SaveToSlot(AutomaticSlot)){Feedback=TEXT("Automatic save failed.");}
}
bool ALinkPlayerController::IsRunCompleted() const{return GetGameInstance()->GetSubsystem<ULinkSession>()->IsRunCompleted();}
void ALinkPlayerController::CompleteRoute()
{
    if(!ActiveInteraction){return;}
    const FName Destination=ActiveInteraction->DestinationMap;const bool bComplete=ActiveInteraction->bCompletesRun;
    CloseInteractionView();bRouteRequested=false;
    if(!Destination.IsNone()){TravelToMap(Destination);}
    else if(bComplete)
    {
        GetGameInstance()->GetSubsystem<ULinkSession>()->SetRunCompleted(true);
        for(int32 Index=0;Index<2;++Index){LeaveInteraction(Index);if(auto* Crew=GetCharacterAt(Index)){Crew->SettleForCompletion();}}
        Feedback=TEXT("The route is clear. You found a way into the plant.");SetPause(true);AutoSave();++ViewRevision;
    }
}
void ALinkPlayerController::TravelToMap(FName Map)
{
    if(Map!=TEXT("LoadingDocks")&&Map!=TEXT("EntryGate")){Feedback=TEXT("That route is unavailable.");return;}
    bTransitioning=true;SetPause(false);Commands.Reset();PerformedButtons=0;
    FLinkSaveSnapshot Snapshot;auto* Session=GetGameInstance()->GetSubsystem<ULinkSession>();Session->CaptureFacts(Snapshot);
    Snapshot.Map=Map.ToString();Snapshot.Positions[0]=FVector(0,-180,110);Snapshot.Positions[1]=FVector(0,180,110);
    Session->StageRestore(Snapshot);if(!IsFixtureRun()){WriteSnapshot(Snapshot,AutomaticSlot);}
    for(int32 Index=0;Index<2;++Index){LeaveInteraction(Index);if(auto* Crew=GetCharacterAt(Index)){Crew->SetMotionMode(ELinkMotionMode::Idle);Crew->StopNavigation();}}
    if(PlayerCameraManager){PlayerCameraManager->StartCameraFade(0,1,0.35f,FLinearColor::Black,false,true);}
    GetWorldTimerManager().SetTimer(TransitionTimer,FTimerDelegate::CreateWeakLambda(this,[this,Map](){UGameplayStatics::OpenLevel(this,Map);}),0.4f,false);
}
void ALinkPlayerController::RestartRun()
{
    if(bTransitioning){return;}
    FLinkSaveSnapshot NewGame;NewGame.Map=TEXT("EntryGate");NewGame.Globals.Add(LinkFacts::RTItem,LinkFacts::Gun);
    NewGame.Positions[0]=FVector(0,-180,110);NewGame.Positions[1]=FVector(0,180,110);
    if (auto* Layout=ALinkSceneLayout::Find(GetWorld()))
    {
        NewGame.Map=UGameplayStatics::GetCurrentLevelName(this);
        NewGame.bUseSceneStart=true;
        // Restart consumes only the new facts; scene-owned starting poses/chain
        // load naturally after the map is opened again.
        for (int32 Index=0;Index<2;++Index) { FTransform Start; if (Layout->GetPlayerStart(Index,Start)) { NewGame.Positions[Index]=Start.GetLocation(); NewGame.Yaws[Index]=Start.Rotator().Yaw; } }
    }
    const FName RestartMap(*NewGame.Map);
    GetGameInstance()->GetSubsystem<ULinkSession>()->StageRestore(MoveTemp(NewGame));SetPause(false);
    UGameplayStatics::OpenLevel(this,RestartMap);
}

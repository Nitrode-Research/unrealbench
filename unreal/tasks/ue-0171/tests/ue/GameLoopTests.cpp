// Continuous integration: no substituted rules, granted puzzle facts or teleported visits.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Gameplay/LinkPlayerController.h"
#include "Gameplay/LinkCharacter.h"
#include "Gameplay/LinkChain.h"
#include "Gameplay/LinkInteractionActor.h"
#include "Gameplay/LinkSession.h"
#include "Gameplay/LinkSaveGame.h"
#include "Camera/CameraActor.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationEditorCommon.h"

struct FTripletVisit { FString Map, Interaction; int32 Player; TArray<int32> Actions; };
class FTripletContinuousRun : public IAutomationLatentCommand
{
public:
    explicit FTripletContinuousRun(FAutomationTestBase* InTest):Test(InTest){}
    ~FTripletContinuousRun(){ if(!Slot.IsEmpty()){UGameplayStatics::DeleteGameInSlot(Slot,0);} }
    bool Update() override
    {
        const double Now=FPlatformTime::Seconds();if(Start<0){Start=Now;Slot=TEXT("GEB-Triplet-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);}
        if(Now-Start>180){Test->AddError(FString::Printf(TEXT("Continuous route timed out: visit %d action %d phase %d"),Visit,Action,Phase));return true;}
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* PC=World?Cast<ALinkPlayerController>(World->GetFirstPlayerController()):nullptr;
        if(!PC||!PC->GetCharacterAt(0)||!PC->GetCharacterAt(1)){return false;}
        auto* Session=World->GetGameInstance()->GetSubsystem<ULinkSession>();
        if(!Session){Test->AddError(TEXT("Session missing"));return true;}
        if(Phase==0)
        {
            if(!Test->TestTrue(TEXT("Real narrative initialized"),Session->IsReady())){return true;}
            if(!Test->TestEqual(TEXT("Fresh LT starts empty"),Session->HeldItem(0),0)||!Test->TestEqual(TEXT("Fresh RT owns gun"),Session->HeldItem(1),LinkFacts::Gun)){return true;}
            if(!Test->TestNotNull(TEXT("Native camera present"),PC->GetSharedCamera())||!Test->TestNotNull(TEXT("Native chain present"),PC->GetChain())){return true;}
            Phase=1;
        }
        if(Phase==3)
        {
            // The submitted LoadFromSlot must actually reload the final map/session.
            if(World==CompletedWorld.Get()){return false;}
            if(!PC->IsRunCompleted()||!PC->IsPaused()){return false;}
            Test->TestEqual(TEXT("Window fact survives complete-run reload"),Session->GetGlobals().FindRef(1389382),1);
            Test->TestEqual(TEXT("Floodlight fact survives complete-run reload"),Session->GetGlobals().FindRef(1389150),1);
            PC->RestartRun();CompletedWorld=World;Phase=4;return false;
        }
        if(Phase==4)
        {
            if(World==CompletedWorld.Get()||UGameplayStatics::GetCurrentLevelName(World)!=TEXT("EntryGate")){return false;}
            Test->TestFalse(TEXT("Restart is not completed"),PC->IsRunCompleted());
            Test->TestFalse(TEXT("Restart returns control"),PC->IsPaused());
            Test->TestEqual(TEXT("Restart clears window progress"),Session->GetGlobals().FindRef(1389382),0);
            Test->TestEqual(TEXT("Restart clears LT inventory"),Session->HeldItem(0),0);
            Test->TestEqual(TEXT("Restart restores RT gun"),Session->HeldItem(1),LinkFacts::Gun);return true;
        }
        if(Visit>=Visits.Num())
        {
            if(!Test->TestTrue(TEXT("Both-area route completes and pauses"),PC->IsRunCompleted()&&PC->IsPaused())){return true;}
            Test->TestTrue(TEXT("Both characters settle at completion"),PC->GetCharacterAt(0)->GetVelocity().IsNearlyZero()&&PC->GetCharacterAt(1)->GetVelocity().IsNearlyZero());
            Test->TestEqual(TEXT("Entry window fact persists through travel"),Session->GetGlobals().FindRef(1389382),1);
            Test->TestEqual(TEXT("Actual wedge-powered light is on"),Session->GetGlobals().FindRef(1389150),1);
            if(!Test->TestTrue(TEXT("Completed game writes explicit isolated save"),PC->SaveToSlot(Slot))){return true;}
            const auto* Save=Cast<ULinkSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));FLinkSaveSnapshot Snapshot;FString Error;
            if(!Test->TestTrue(TEXT("Disk payload decodes completed state"),Save&&LinkSave::Decode(Save->Payload,Session->GetDatabase(),Snapshot,Error)&&Snapshot.bCompleted)){return true;}
            CompletedWorld=World;
            if(!Test->TestTrue(TEXT("Completed save invokes native reload"),PC->LoadFromSlot(Slot))){return true;}
            Phase=3;return false;
        }
        const auto& Step=Visits[Visit];
        if(Opened&&Action>=Step.Actions.Num()&&!PC->IsWaitingForPartner())
        {
            PC->CloseInteractionView();Target=nullptr;Opened=false;++Visit;return false;
        }
        if(UGameplayStatics::GetCurrentLevelName(World)!=Step.Map){return false;}
        if(!Target.IsValid())
        {
            for(TActorIterator<ALinkInteractionActor> It(World);It;++It){if(It->PersistentId==Step.Map+TEXT(".")+Step.Interaction){Target=*It;break;}}
            if(!Target.IsValid()){Test->AddError(TEXT("Authored interaction missing: ")+Step.Map+TEXT(".")+Step.Interaction);return true;}
            PC->CloseInteractionView();FLinkCommandFrame Together;Together.Pressed=3;Together.Held=3;Together.bInsideViewport=true;
            Together.Target=ELinkTargetKind::Ground;Together.RealTime=World->GetRealTimeSeconds();PC->ApplyCommandFrame(Together,FVector::ZeroVector);
            Origin=PC->GetCharacterAt(Step.Player)->GetActorLocation();VisitStart=Now;
            if(!Test->TestTrue(TEXT("Interaction request uses actual controller"),PC->InteractWith(Target.Get(),Step.Player))){return true;}
            Action=0;Opened=false;return false;
        }
        if(Now-VisitStart>30){Test->AddError(TEXT("Physical approach or partner arrival stalled"));return true;}
        if(!Target->IsReady(Step.Player)||PC->IsWaitingForPartner()){return false;}
        if(!Opened)
        {
            const auto* Crew=PC->GetCharacterAt(Step.Player);
            Test->TestTrue(TEXT("Arrival is within the authored interaction area"),FVector::Dist2D(Crew->GetActorLocation(),Target->GetActorLocation())<=Target->ArrivalRadius+100);
            if(!Test->TestNotNull(TEXT("Native chain survives map travel"),PC->GetChain())){return true;}
            TArray<FVector> ChainPoints;PC->GetChain()->GetPoints(ChainPoints);
            if(!Test->TestTrue(TEXT("Actual native chain remains finite during gameplay"),ChainPoints.Num()>2&&!ChainPoints.ContainsByPredicate([](const FVector& P){return P.ContainsNaN();}))){return true;}
            if(!Test->TestTrue(TEXT("Arrived interaction opens"),PC->InteractWith(Target.Get(),Step.Player))){return true;}
            auto* Prior=PC->GetInteractionFor(Step.Player);
            Test->TestFalse(TEXT("Unreachable ground request rejected"),PC->NavigateCharacter(Step.Player,FVector(10000000,10000000,0)));
            Test->TestEqual(TEXT("Rejected ground request preserves interaction ownership"),PC->GetInteractionFor(Step.Player),Prior);
            Opened=true;return false;
        }
        if(Action<Step.Actions.Num())
        {
            const int32 Id=Step.Actions[Action];
            if(!Test->TestTrue(FString::Printf(TEXT("Authored action %d available in continuous run"),Id),PC->ChooseAction(Id))){return true;}
            ++Action;return false;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;double Start=-1,VisitStart=0;int32 Visit=0,Action=0,Phase=0;bool Opened=false;
    FString Slot;FVector Origin;TWeakObjectPtr<ALinkInteractionActor> Target;TWeakObjectPtr<UWorld> CompletedWorld;
    const TArray<FTripletVisit> Visits={
        {TEXT("EntryGate"),TEXT("Rock"),0,{1389448,1389800}},
        {TEXT("EntryGate"),TEXT("Kiosk"),0,{1389390,1389391,1389392}},
        {TEXT("EntryGate"),TEXT("Gate"),0,{1389385,1389386,1389398}},
        {TEXT("LoadingDocks"),TEXT("Kiosk"),0,{1388912,1388938,1388912,1389727}},
        {TEXT("LoadingDocks"),TEXT("Bay2"),0,{1389011,1389200}},
        {TEXT("LoadingDocks"),TEXT("Pole"),1,{1389002}},
        {TEXT("LoadingDocks"),TEXT("Kiosk"),1,{1389776}},
        {TEXT("LoadingDocks"),TEXT("Bay1"),0,{1389007,1389661}},
        {TEXT("LoadingDocks"),TEXT("Gate"),0,{1389663}},
        {TEXT("LoadingDocks"),TEXT("Bay2"),0,{1389665}}
    };
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTripletGameLoop,"Task0171.Headless.Integration.ContinuousTwoAreaSaveReload",EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FTripletGameLoop::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/EntryGate")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FTripletContinuousRun(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif

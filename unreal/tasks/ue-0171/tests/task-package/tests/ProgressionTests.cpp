// UE-0157: native two-area progression behavioral verifier.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/LinkAudio.h"
#include "Gameplay/LinkCharacter.h"
#include "Gameplay/LinkInteractionActor.h"
#include "Gameplay/LinkPlayerController.h"
#include "Gameplay/LinkSaveGame.h"
#include "Gameplay/LinkSession.h"
#include "Gameplay/LinkWorldEffect.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Rules/LinkSave.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

// Adapted from InteractionWorldTests.cpp
class FVerifyWorldInteraction : public IAutomationLatentCommand
{
public:
    explicit FVerifyWorldInteraction(FAutomationTestBase* InTest):Test(InTest){}
    bool Update() override
    {
        const double Now=FPlatformTime::Seconds(); if(Start<0){Start=Now;}
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* Controller=World?Cast<ALinkPlayerController>(World->GetFirstPlayerController()):nullptr;
        if(!Controller || !Controller->GetCharacterAt(0)) { if(Now-Start<15){return false;} Test->AddError(TEXT("Interaction fixture failed to start"));return true; }
        if(!Target.IsValid())
        {
            auto* Actor=World->SpawnActorDeferred<ALinkInteractionActor>(ALinkInteractionActor::StaticClass(),FTransform(FVector(500,0,0)));
            if(!Actor){Test->AddError(TEXT("INVALID_VERIFICATION: interaction actor spawn failed"));return true;}
            Actor->WaypointOffsets={FVector(-100,0,0),FVector(100,0,0)};
            Actor->PersistentId=TEXT("Fixture.Gate");Actor->EventId=1389383;Actor->InitialItem=1389410;
            Actor->FinishSpawning(FTransform(FVector(500,0,0)));Target=Actor;
            Test->TestTrue(TEXT("First click begins approach"),Controller->InteractWith(Actor,0));
            Test->TestFalse(TEXT("Distant participant is not physically ready"),Actor->IsReady(0));
            Test->TestNull(TEXT("Approach does not open interaction prematurely"),Controller->GetActiveInteraction());
        }
        auto* Actor=Target.Get();
        if(Phase==0 && Actor->IsReady(0))
        {
            Test->TestTrue(TEXT("Ready click opens the interaction"),Controller->InteractWith(Actor,0));
            Test->TestEqual(TEXT("Correct interaction is active"),Controller->GetActiveInteraction(),Actor);
            Controller->CloseInteractionView();
            Controller->InteractWith(Actor,1);Phase=1;
        }
        if(Phase==1 && Actor->IsReady(1) && Now-Start>5)
        {
            auto* Session=World->GetGameInstance()->GetSubsystem<ULinkSession>();
            FLinkStoryEngine Engine(Session->GetDatabase());auto& Context=Actor->GetContext();
            Test->TestEqual(TEXT("World facts identify LT initiator"),Engine.Get(LinkFacts::Initiator,Context),LinkFacts::LT);
            Test->TestEqual(TEXT("World facts identify RT listener"),Engine.Get(LinkFacts::Listener,Context),LinkFacts::RT);
            Test->TestEqual(TEXT("Both participants are present"),Actor->GetParticipants().ActiveMask(),uint8(3));
            Test->TestTrue(TEXT("Distinct approach waypoints are allocated"),Actor->GetParticipants().Waypoint(0)!=Actor->GetParticipants().Waypoint(1));
            Test->TestEqual(TEXT("Source initial item is initialized once"),Engine.Get(1389410,Context),1);
            Test->TestEqual(TEXT("Source new-game RT item is the gun"),Session->HeldItem(1),LinkFacts::Gun);
            Test->TestEqual(TEXT("Source player layer does not physically block the partner"),Controller->GetCharacterAt(0)->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
            Controller->NavigateCharacter(0,FVector(-300,-300,0));
            Test->TestFalse(TEXT("Movement leaves previous interaction"),Actor->GetParticipants().IsActive(0));
            FLinkCommandFrame Together;Together.Pressed=3;Together.Held=3;Together.bInsideViewport=true;
            Together.Target=ELinkTargetKind::Ground;Together.RealTime=World->GetRealTimeSeconds();
            Controller->ApplyCommandFrame(Together,FVector(-300,-300,0));
            Test->TestFalse(TEXT("Following leaves the other participant's previous interaction"),Actor->GetParticipants().IsActive(1));
            Test->TestNull(TEXT("Following clears stale interaction ownership"),Controller->GetInteractionFor(1));
            Test->TestTrue(TEXT("The released participant enters Follow"),Controller->GetCharacterAt(1)->GetMotionMode()==ELinkMotionMode::Follow);
            return true;
        }
        if(Now-Start>15){Test->AddError(TEXT("Interaction approach/arrival timed out"));return true;}
        return false;
    }
private:
    FAutomationTestBase* Test;double Start=-1;int32 Phase=0;TWeakObjectPtr<ALinkInteractionActor> Target;
};
// REQUIRED: behavior exercised through public gameplay APIs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkInteractionWorldTest,"Task0171.Headless.Progression.Interactions.ApproachAndRolesPIE",EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FLinkInteractionWorldTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/PortFixture")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyWorldInteraction(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

// Adapted from PartnerArrivalTests.cpp
class FVerifyPartnerArrival : public IAutomationLatentCommand
{
public:
    explicit FVerifyPartnerArrival(FAutomationTestBase* InTest):Test(InTest){}
    bool Update() override
    {
        const double Now=FPlatformTime::Seconds();if(Start<0){Start=Now;}
        if(Now-Start>18){Test->AddError(TEXT("Partner arrival did not complete"));return true;}
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* Controller=World?Cast<ALinkPlayerController>(World->GetFirstPlayerController()):nullptr;
        if(!Controller||!Controller->GetCharacterAt(1)){return false;}
        if(!Target.IsValid())
        {
            auto* Actor=World->SpawnActorDeferred<ALinkInteractionActor>(ALinkInteractionActor::StaticClass(),FTransform(FVector(400,-180,0)));
            if(!Actor){Test->AddError(TEXT("INVALID_VERIFICATION: partner actor spawn failed"));return true;}
            Actor->WaypointOffsets={FVector(-100,0,0),FVector(100,0,0)};
            Actor->PersistentId=TEXT("Fixture.CallOther");Actor->EventId=1389009;Actor->FinishSpawning(FTransform(FVector(400,-180,0)));Target=Actor;
            Controller->GetCharacterAt(1)->SetActorLocation(FVector(-500,200,103),false,nullptr,ETeleportType::TeleportPhysics);
            World->GetGameInstance()->GetSubsystem<ULinkSession>()->GetGlobals().Add(1389146,1);
            Controller->InteractWith(Actor,0);
        }
        if(Phase==0&&Target->IsReady(0))
        {
            Controller->InteractWith(Target.Get(),0);
            if(!Test->TestTrue(TEXT("Source ask-partner option is available"),Controller->ChooseAction(1389200))){return true;}
            Test->TestTrue(TEXT("Call-other waits for physical arrival"),Controller->IsWaitingForPartner());
            Test->TestEqual(TEXT("RT approaches the same interaction"),Controller->GetInteractionFor(1),Target.Get());
            Test->TestTrue(TEXT("RT becomes active before becoming ready"),Target->GetParticipants().IsActive(1));
            Test->TestFalse(TEXT("RT is not teleported into readiness"),Target->IsReady(1));Phase=1;
        }
        if(Phase==1&&!Controller->IsWaitingForPartner())
        {
            Test->TestTrue(TEXT("Action flow resumes only after RT arrives"),Target->IsReady(1));
            Test->TestTrue(TEXT("Action flow returns usable options"),!Controller->GetActionChoices().IsEmpty());
            Test->TestTrue(TEXT("Both characters remain allocated"),Target->GetParticipants().ActiveMask()==3);return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;double Start=-1;int32 Phase=0;TWeakObjectPtr<ALinkInteractionActor> Target;
};
// REQUIRED: behavior exercised through public gameplay APIs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkPartnerArrivalTest,"Task0171.Headless.Progression.Interactions.CallOtherArrivalPIE",EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FLinkPartnerArrivalTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/PortFixture")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyPartnerArrival(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

// Adapted from InventoryWorldTests.cpp
class FVerifyInventoryActions : public IAutomationLatentCommand
{
public:
    explicit FVerifyInventoryActions(FAutomationTestBase* InTest):Test(InTest){}
    bool Update() override
    {
        const double Now=FPlatformTime::Seconds();if(Start<0){Start=Now;}
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* Controller=World?Cast<ALinkPlayerController>(World->GetFirstPlayerController()):nullptr;
        if(!Controller || !Controller->GetCharacterAt(0))
        {
            if(Now-Start<15){return false;}Test->AddError(TEXT("Inventory world did not start"));return true;
        }
        if(!Target.IsValid())
        {
            for(TActorIterator<ALinkInteractionActor> It(World);It;++It)
            {
                if(It->PersistentId==TEXT("EntryGate.Rock")){Target=*It;break;}
            }
            if(!Target.IsValid()){Test->AddError(TEXT("The playable yard has no stone interaction"));return true;}
            Controller->InteractWith(Target.Get(),0);
        }
        auto* Actor=Target.Get();
        auto* Session=World->GetGameInstance()->GetSubsystem<ULinkSession>();
        FLinkStoryEngine Engine(Session->GetDatabase());auto& Context=Actor->GetContext();
        if(Phase==0 && Actor->IsReady(0))
        {
            Controller->InteractWith(Actor,0);
            Test->TestTrue(TEXT("Uninspected stone offers the source inspect action"),Controller->GetActionChoices().Contains(1389448));
            Test->TestFalse(TEXT("Source inspect prerequisite hides pickup initially"),Controller->GetActionChoices().Contains(1389800));
            if(!Controller->ChooseAction(1389448)){Test->AddError(TEXT("Inspect action failed"));return true;}
            Test->TestTrue(TEXT("Inspection unlocks the source pickup option"),Controller->GetActionChoices().Contains(1389800));
            Test->TestFalse(TEXT("Arbitrary hidden actions cannot be executed"),Controller->ChooseAction(1389456));
            Phase=1;PhaseStart=Now;
        }
        if(Phase==1 && Now-PhaseStart>1)
        {
            if(FParse::Param(FCommandLine::Get(),TEXT("FoundationsCapture")))
            {
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/Actions.png"),true,false);
            }
            Phase=2;PhaseStart=Now;return false;
        }
        if(Phase==2 && Now-PhaseStart>0.5)
        {
            Test->TestTrue(TEXT("Source pickup action succeeds"),Controller->ChooseAction(1389800));
            Test->TestEqual(TEXT("LT holds the stone"),Session->HeldItem(0),1389410);
            Test->TestEqual(TEXT("Pickup removes the local stone fact"),Engine.Get(1389410,Context),0);
            Test->TestEqual(TEXT("RT keeps the starting gun"),Session->HeldItem(1),LinkFacts::Gun);
            Test->TestFalse(TEXT("Duplicate pickup is rejected"),Session->ObservePickup(1389410,Context));
            Phase=3;PhaseStart=Now;return false;
        }
        if(Phase==3 && Now-PhaseStart>0.3)
        {
            bool FoundVisual=false;
            for(TActorIterator<AActor> It(World);It;++It)
            {
                if(It->ActorHasTag(TEXT("Item.EntryGate.Rock")))
                {
                    FoundVisual=true;Test->TestTrue(TEXT("Picked-up stone disappears from the world"),It->IsHidden());
                }
            }
            Test->TestTrue(TEXT("Stone has a presentation actor"),FoundVisual);
            Engine.Set(LinkFacts::CurrentSpeaker,LinkFacts::RT,Context);Engine.Set(1389410,1,Context);
            Test->TestFalse(TEXT("Occupied inventory cannot pick up another item"),Session->ObservePickup(1389410,Context));
            Test->TestTrue(TEXT("Using LT's item transfers it into the interaction"),Session->ObserveUse(LinkFacts::LTItem,Context));
            Test->TestEqual(TEXT("Use clears LT's hand"),Session->HeldItem(0),0);
            Test->TestEqual(TEXT("Used stone is available to local rules"),Engine.Get(1389410,Context),1);
            Test->TestFalse(TEXT("Empty-hand use is rejected"),Session->ObserveUse(LinkFacts::LTItem,Context));
            Controller->CloseInteractionView();Controller->TogglePauseOrClose();
            Test->TestTrue(TEXT("Escape pauses the world"),Controller->IsPaused());Controller->TogglePauseOrClose();
            Test->TestFalse(TEXT("Resume clears pause"),Controller->IsPaused());
            auto* Audio=World->GetSubsystem<ULinkAudio>();
            Test->TestNotNull(TEXT("Native audio is present in PIE"),Audio);
            if(Audio)
            {
                Test->TestTrue(TEXT("Runtime audio assets are complete"),Audio->HasRequiredAssets());
                Controller->ToggleAudio();Test->TestTrue(TEXT("Mute applies to the world sound layer"),Audio->IsMuted());
                Controller->ToggleAudio();Test->TestFalse(TEXT("Sound can be restored"),Audio->IsMuted());
            }
            return true;
        }
        if(Now-Start>18){Test->AddError(TEXT("Inventory action sequence timed out"));return true;}return false;
    }
private:
    FAutomationTestBase* Test;double Start=-1,PhaseStart=0;int32 Phase=0;TWeakObjectPtr<ALinkInteractionActor> Target;
};

// REQUIRED: behavior exercised through public gameplay APIs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkInventoryWorldTest,"Task0171.Headless.Progression.Inventory.SourceActionsPIE",EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FLinkInventoryWorldTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/EntryGate")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyInventoryActions(this));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

// Adapted from EntryGatePuzzleTests.cpp
class FVerifyEntryGatePuzzle : public IAutomationLatentCommand
{
public:
    explicit FVerifyEntryGatePuzzle(FAutomationTestBase* InTest):Test(InTest){}
    bool Update() override
    {
        const double Now=FPlatformTime::Seconds();if(Start<0){Start=Now;}
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* Controller=World?Cast<ALinkPlayerController>(World->GetFirstPlayerController()):nullptr;
        if(Now-Start>30){Test->AddError(FString::Printf(TEXT("EntryGate puzzle timed out in phase %d"),Phase));return true;}
        if(!Controller||!Controller->GetCharacterAt(0)){return false;}
        auto Find=[&](const FString& Id)->ALinkInteractionActor*
        {
            for(TActorIterator<ALinkInteractionActor> It(World);It;++It){if(It->PersistentId==Id){return *It;}}return nullptr;
        };
        auto Choose=[&](int32 Id)
        {
            const bool Result=Controller->ChooseAction(Id);
            Test->TestTrue(FString::Printf(TEXT("Source puzzle action %d is available"),Id),Result);return Result;
        };
        auto* Session=World->GetGameInstance()->GetSubsystem<ULinkSession>();
        if(Phase==4)
        {
            if(UGameplayStatics::GetCurrentLevelName(World)!=TEXT("LoadingDocks")){return false;}
            Test->TestEqual(TEXT("Source window state survives area travel"),Session->GetGlobals().FindRef(1389382),1);
            Test->TestEqual(TEXT("The first area's chair placement survives travel"),Session->GetInteractionFacts(TEXT("EntryGate.Gate"),0).FindRef(1389396),1);
            Test->TestEqual(TEXT("Inventory survives the area transition"),Session->HeldItem(1),LinkFacts::Gun);
            Test->TestNotNull(TEXT("The partner spawns in the new area"),Controller->GetCharacterAt(1));
            Test->TestFalse(TEXT("Entering the docks does not prematurely complete the run"),Controller->IsRunCompleted());return true;
        }
        if(!Target.IsValid())
        {
            Target=Find(TEXT("EntryGate.Rock"));if(!Target.IsValid()){Test->AddError(TEXT("Stone is missing"));return true;}
            Controller->InteractWith(Target.Get(),0);
        }
        if(Phase==0&&Target->IsReady(0))
        {
            Controller->InteractWith(Target.Get(),0);
            if(!Choose(1389448)||!Choose(1389800)){return true;}
            Controller->CloseInteractionView();Target=Find(TEXT("EntryGate.Kiosk"));
            if(!Target.IsValid()){Test->AddError(TEXT("Kiosk is missing"));return true;}
            Controller->InteractWith(Target.Get(),0);Phase=1;return false;
        }
        if(Phase==1&&Target->IsReady(0))
        {
            Controller->InteractWith(Target.Get(),0);
            if(!Choose(1389390)||!Choose(1389391)){return true;}
            Test->TestEqual(TEXT("Breaking the window consumes the carried rock"),Session->HeldItem(0),0);
            Test->TestEqual(TEXT("Window broken fact survives the dialogue chain"),Session->GetGlobals().FindRef(1389382),1);
            if(!Choose(1389392)){return true;}
            Test->TestEqual(TEXT("Chair can be retrieved through the broken window"),Session->HeldItem(0),1389396);
            Controller->CloseInteractionView();Target=Find(TEXT("EntryGate.Gate"));
            if(!Target.IsValid()){Test->AddError(TEXT("Gate is missing"));return true;}
            Controller->InteractWith(Target.Get(),0);Phase=2;return false;
        }
        if(Phase==2&&Target->IsReady(0))
        {
            Controller->InteractWith(Target.Get(),0);
            if(!Choose(1389385)||!Choose(1389386)){return true;}
            Test->TestEqual(TEXT("Placing the chair clears the held slot"),Session->HeldItem(0),0);
            Test->TestEqual(TEXT("The gate receives the chair"),Target->GetContext().Interaction->FindRef(1389396),1);
            Phase=3;PhaseStart=Now;return false;
        }
        if(Phase==3&&Now-PhaseStart>0.3)
        {
            int32 Variants=0;
            for(TActorIterator<AActor> It(World);It;++It)
            {
                if(It->ActorHasTag(TEXT("World.Kiosk.Glass"))){++Variants;Test->TestTrue(TEXT("Broken window pane is hidden"),It->IsHidden());}
                if(It->ActorHasTag(TEXT("World.Kiosk.Shards"))){++Variants;Test->TestFalse(TEXT("Broken glass is visible"),It->IsHidden());}
                if(It->ActorHasTag(TEXT("Item.EntryGate.Gate"))){++Variants;Test->TestFalse(TEXT("Placed gate chair is visible"),It->IsHidden());}
            }
            Test->TestEqual(TEXT("All expected world-state visuals are present"),Variants,5);
            if(!Choose(1389398)){return true;}
            Test->TestTrue(TEXT("Gate completion emits route feedback"),!Controller->GetFeedback().IsEmpty());
            Phase=4;return false;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;double Start=-1,PhaseStart=0;int32 Phase=0;TWeakObjectPtr<ALinkInteractionActor> Target;
};
// REQUIRED: behavior exercised through public gameplay APIs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkEntryGatePuzzleTest,"Task0171.Headless.Progression.Puzzles.EntryGatePIE",EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FLinkEntryGatePuzzleTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/EntryGate")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyEntryGatePuzzle(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

// Adapted from DocksPuzzleTests.cpp
struct FLinkPuzzleVisit{FString Name;int32 Player;TArray<int32> Actions;};
class FVerifyDocksPuzzle : public IAutomationLatentCommand
{
public:
    explicit FVerifyDocksPuzzle(FAutomationTestBase* InTest):Test(InTest){}
    bool Update() override
    {
        const double Now=FPlatformTime::Seconds();if(Start<0){Start=Now;}
        if(Now-Start>90){Test->AddError(FString::Printf(TEXT("Docks puzzle timed out at visit %d action %d"),Visit,Action));return true;}
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* Controller=World?Cast<ALinkPlayerController>(World->GetFirstPlayerController()):nullptr;
        if(!Controller||!Controller->GetCharacterAt(1)){return false;}
        auto* Session=World->GetGameInstance()->GetSubsystem<ULinkSession>();
        if(Target.IsValid()&&Now-VisitStart>15)
        {
            Test->AddError(FString::Printf(TEXT("Visit %d stalled: LT=%s RT=%s target=%s RT mode=%d active=%d ready=%d destination=%s"),Visit,
                *Controller->GetCharacterAt(0)->GetActorLocation().ToString(),*Controller->GetCharacterAt(1)->GetActorLocation().ToString(),*Target->GetActorLocation().ToString(),
                int32(Controller->GetCharacterAt(1)->GetMotionMode()),Target->GetParticipants().IsActive(1),Target->IsReady(1),*Target->WaypointFor(1).ToString()));
            if(FParse::Param(FCommandLine::Get(),TEXT("FoundationsCapture"))){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/DocksPuzzle.png"),true,false);}
            return true;
        }
        if(Visit>=Visits.Num())
        {
            FLinkStoryEngine Engine(Session->GetDatabase());FLinkStoryContext Context;
            Context.Global=&Session->GetGlobals();Context.Interaction=Session->FindInteractionFacts(TEXT("LoadingDocks.Bay2"));
            for(int32 Fact:{1388906,1388908,1389149,1389766,1389785,1389150,1389735})
            {
                const auto* Entry=Session->GetDatabase().Find(Fact);
                Test->TestEqual(Entry->Key+TEXT(" is set in its declared scope by actual source actions"),Engine.Get(Fact,Context),1);
            }
            Test->TestEqual(TEXT("The wedge was used, leaving LT's hand empty"),Session->HeldItem(0),0);
            Test->TestEqual(TEXT("The gate owns the inserted wedge"),Session->GetInteractionFacts(TEXT("LoadingDocks.Gate"),0).FindRef(1389508),1);
            Test->TestTrue(TEXT("Opening the lit bay invokes the source exit event"),!Controller->GetFeedback().IsEmpty());
            Test->TestTrue(TEXT("The exit displays a completed run"),Controller->IsRunCompleted());
            Test->TestTrue(TEXT("The completion panel pauses gameplay"),Controller->IsPaused());
            Test->TestTrue(TEXT("Characters settle before the completed run is paused"),Controller->GetCharacterAt(0)->GetVelocity().IsNearlyZero()&&Controller->GetCharacterAt(1)->GetVelocity().IsNearlyZero());
            if(FParse::Param(FCommandLine::Get(),TEXT("FoundationsCapture"))){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/Completion.png"),true,false);}
            return true;
        }
        const auto& Step=Visits[Visit];
        if(!Target.IsValid())
        {
            for(TActorIterator<ALinkInteractionActor> It(World);It;++It){if(It->PersistentId==TEXT("LoadingDocks.")+Step.Name){Target=*It;break;}}
            if(!Target.IsValid()){Test->AddError(TEXT("Puzzle interaction is missing: ")+Step.Name);return true;}
            Controller->CloseInteractionView();
            // The actual paired ground command releases both previous interaction anchors.
            FLinkCommandFrame Together;Together.Pressed=3;Together.Held=3;Together.bInsideViewport=true;
            Together.Target=ELinkTargetKind::Ground;Together.RealTime=World->GetRealTimeSeconds();
            Controller->ApplyCommandFrame(Together,FVector(0,0,0));
            Controller->InteractWith(Target.Get(),Step.Player);bOpened=false;Action=0;VisitStart=Now;
        }
        if(bOpened&&Action>=Step.Actions.Num()&&!Controller->IsWaitingForPartner())
        {
            Controller->CloseInteractionView();Target=nullptr;++Visit;return false;
        }
        if(!Target->IsReady(Step.Player)||Controller->IsWaitingForPartner()){return false;}
        if(!bOpened){Controller->InteractWith(Target.Get(),Step.Player);bOpened=true;return false;}
        if(Action<Step.Actions.Num())
        {
            const int32 Id=Step.Actions[Action];
            if(!Controller->ChooseAction(Id))
            {
                FString Available;for(int32 Option:Controller->GetActionChoices()){Available+=FString::FromInt(Option)+TEXT(" ");}
                Test->AddError(FString::Printf(TEXT("%s source action %d unavailable; choices: %s; feedback: %s"),*Step.Name,Id,*Available,*Controller->GetFeedback()));return true;
            }
            Test->AddInfo(FString::Printf(TEXT("%s: completed source action %d"),*Step.Name,Id));++Action;return false;
        }
        Controller->CloseInteractionView();Target=nullptr;++Visit;return false;
    }
private:
    FAutomationTestBase* Test;double Start=-1,VisitStart=0;int32 Visit=0,Action=0;bool bOpened=false;TWeakObjectPtr<ALinkInteractionActor> Target;
    const TArray<FLinkPuzzleVisit> Visits={
        {TEXT("Kiosk"),0,{1388912,1388938,1388912,1389727}},
        {TEXT("Bay2"),0,{1389011,1389200}},
        {TEXT("Pole"),1,{1389002}},
        {TEXT("Kiosk"),1,{1389776}},
        {TEXT("Bay1"),0,{1389007,1389661}},
        {TEXT("Gate"),0,{1389663}},
        // Reopening the now-lit bay automatically identifies it through the source entry chain.
        {TEXT("Bay2"),0,{1389665}}
    };
};
// REQUIRED: behavior exercised through public gameplay APIs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkDocksPuzzleTest,"Task0171.Headless.Progression.Puzzles.LoadingDocksPIE",EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FLinkDocksPuzzleTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/LoadingDocks")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyDocksPuzzle(this));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

// Adapted from DocksWorldTests.cpp
class FVerifyDocksWorld : public IAutomationLatentCommand
{
public:
    explicit FVerifyDocksWorld(FAutomationTestBase* InTest):Test(InTest){}
    bool Update() override
    {
        const double Now=FPlatformTime::Seconds();if(Start<0){Start=Now;}
        if(Now-Start>20){Test->AddError(TEXT("Loading docks world fixture timed out"));return true;}
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* Controller=World?Cast<ALinkPlayerController>(World->GetFirstPlayerController()):nullptr;
        if(!Controller||!Controller->GetCharacterAt(0)||Now-Start<1){return false;}
        auto* Session=World->GetGameInstance()->GetSubsystem<ULinkSession>();
        if(Phase==0)
        {
            int32 Interactions=0;
            for(TActorIterator<ALinkInteractionActor> It(World);It;++It)
            {
                ++Interactions;
                for(const auto& Offset:It->WaypointOffsets)
                {
                    auto* Route=UNavigationSystemV1::FindPathToLocationSynchronously(World,Controller->GetCharacterAt(0)->GetNavAgentLocation(),It->GetActorTransform().TransformPosition(Offset),Controller->GetCharacterAt(0));
                    Test->TestTrue(It->PersistentId+TEXT(" has a complete approach route"),Route&&Route->IsValid()&&!Route->IsPartial());
                }
            }
            Test->TestEqual(TEXT("All eight source interactions are present"),Interactions,8);
            Test->TestEqual(TEXT("Bay 1 initially contains the source wedge"),Session->GetInteractionFacts(TEXT("LoadingDocks.Bay1"),0).FindRef(1389508),1);
            Test->TestEqual(TEXT("Gate initially contains the source pickaxe"),Session->GetInteractionFacts(TEXT("LoadingDocks.Gate"),0).FindRef(1389509),1);
            Test->TestEqual(TEXT("Employee door initially contains its knob"),Session->GetInteractionFacts(TEXT("LoadingDocks.EmployeeDoor"),0).FindRef(1389510),1);
            Test->TestEqual(TEXT("Floodlight fact starts off"),Session->GetGlobals().FindRef(1389150),0);
            Session->GetGlobals().Add(1388906,1);Session->GetGlobals().Add(1389671,1);
            Session->GetInteractionFacts(TEXT("LoadingDocks.Gate"),0).Add(1389508,1);
            Controller->NavigateCharacter(0,FVector(900,0,0));Phase=1;PhaseStart=Now;return false;
        }
        if(Phase==1&&Now-PhaseStart>3)
        {
            int32 Doors=0,Lights=0,Fragments=0;
            for(TActorIterator<AActor> It(World);It;++It)
            {
                if(It->ActorHasTag(TEXT("World.Docks.KioskDoor")))
                {
                    ++Doors;Test->TestEqual(TEXT("Kiosk door opens around its hinge"),It->GetActorRotation().Yaw,-95.0,0.5);
                }
                if(It->ActorHasTag(TEXT("World.Docks.Floodlights")))
                {
                    ++Lights;auto* Light=It->FindComponentByClass<ULightComponent>();
                    Test->TestTrue(TEXT("Wedge-powered light reaches its authored intensity"),Light&&Light->Intensity>5700);
                }
                if(It->ActorHasTag(TEXT("World.Docks.TrailerBroken"))){++Fragments;Test->TestFalse(TEXT("Trailer damage reveals broken pieces"),It->IsHidden());}
            }
            Test->TestEqual(TEXT("One animated kiosk door is present"),Doors,1);Test->TestEqual(TEXT("Both puzzle floodlights are present"),Lights,2);Test->TestEqual(TEXT("Three trailer fragments are present"),Fragments,3);
            Test->TestEqual(TEXT("Local wedge powers the source global lighting fact"),Session->GetGlobals().FindRef(1389150),1);
            if(FParse::Param(FCommandLine::Get(),TEXT("FoundationsCapture"))){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/Docks.png"),true,false);}
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;double Start=-1,PhaseStart=0;int32 Phase=0;
};
// REQUIRED: behavior exercised through public gameplay APIs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkDocksWorldTest,"Task0171.Headless.Progression.Docks.WorldEffectsPIE",EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FLinkDocksWorldTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/LoadingDocks")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyDocksWorld(this));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

// Adapted from SaveTests.cpp
// REQUIRED: behavior exercised through public gameplay APIs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkSaveCodecTest,"Task0171.Headless.Progression.Save.VersionedPayload",EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FLinkSaveCodecTest::RunTest(const FString& Parameters)
{
    FLinkStoryDatabase Database;FString Error;
    if(!Database.LoadFile(FPaths::ProjectContentDir()/TEXT("Data/Narrative.json"),Error)){AddError(Error);return false;}
    FLinkSaveSnapshot Source;Source.Map=TEXT("EntryGate");Source.Positions[0]=FVector(10,-20,100);Source.Positions[1]=FVector(180,260,100);Source.Yaws[0]=-45;
    Source.Globals.Add(LinkFacts::RTItem,LinkFacts::Gun);Source.Globals.Add(LinkFacts::LTItem,1389410);
    Source.Interactions.FindOrAdd(TEXT("EntryGate.Rock")).Add(1389410,0);
    Source.bCompleted=true;
    const FString Json=LinkSave::Encode(Source);FLinkSaveSnapshot Restored;
    if(!TestTrue(TEXT("A native snapshot can be decoded"),LinkSave::Decode(Json,Database,Restored,Error))){return false;}
    TestEqual(TEXT("Save preserves item ownership"),Restored.Globals.FindRef(LinkFacts::LTItem),1389410);
    TestTrue(TEXT("Save preserves completion state"),Restored.bCompleted);
    TestTrue(TEXT("Save preserves exact positions"),Restored.Positions[1].Equals(Source.Positions[1],0.001));
    const auto* RestoredRock=Restored.Interactions.Find(TEXT("EntryGate.Rock"));
    if(!TestNotNull(TEXT("Local interaction state survives the round trip"),RestoredRock)){return false;}
    TestEqual(TEXT("Empty local item maps do not reset to initial state"),RestoredRock->FindRef(1389410),0);
    TestEqual(TEXT("Encoding is stable after round-trip"),LinkSave::Encode(Restored),Json);
    // Every rejection starts with independent live state, deliberately different from
    // the incoming save. A previous failed decode must never establish the expected
    // state for another probe, or partial writes can silently become the new baseline.
    auto RejectPayload=[&](const TCHAR* Label,const FString& Payload)
    {
        FLinkSaveSnapshot Destination;Destination.Map=TEXT("LoadingDocks");Destination.bCompleted=false;
        Destination.Positions[0]=FVector(-123,456,111);Destination.Positions[1]=FVector(789,-321,222);
        Destination.Yaws[0]=67;Destination.Yaws[1]=-89;
        Destination.Globals.Add(LinkFacts::LTItem,0);Destination.Globals.Add(LinkFacts::RTItem,LinkFacts::Gun);
        Destination.Interactions.FindOrAdd(TEXT("EntryGate.Gate")).Add(1389396,1);
        const FString Before=LinkSave::Encode(Destination);
        TestFalse(Label,LinkSave::Decode(Payload,Database,Destination,Error));
        TestEqual(TEXT("Rejected payload preserves the complete independent destination"),LinkSave::Encode(Destination),Before);
    };
    RejectPayload(TEXT("Malformed payload fails"),TEXT("{broken"));
    auto Invalid=Source;Invalid.Map=TEXT("/UnknownMap");RejectPayload(TEXT("Unknown maps are rejected"),LinkSave::Encode(Invalid));
    Invalid=Source;Invalid.Globals.Add(LinkFacts::LTItem,1389383);RejectPayload(TEXT("An event cannot be restored as an inventory item"),LinkSave::Encode(Invalid));
    Invalid=Source;Invalid.Globals.Add(1389410,1);RejectPayload(TEXT("Facts in the wrong scope are rejected"),LinkSave::Encode(Invalid));
    Invalid=Source;Invalid.Positions[0].X=1e12;RejectPayload(TEXT("Invalid position bounds are rejected"),LinkSave::Encode(Invalid));
    // Mutate parsed fields, not serializer whitespace; reuse the same atomicity assertion.
    auto RejectMutation=[&](const TCHAR* Label,TFunction<void(TSharedPtr<FJsonObject>)> Mutate)
    {
        TSharedPtr<FJsonObject> Root;
        if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root)
        {AddError(TEXT("The emitted snapshot is not JSON"));return;}
        Mutate(Root);FString Broken;
        FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Broken));
        RejectPayload(Label,Broken);
    };
    RejectMutation(TEXT("Unknown version"),[](auto Root){Root->SetNumberField(TEXT("version"),99);});
    RejectMutation(TEXT("Incompatible source"),[](auto Root){Root->SetStringField(TEXT("source"),TEXT("other"));});
    RejectMutation(TEXT("Wrong player count"),[](auto Root){Root->SetArrayField(TEXT("players"),{});});
    RejectMutation(TEXT("Unknown global fact"),[](auto Root){Root->GetObjectField(TEXT("globals"))->SetNumberField(TEXT("2147483646"),1);});
    RejectMutation(TEXT("Nonintegral fact"),[](auto Root){Root->GetObjectField(TEXT("globals"))->SetNumberField(FString::FromInt(LinkFacts::LTItem),0.25);});
    RejectMutation(TEXT("Invalid yaw bound"),[](auto Root){Root->GetArrayField(TEXT("players"))[0]->AsObject()->SetNumberField(TEXT("yaw"),36001);});
    RejectPayload(TEXT("Oversized payload"),FString::ChrN(1024*1024+1,TEXT(' ')));
    return true;
}

// Adapted from SaveWorldTests.cpp
class FVerifySaveReload : public IAutomationLatentCommand
{
public:
    explicit FVerifySaveReload(FAutomationTestBase* InTest):Test(InTest),Slot(TEXT("Foundations-Automation-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)){}
    bool Update() override
    {
        const double Now=FPlatformTime::Seconds();if(Start<0){Start=Now;}
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* Controller=World?Cast<ALinkPlayerController>(World->GetFirstPlayerController()):nullptr;
        if(Now-Start>25){Test->AddError(TEXT("Save/reload fixture timed out"));Cleanup();return true;}
        if(!Controller||!Controller->GetCharacterAt(0)){return false;}
        auto* Session=World->GetGameInstance()->GetSubsystem<ULinkSession>();
        if(Phase==0)
        {
            if(Now-Start<1){return false;}
            // Preserve the actual settled capsule height; Z=100 embeds the capsule in the floor skin.
            ExpectedPositions[0]=FVector(-300,-200,Controller->GetCharacterAt(0)->GetActorLocation().Z);
            ExpectedPositions[1]=FVector(100,240,Controller->GetCharacterAt(1)->GetActorLocation().Z);
            Controller->GetCharacterAt(0)->SetActorLocation(ExpectedPositions[0],false,nullptr,ETeleportType::TeleportPhysics);
            Controller->GetCharacterAt(1)->SetActorLocation(ExpectedPositions[1],false,nullptr,ETeleportType::TeleportPhysics);
            Controller->GetCharacterAt(0)->SetActorRotation(FRotator(0,-35,0));
            Session->GetGlobals().Add(LinkFacts::LTItem,1389410);Session->GetInteractionFacts(TEXT("EntryGate.Rock"),1389410).Add(1389410,0);
            Session->SetRunCompleted(true);
            Test->TestTrue(TEXT("Native platform slot saves"),Controller->SaveToSlot(Slot));
            Session->GetGlobals().Add(LinkFacts::LTItem,0);Session->GetInteractionFacts(TEXT("EntryGate.Rock"),1389410).Add(1389410,1);
            OldController=Controller;
            if(!Controller->LoadFromSlot(Slot)){Test->AddError(TEXT("Saved slot could not be loaded"));Cleanup();return true;}
            Phase=1;return false;
        }
        if(Phase==1 && Controller!=OldController.Get())
        {
            Test->AddInfo(FString::Printf(TEXT("Restored LT=%s RT=%s"),*Controller->GetCharacterAt(0)->GetActorLocation().ToString(),*Controller->GetCharacterAt(1)->GetActorLocation().ToString()));
            Test->TestEqual(TEXT("Level reload restores held item"),Session->HeldItem(0),1389410);
            Test->TestTrue(TEXT("Completion status survives a real reload"),Controller->IsRunCompleted());
            Test->TestEqual(TEXT("RT still owns the gun"),Session->HeldItem(1),LinkFacts::Gun);
            Test->TestTrue(TEXT("LT restores within 2cm"),Controller->GetCharacterAt(0)->GetActorLocation().Equals(ExpectedPositions[0],2));
            Test->TestTrue(TEXT("RT restores within 2cm"),Controller->GetCharacterAt(1)->GetActorLocation().Equals(ExpectedPositions[1],2));
            Test->TestEqual(TEXT("Facing survives reload"),Controller->GetCharacterAt(0)->GetActorRotation().Yaw,-35.0,0.5);
            bool Found=false;
            for(TActorIterator<ALinkInteractionActor> It(World);It;++It)
            {
                if(It->PersistentId==TEXT("EntryGate.Rock"))
                {
                    Found=true;Test->TestEqual(TEXT("Interaction points at restored local state"),It->GetContext().Interaction->FindRef(1389410),0);
                }
            }
            Test->TestTrue(TEXT("Restored map has its interactable"),Found);
            auto* Invalid=Cast<ULinkSaveGame>(UGameplayStatics::CreateSaveGameObject(ULinkSaveGame::StaticClass()));Invalid->Payload=TEXT("{invalid");
            UGameplayStatics::SaveGameToSlot(Invalid,Slot,0);
            Test->TestFalse(TEXT("Corrupt native slot is rejected"),Controller->LoadFromSlot(Slot));
            Test->TestEqual(TEXT("Rejected load keeps current inventory"),Session->HeldItem(0),1389410);
            OldController=Controller;Controller->RestartRun();Phase=2;return false;
        }
        if(Phase==2&&Controller!=OldController.Get())
        {
            Test->TestFalse(TEXT("Restart clears completion"),Controller->IsRunCompleted());
            Test->TestFalse(TEXT("Restart resumes gameplay"),Controller->IsPaused());
            Test->TestEqual(TEXT("Restart restores LT's empty hand"),Session->HeldItem(0),0);
            Test->TestEqual(TEXT("Restart restores RT's starting gun"),Session->HeldItem(1),LinkFacts::Gun);
            Test->TestEqual(TEXT("Restart restores the world stone"),Session->GetInteractionFacts(TEXT("EntryGate.Rock"),0).FindRef(1389410),1);
            Cleanup();return true;
        }
        return false;
    }
private:
    void Cleanup(){UGameplayStatics::DeleteGameInSlot(Slot,0);}
    FAutomationTestBase* Test;FString Slot;double Start=-1;int32 Phase=0;TWeakObjectPtr<ALinkPlayerController> OldController;
    FVector ExpectedPositions[2];
};
// REQUIRED: behavior exercised through public gameplay APIs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkSaveWorldTest,"Task0171.Headless.Progression.Save.ReloadWorldPIE",EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FLinkSaveWorldTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/EntryGate")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifySaveReload(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

// REQUIRED: restores session facts independently of actors, and consumes spawn state once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE0157SessionRestoreTest,"Task0171.Headless.Progression.Save.SessionRestoreIsolation",EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FUE0157SessionRestoreTest::RunTest(const FString& Parameters)
{
    // An uninitialized transient subsystem is intentional: these operations do not
    // depend on database loading. Never replace fact storage beneath live map actors.
    auto* Instance=NewObject<UGameInstance>();
    auto* Session=NewObject<ULinkSession>(Instance);
    Session->GetGlobals().Add(LinkFacts::LTItem,1389410);
    Session->GetInteractionFacts(TEXT("EntryGate.Rock"),1389410).Add(1389410,0);
    TestEqual(TEXT("Repeated lookup does not recreate a consumed item"),Session->GetInteractionFacts(TEXT("EntryGate.Rock"),1389410).FindRef(1389410),0);
    FLinkSaveSnapshot Staged;Staged.Map=TEXT("LoadingDocks");Staged.bCompleted=true;
    Staged.Globals.Add(LinkFacts::RTItem,LinkFacts::Gun);
    Staged.Interactions.FindOrAdd(TEXT("EntryGate.Rock")).Add(1389410,0);
    Staged.Positions[0]=FVector(30,40,110);Staged.Yaws[1]=32;
    Session->StageRestore(Staged);
    TestEqual(TEXT("Staging alone does not replace live inventory"),Session->HeldItem(0),1389410);
    Session->ApplyPendingFacts();
    TestEqual(TEXT("Restore replaces globals rather than merging stale inventory"),Session->HeldItem(0),0);
    TestEqual(TEXT("Restore retains RT inventory"),Session->HeldItem(1),LinkFacts::Gun);
    TestTrue(TEXT("Restore carries completion"),Session->IsRunCompleted());
    TestEqual(TEXT("Restored empty local state stays empty"),Session->GetInteractionFacts(TEXT("EntryGate.Rock"),1389410).FindRef(1389410),0);
    FLinkSaveSnapshot Output;Output.Map=TEXT("sentinel");
    TestFalse(TEXT("Wrong map cannot consume spawn state"),Session->ConsumeSpawnState(TEXT("EntryGate"),Output));
    TestEqual(TEXT("Wrong-map lookup leaves output untouched"),Output.Map,FString(TEXT("sentinel")));
    TestTrue(TEXT("Matching map consumes spawn state"),Session->ConsumeSpawnState(TEXT("LoadingDocks"),Output));
    TestTrue(TEXT("Spawn position is preserved"),Output.Positions[0].Equals(Staged.Positions[0],0.001));
    TestEqual(TEXT("Spawn heading is preserved"),Output.Yaws[1],32.f);
    TestFalse(TEXT("Spawn state is consumed only once"),Session->ConsumeSpawnState(TEXT("LoadingDocks"),Output));
    FLinkSaveSnapshot Captured;Captured.Interactions.FindOrAdd(TEXT("obsolete"));
    Session->CaptureFacts(Captured);
    TestFalse(TEXT("Capture removes obsolete output interaction state"),Captured.Interactions.Contains(TEXT("obsolete")));
    TestTrue(TEXT("Capture includes completion"),Captured.bCompleted);
    TestTrue(TEXT("Capture includes current interaction"),Captured.Interactions.Contains(TEXT("EntryGate.Rock")));
    return true;
}

namespace UE0157
{
class FWorldEffectCycles : public IAutomationLatentCommand
{
public:
    explicit FWorldEffectCycles(FAutomationTestBase* InTest):Test(InTest){}
    bool Update() override
    {
        if(Start<0){Start=FPlatformTime::Seconds();}
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* Controller=World?Cast<ALinkPlayerController>(World->GetFirstPlayerController()):nullptr;
        if(!Controller||!Controller->GetCharacterAt(1))
        {
            if(FPlatformTime::Seconds()-Start<20){return false;}
            Test->AddError(TEXT("World-effect fixture could not start"));return true;
        }
        auto* Session=World->GetGameInstance()->GetSubsystem<ULinkSession>();
        auto* Visible=World->SpawnActor<AActor>();
        auto* Hidden=World->SpawnActor<AActor>();
        if(!Visible||!Hidden){Test->AddError(TEXT("Effect fixtures could not spawn"));return true;}
        auto* Scene=NewObject<USceneComponent>(Visible);Visible->SetRootComponent(Scene);Scene->RegisterComponent();
        Visible->Tags.Add(TEXT("UE0157.Active"));Hidden->Tags.Add(TEXT("UE0157.Inactive"));
        const FTransform Initial(FRotator(0,15,0),FVector(200,300,500));Visible->SetActorTransform(Initial);
        Session->GetGlobals().Add(1388906,0);
        auto* Effect=World->SpawnActorDeferred<ALinkWorldEffect>(ALinkWorldEffect::StaticClass(),FTransform::Identity);
        if(!Effect){Test->AddError(TEXT("Effect could not spawn"));Visible->Destroy();Hidden->Destroy();return true;}
        Effect->InputFact=1388906;Effect->TrueTag=TEXT("UE0157.Active");Effect->FalseTag=TEXT("UE0157.Inactive");
        Effect->MovingTag=TEXT("UE0157.Active");Effect->ActiveOffset=FVector(0,0,120);Effect->ActiveYaw=70;
        Effect->FinishSpawning(FTransform::Identity);Effect->SetActorTickEnabled(false);
        for(int32 Cycle=0;Cycle<4;++Cycle)
        {
            Session->GetGlobals().Add(1388906,1);Effect->Tick(2.f);
            Test->TestTrue(TEXT("Fact activates effect"),Effect->IsActivated());
            Test->TestFalse(TEXT("True variant becomes visible"),Visible->IsHidden());
            Test->TestTrue(TEXT("True variant enables collision"),Visible->GetActorEnableCollision());
            Test->TestTrue(TEXT("False variant becomes hidden"),Hidden->IsHidden());
            Test->TestFalse(TEXT("False variant disables collision"),Hidden->GetActorEnableCollision());
            Test->TestTrue(TEXT("Offset is relative to original transform"),Visible->GetActorLocation().Equals(Initial.GetLocation()+FVector(0,0,120),0.01));
            Test->TestEqual(TEXT("Hinge rotates from original yaw"),Visible->GetActorRotation().Yaw,85.0,0.01);
            Effect->Tick(2.f);
            Test->TestTrue(TEXT("Repeated active updates do not add translation"),Visible->GetActorLocation().Equals(Initial.GetLocation()+FVector(0,0,120),0.01));
            Session->GetGlobals().Add(1388906,0);Effect->Tick(2.f);
            Test->TestFalse(TEXT("Cleared fact deactivates effect"),Effect->IsActivated());
            Test->TestTrue(TEXT("Deactivation restores original transform without drift"),Visible->GetActorTransform().Equals(Initial,0.01));
            Test->TestTrue(TEXT("Inactive true variant is hidden"),Visible->IsHidden());
            Test->TestFalse(TEXT("Inactive false variant becomes visible"),Hidden->IsHidden());
        }
        Visible->Destroy();Effect->Tick(1.f); // dead weak targets must be harmless
        Hidden->Destroy();Effect->Destroy();return true;
    }
private:
    FAutomationTestBase* Test;double Start=-1;
};
}
// REQUIRED: repeated fact/visual/transform transitions and destroyed target cleanup.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE0157WorldEffectCycleTest,"Task0171.Headless.Progression.Docks.RepeatedEffectsPIE",EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FUE0157WorldEffectCycleTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/PortFixture")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(UE0157::FWorldEffectCycles(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif

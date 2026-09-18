#include "Gameplay/LinkPlayerController.h"
#include "Gameplay/LinkInteractionActor.h"
#include "Gameplay/LinkSession.h"
#include "Gameplay/LinkAudio.h"
#include "UI/LinkHUD.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"

namespace
{
FString PlainText(const FString& Text)
{
    FString Result;bool InTag=false;
    for(TCHAR C:Text){if(C==TEXT('<')){InTag=true;}else if(C==TEXT('>')){InTag=false;}else if(!InTag){Result.AppendChar(C);}}
    return Result;
}
}
void ALinkPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if(Interface && GetWorld()->GetGameViewport()){GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Interface.ToSharedRef());}
    Interface.Reset();Super::EndPlay(Reason);
}
void ALinkPlayerController::CloseInteractionView()
{
    const bool bWasActive=ActiveInteraction!=nullptr;
    ActiveInteraction=nullptr;ActionChoices.Reset();QueuedEntry=0;AwaitedParticipants=0;++ViewRevision;
    if(bWasActive){CameraFocus.Settle();AutoSave();}
}
void ALinkPlayerController::TogglePauseOrClose()
{
    if(bTransitioning){return;}
    if(ActiveInteraction){CloseInteractionView();return;}
    SetPause(!IsPaused());Commands.Reset();PerformedButtons=0;++ViewRevision;
}
FString ALinkPlayerController::InventoryLabel(int32 PlayerIndex) const
{
    const auto* Session=GetGameInstance()->GetSubsystem<ULinkSession>();return Session->ItemLabel(Session->HeldItem(PlayerIndex));
}
void ALinkPlayerController::ToggleAudio(){if(auto* Audio=GetWorld()->GetSubsystem<ULinkAudio>()){Audio->ToggleMute();}}
FString ALinkPlayerController::AudioLabel() const
{
    const auto* Audio=GetWorld()->GetSubsystem<ULinkAudio>();return Audio&&Audio->IsMuted()?TEXT("M  ·  SOUND OFF"):TEXT("M  ·  SOUND ON");
}
FString ALinkPlayerController::HoverLabel() const
{
    if(ActiveInteraction||IsPaused()){return {};}
    if(!HoveredInteraction){return Feedback;}
    return HoveredInteraction->DisplayName+TEXT("  ·  Click to approach / interact");
}
FString ALinkPlayerController::ActionLabel(int32 EntryId) const
{
    const auto* Session=GetGameInstance()->GetSubsystem<ULinkSession>();const auto* Entry=Session->GetDatabase().Find(EntryId);
    if(!Entry){return {};}
    const FString Prefix=Entry->Speaker==LinkFacts::RT?TEXT("RT  ·  "):TEXT("LT  ·  ");
    return Prefix+PlainText(Entry->Text);
}
void ALinkPlayerController::OpenInteraction(ALinkInteractionActor* Target)
{
    ReviewCameraId=NAME_None;
    Feedback.Reset();
    ActiveInteraction=Target;ActionChoices.Reset();QueuedEntry=0;AwaitedParticipants=0;bRouteRequested=false;
    auto* Session=GetGameInstance()->GetSubsystem<ULinkSession>();
    InteractionEngine=MakeUnique<FLinkStoryEngine>(Session->GetDatabase());
    InteractionEngine->OnInvoked=[this,Session](int32 Id,FLinkStoryContext& Context)
    {
        if(Session->ObservePickup(Id,Context))
        {
            Feedback=TEXT("Picked up ")+Session->ItemLabel(Id);
            if(auto* Audio=GetWorld()->GetSubsystem<ULinkAudio>()){Audio->Pickup();}
        }
        QueuedEntry=Id;
    };
    InteractionEngine->OnSpecificInvoked=[this,Session](int32 Id,FLinkStoryContext& Context)
    {
        if(Session->ObserveUse(Id,Context)){Feedback=TEXT("Item used.");}
        if(Id==LinkFacts::CallOther && ActiveInteraction)
        {
            for(int32 Index=0;Index<2;++Index)
            {
                if(InteractionEngine->Get(Index==0?LinkFacts::IsLTPresent:LinkFacts::IsRTPresent,Context)==0&&InteractWith(ActiveInteraction,Index))
                {
                    AwaitedParticipants|=uint8(1<<Index);Feedback=Index==0?TEXT("Waiting for LT to arrive..."):TEXT("Waiting for RT to arrive...");
                }
            }
        }
        if(Id==1389397){bRouteRequested=true;Feedback=TEXT("The route is clear.");}
    };
    InteractionEngine->TryProcess(Target->EventId,Target->GetContext());PumpInteraction();++ViewRevision;
}
bool ALinkPlayerController::ChooseAction(int32 EntryId)
{
    if(!ActiveInteraction || !InteractionEngine || !ActionChoices.Contains(EntryId)){return false;}
    if(auto* Audio=GetWorld()->GetSubsystem<ULinkAudio>()){Audio->Command();}
    const auto* Entry=GetGameInstance()->GetSubsystem<ULinkSession>()->GetDatabase().Find(EntryId);
    Feedback=Entry && Entry->Key.Contains(TEXT("inspect"))?ActiveInteraction->DisplayName+TEXT(" inspected."):FString();
    ActionChoices.Reset();QueuedEntry=0;
    InteractionEngine->Process(EntryId,ActiveInteraction->GetContext());PumpInteraction();++ViewRevision;return true;
}
void ALinkPlayerController::PumpInteraction()
{
    auto* Session=GetGameInstance()->GetSubsystem<ULinkSession>();
    for(int32 Step=0;Step<256 && ActiveInteraction;++Step)
    {
        if(AwaitedParticipants){return;}
        if(bRouteRequested){CompleteRoute();return;}
        auto& Context=ActiveInteraction->GetContext();
        if(!InteractionEngine->GetError().IsEmpty()){Feedback=InteractionEngine->GetError();CloseInteractionView();return;}
        if(!QueuedEntry)
        {
            if(!InteractionEngine->Process(ActiveInteraction->EventId,Context) || !QueuedEntry){CloseInteractionView();return;}
        }
        const int32 Id=QueuedEntry;QueuedEntry=0;
        const auto* Entry=Session->GetDatabase().Find(Id);if(!Entry){continue;}
        if(Entry->Kind==ELinkStoryKind::Choice)
        {
            for(int32 Option:InteractionEngine->Matching(Id,Context))
            {
                const auto* Candidate=Session->GetDatabase().Find(Option);
                if(Candidate && Candidate->Kind==ELinkStoryKind::Dialogue){ActionChoices.Add(Option);}
            }
            InteractionEngine->Apply(Id,Context);
            if(ActionChoices.IsEmpty()){CloseInteractionView();}return;
        }
        if(Entry->Kind==ELinkStoryKind::Dialogue || Entry->Kind==ELinkStoryKind::Event)
        {
            InteractionEngine->Process(Id,Context);
        }
    }
    if(ActiveInteraction){Feedback=TEXT("No further action is available.");CloseInteractionView();}
}
void ALinkPlayerController::UpdatePartnerArrival()
{
    if(!ActiveInteraction||!AwaitedParticipants){return;}
    for(int32 Index=0;Index<2;++Index)
    {
        if((AwaitedParticipants&(1<<Index))&&!ActiveInteraction->IsReady(Index)){return;}
    }
    AwaitedParticipants=0;Feedback=TEXT("Both characters are here.");PumpInteraction();++ViewRevision;
}

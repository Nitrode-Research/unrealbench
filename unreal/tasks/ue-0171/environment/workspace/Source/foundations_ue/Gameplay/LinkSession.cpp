#include "Gameplay/LinkSession.h"
#include "Misc/Paths.h"

void ULinkSession::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    FString Error;
    bReady=Database.LoadFile(FPaths::ProjectContentDir()/TEXT("Data/Narrative.json"),Error);
    if (!bReady) { UE_LOG(LogTemp,Error,TEXT("Narrative initialization failed: %s"),*Error); }
    Globals.Add(LinkFacts::RTItem,LinkFacts::Gun);
}
FLinkFactMap& ULinkSession::GetInteractionFacts(const FString& Id,int32 InitialItem)
{
    auto& Facts=LocalFacts.FindOrAdd(Id);
    if (!Facts)
    {
        Facts=MakeShared<FLinkFactMap>();
        if (InitialItem) { Facts->Add(InitialItem,1); }
    }
    return *Facts;
}
int32 ULinkSession::HeldItem(int32 Player) const
{
    return Player==0 ? Globals.FindRef(LinkFacts::LTItem) : Player==1 ? Globals.FindRef(LinkFacts::RTItem) : 0;
}
FLinkFactMap* ULinkSession::FindInteractionFacts(const FString& Id)
{
    const auto* Facts=LocalFacts.Find(Id);return Facts?Facts->Get():nullptr;
}
bool ULinkSession::ObservePickup(int32 EntryId,FLinkStoryContext& Context)
{
    const auto* Entry=Database.Find(EntryId);
    if (!Entry || Entry->Kind!=ELinkStoryKind::Item) { return false; }
    FLinkStoryEngine Engine(Database);
    for(int32 Index=0;Index<2;++Index)
    {
        if(HeldItem(Index)==0 && Engine.Get(LinkFacts::CurrentSpeaker,Context)==(Index==0?LinkFacts::LT:LinkFacts::RT) && Engine.Get(EntryId,Context)==1)
        {
            Engine.Set(EntryId,0,Context);Globals.Add(Index==0?LinkFacts::LTItem:LinkFacts::RTItem,EntryId);return true;
        }
    }
    return false;
}
bool ULinkSession::ObserveUse(int32 EntryId,FLinkStoryContext& Context)
{
    const int32 Index=EntryId==LinkFacts::LTItem?0:EntryId==LinkFacts::RTItem?1:INDEX_NONE;
    if(Index==INDEX_NONE || !Context.Interaction || HeldItem(Index)==0){return false;}
    Context.Interaction->Add(HeldItem(Index),1);Globals.Add(EntryId,0);return true;
}
FString ULinkSession::ItemLabel(int32 Item) const
{
    const auto* Entry=Database.Find(Item);
    if(!Entry){return TEXT("Empty hands");}
    FString Label=Entry->Key;Label.RemoveFromStart(TEXT("I_"));Label.ReplaceInline(TEXT("_"),TEXT(" "));
    if(!Label.IsEmpty()){Label[0]=FChar::ToUpper(Label[0]);}return Label;
}
void ULinkSession::CaptureFacts(FLinkSaveSnapshot& Snapshot) const
{
    Snapshot.Globals=Globals;Snapshot.bCompleted=bRunCompleted;Snapshot.Interactions.Reset();
    for(const auto& Pair:LocalFacts){if(Pair.Value){Snapshot.Interactions.Add(Pair.Key,*Pair.Value);}}
}
void ULinkSession::ApplyPendingFacts()
{
    if(!PendingRestore){return;}
    Globals=PendingRestore->Globals;bRunCompleted=PendingRestore->bCompleted;LocalFacts.Reset();
    for(const auto& Pair:PendingRestore->Interactions){LocalFacts.Add(Pair.Key,MakeShared<FLinkFactMap>(Pair.Value));}
}
bool ULinkSession::ConsumeSpawnState(const FString& Map,FLinkSaveSnapshot& Out)
{
    if(!PendingRestore||PendingRestore->Map!=Map){return false;}
    Out=MoveTemp(PendingRestore.GetValue());PendingRestore.Reset();return true;
}

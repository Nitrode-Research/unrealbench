#include "Gameplay/LinkSession.h"
#include "Misc/Paths.h"

void ULinkSession::Initialize(FSubsystemCollectionBase& Collection)
{
    // Restore the documented B gameplay contract.
    
}
FLinkFactMap& ULinkSession::GetInteractionFacts(const FString& Id,int32 InitialItem)
{
    // Restore the documented B gameplay contract.
    static FLinkFactMap Missing; return Missing;
}
int32 ULinkSession::HeldItem(int32 Player) const
{
    // Restore the documented B gameplay contract.
    return {};
}
FLinkFactMap* ULinkSession::FindInteractionFacts(const FString& Id)
{
    // Restore the documented B gameplay contract.
    return nullptr;
}
bool ULinkSession::ObservePickup(int32 EntryId,FLinkStoryContext& Context)
{
    // Restore the documented B gameplay contract.
    return {};
}
bool ULinkSession::ObserveUse(int32 EntryId,FLinkStoryContext& Context)
{
    // Restore the documented B gameplay contract.
    return {};
}
FString ULinkSession::ItemLabel(int32 Item) const
{
    // Restore the documented B gameplay contract.
    return {};
}
void ULinkSession::CaptureFacts(FLinkSaveSnapshot& Snapshot) const
{
    // Restore the documented B gameplay contract.
    
}
void ULinkSession::ApplyPendingFacts()
{
    // Restore the documented B gameplay contract.
    
}
bool ULinkSession::ConsumeSpawnState(const FString& Map,FLinkSaveSnapshot& Out)
{
    // Restore the documented B gameplay contract.
    return {};
}

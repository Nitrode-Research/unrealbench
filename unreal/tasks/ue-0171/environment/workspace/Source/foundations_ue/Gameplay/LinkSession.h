#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Rules/LinkStory.h"
#include "Rules/LinkSave.h"
#include "LinkSession.generated.h"

UCLASS()
class FOUNDATIONS_UE_API ULinkSession : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    const FLinkStoryDatabase& GetDatabase() const { return Database; }
    FLinkFactMap& GetGlobals() { return Globals; }
    FLinkFactMap& GetInteractionFacts(const FString& Id,int32 InitialItem);
    FLinkFactMap* FindInteractionFacts(const FString& Id);
    bool IsReady() const { return bReady; }
    bool IsRunCompleted() const{return bRunCompleted;}
    void SetRunCompleted(bool Value){bRunCompleted=Value;}
    int32 HeldItem(int32 Player) const;
    bool ObservePickup(int32 EntryId,FLinkStoryContext& Context);
    bool ObserveUse(int32 EntryId,FLinkStoryContext& Context);
    FString ItemLabel(int32 Item) const;
    void CaptureFacts(FLinkSaveSnapshot& Snapshot) const;
    void StageRestore(FLinkSaveSnapshot Snapshot){PendingRestore=MoveTemp(Snapshot);}
    void ApplyPendingFacts();
    bool ConsumeSpawnState(const FString& Map,FLinkSaveSnapshot& Out);
private:
    FLinkStoryDatabase Database;
    FLinkFactMap Globals;
    TMap<FString,TSharedPtr<FLinkFactMap>> LocalFacts;
    bool bReady=false;
    bool bRunCompleted=false;
    TOptional<FLinkSaveSnapshot> PendingRestore;
};

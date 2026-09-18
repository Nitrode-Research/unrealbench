#include "Rules/LinkStory.h"

namespace
{
bool IsRuleKind(ELinkStoryKind Kind)
{
    return Kind == ELinkStoryKind::Rule || Kind == ELinkStoryKind::Dialogue || Kind == ELinkStoryKind::Choice;
}
}

FLinkFactMap* FLinkStoryContext::Resolve(int32 Scope)
{
    if (Scope == LinkFacts::GlobalScope) { return Global; }
    if (Scope == LinkFacts::InteractionScope) { return Interaction; }
    return Scope == LinkFacts::ContextScope ? &Local : nullptr;
}
const FLinkFactMap* FLinkStoryContext::Resolve(int32 Scope) const
{
    if (Scope == LinkFacts::GlobalScope) { return Global; }
    if (Scope == LinkFacts::InteractionScope) { return Interaction; }
    return Scope == LinkFacts::ContextScope ? &Local : nullptr;
}

bool FLinkStoryDatabase::Initialize(TArray<FLinkStoryEntry> InEntries, FString& Error)
{
    Error.Reset();
    Entries.Reset(); Indices.Reset(); Relations.Reset();
    TSet<int32> Seen;
    for (const auto& Entry : InEntries)
    {
        if (Entry.Id == 0 || Seen.Contains(Entry.Id)) { Error = TEXT("Duplicate or zero narrative entry ID"); return false; }
        Seen.Add(Entry.Id);
    }
    Entries = MoveTemp(InEntries);
    for (int32 Index = 0; Index < Entries.Num(); ++Index) { Indices.Add(Entries[Index].Id, Index); }
    for (const auto& Entry : Entries)
    {
        for (int32 Trigger : Entry.Triggers)
        {
            auto& List = Relations.FindOrAdd(Trigger);
            int32 Low = 0, High = List.Num() - 1;
            while (Low <= High)
            {
                const int32 Middle = (Low + High) / 2;
                const int32 ExistingWeight = Find(List[Middle])->Weight();
                if (Entry.Weight() == ExistingWeight) { Low = Middle; break; }
                if (Entry.Weight() < ExistingWeight) { Low = Middle + 1; }
                else { High = Middle - 1; }
            }
            List.Insert(Entry.Id, Low);
        }
    }
    return true;
}

const FLinkStoryEntry* FLinkStoryDatabase::Find(int32 Id) const
{
    const int32* Index = Indices.Find(Id);
    return Index ? &Entries[*Index] : nullptr;
}
const FLinkStoryEntry* FLinkStoryDatabase::FindByKey(const FString& Key, const FString& Table) const
{
    const FLinkStoryEntry* Match = nullptr;
    for (const auto& Entry : Entries)
    {
        if (Entry.Key.Equals(Key, ESearchCase::CaseSensitive)
            && (Table.IsEmpty() || Entry.Table.Equals(Table, ESearchCase::CaseSensitive)))
        {
            if (Match) { return nullptr; } // Ambiguous cross-scene keys must use a table or ID.
            Match = &Entry;
        }
    }
    return Match;
}
const TArray<int32>& FLinkStoryDatabase::Related(int32 Id) const
{
    static const TArray<int32> Empty;
    const TArray<int32>* List = Relations.Find(Id);
    return List ? *List : Empty;
}

int32 FLinkStoryEngine::Get(int32 Fact, const FLinkStoryContext& Context) const
{
    const auto* Entry = Database.Find(Fact);
    const auto* Map = Entry ? Context.Resolve(Entry->Scope) : nullptr;
    return Map ? Map->FindRef(Fact) : 0;
}
bool FLinkStoryEngine::Set(int32 Fact, int32 Value, FLinkStoryContext& Context) const
{
    const auto* Entry = Database.Find(Fact);
    auto* Map = Entry ? Context.Resolve(Entry->Scope) : nullptr;
    if (!Map) { return false; }
    Map->Add(Fact, Value);
    return true;
}

bool FLinkStoryEngine::Test(int32 Id, const FLinkStoryContext& Context) const
{
    const auto* Entry = Database.Find(Id);
    if (!Entry) { return false; }
    if (Entry->bOnce && Entry->Scope != 0 && Get(Id, Context) != 0) { return false; }
    for (const auto& Criterion : Entry->Criteria)
    {
        const auto* Fact = Database.Find(Criterion.Fact);
        if (!Fact || !Context.Resolve(Fact->Scope)) { return false; }
        const int32 Value = Get(Criterion.Fact, Context);
        if (Value < Criterion.Min || Value > Criterion.Max) { return false; }
    }
    return true;
}

TArray<int32> FLinkStoryEngine::Matching(int32 Trigger, const FLinkStoryContext& Context) const
{
    TArray<int32> Result;
    if (Database.Find(Trigger))
    {
        for (int32 Id : Database.Related(Trigger)) { if (Test(Id, Context)) { Result.Add(Id); } }
    }
    return Result;
}

void FLinkStoryEngine::BeginExecution()
{
    if (Depth == 0) { Error.Reset(); Operations = 0; }
}
bool FLinkStoryEngine::SpendOperation()
{
    if (++Operations <= 128) { return true; }
    Error = TEXT("Narrative dispatch exceeded 128 operations (possible cycle)");
    return false;
}
bool FLinkStoryEngine::Dispatch(const TArray<int32>& References, FLinkStoryContext& Context)
{
    for (int32 Id : References) { if (Test(Id, Context) && !Invoke(Id, Context)) { return false; } }
    return Error.IsEmpty();
}

bool FLinkStoryEngine::Apply(int32 Id, FLinkStoryContext& Context)
{
    BeginExecution();
    TGuardValue<int32> Guard(Depth, Depth + 1);
    const auto* Entry = Database.Find(Id);
    if (!Entry || !SpendOperation()) { return false; }
    if (Entry->Kind == ELinkStoryKind::Dialogue) { Set(LinkFacts::CurrentSpeaker, Entry->Speaker, Context); }
    for (const auto& Modification : Entry->Modifications)
    {
        if (Modification.Operation == 0 || Modification.Operation == 1) { Set(Modification.Fact, Modification.Value, Context); }
        else if (Modification.Operation == 2)
        {
            Set(Modification.Fact, int32(uint32(Get(Modification.Fact, Context)) + uint32(Modification.Value)), Context);
        }
    }
    if (Entry->Scope != 0) { Set(Id, int32(uint32(Get(Id, Context)) + 1u), Context); }
    return !IsRuleKind(Entry->Kind) || Dispatch(Entry->OnApply, Context);
}

bool FLinkStoryEngine::Invoke(int32 Id, FLinkStoryContext& Context)
{
    BeginExecution();
    TGuardValue<int32> Guard(Depth, Depth + 1);
    const auto* Entry = Database.Find(Id);
    if (!Entry || !SpendOperation()) { return false; }
    if (IsRuleKind(Entry->Kind) && !Dispatch(Entry->OnInvoke, Context)) { return false; }
    if (OnInvoked) { OnInvoked(Id, Context); }
    if (OnSpecificInvoked) { OnSpecificInvoked(Id, Context); }
    return Error.IsEmpty();
}

bool FLinkStoryEngine::Process(int32 Id, FLinkStoryContext& Context)
{
    BeginExecution();
    TGuardValue<int32> Guard(Depth, Depth + 1);
    if (!Apply(Id, Context)) { return false; }
    const auto Matches = Matching(Id, Context);
    return !Matches.IsEmpty() && Invoke(Matches[0], Context);
}

bool FLinkStoryEngine::TryProcess(int32 Id, FLinkStoryContext& Context)
{
    BeginExecution();
    return Test(Id, Context) && Process(Id, Context);
}

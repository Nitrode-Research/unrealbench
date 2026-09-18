// Typewriter semantics derived from the pinned MIT source. See THIRD_PARTY_NOTICES.md.
#pragma once
#include "CoreMinimal.h"

namespace LinkFacts
{
    inline constexpr int32 GlobalScope = 475414559;
    inline constexpr int32 InteractionScope = -1936302086;
    inline constexpr int32 ContextScope = -1768494618;
    inline constexpr int32 LT = -417971899, RT = -2078916288;
    inline constexpr int32 CurrentSpeaker = -1596563589;
    inline constexpr int32 Initiator = -963223665, Listener = 1809211332;
    inline constexpr int32 IsLTPresent = -95002655, IsRTPresent = -514433065;
    inline constexpr int32 InitialEvent = -52982722, CallOther = 2014434846;
    inline constexpr int32 LTItem = 1389380, RTItem = 1389381, Gun = 1389507;
}

enum class ELinkStoryKind : uint8 { Rule, Dialogue, Choice, Event, Fact, Item, Scope };
struct FLinkCriterion { int32 Fact = 0, Min = 0, Max = 0; };
struct FLinkModification { int32 Fact = 0, Value = 0, Operation = 0; };

struct FLinkStoryEntry
{
    int32 Id = 0;
    FString Key, Table;
    ELinkStoryKind Kind = ELinkStoryKind::Fact;
    int32 Scope = 0, Padding = 0, Speaker = 0, Style = 0, Icon = 0;
    bool bOnce = false, bCancellable = false;
    FString Text;
    TArray<int32> Triggers, OnApply, OnInvoke;
    TArray<FLinkCriterion> Criteria;
    TArray<FLinkModification> Modifications;
    int32 Weight() const { return Criteria.Num() + Padding; }
};

using FLinkFactMap = TMap<int32, int32>;
struct FLinkStoryContext
{
    FLinkFactMap* Global = nullptr;
    FLinkFactMap* Interaction = nullptr;
    FLinkFactMap Local;
    FLinkFactMap* Resolve(int32 Scope);
    const FLinkFactMap* Resolve(int32 Scope) const;
};

/** Immutable ordered entry registry with source-compatible relation construction. */
class FLinkStoryDatabase
{
public:
    bool Initialize(TArray<FLinkStoryEntry> InEntries, FString& Error);
    bool LoadJson(const FString& Json, FString& Error);
    bool LoadFile(const FString& Path, FString& Error);
    const FLinkStoryEntry* Find(int32 Id) const;
    const FLinkStoryEntry* FindByKey(const FString& Key, const FString& Table = FString()) const;
    const TArray<int32>& Related(int32 Id) const;
    const TArray<FLinkStoryEntry>& All() const { return Entries; }
private:
    TArray<FLinkStoryEntry> Entries;
    TMap<int32, int32> Indices;
    TMap<int32, TArray<int32>> Relations;
};

/** Synchronous rule evaluator. Presentation chooses when to apply displayed entries. */
class FLinkStoryEngine
{
public:
    explicit FLinkStoryEngine(const FLinkStoryDatabase& InDatabase) : Database(InDatabase) {}
    int32 Get(int32 Fact, const FLinkStoryContext& Context) const;
    bool Set(int32 Fact, int32 Value, FLinkStoryContext& Context) const;
    bool Test(int32 Entry, const FLinkStoryContext& Context) const;
    TArray<int32> Matching(int32 Trigger, const FLinkStoryContext& Context) const;
    bool Apply(int32 Entry, FLinkStoryContext& Context);
    bool Invoke(int32 Entry, FLinkStoryContext& Context);
    bool Process(int32 Entry, FLinkStoryContext& Context);
    bool TryProcess(int32 Entry, FLinkStoryContext& Context);
    const FString& GetError() const { return Error; }
    // Source global listeners run before per-entry listeners.
    TFunction<void(int32, FLinkStoryContext&)> OnInvoked;
    TFunction<void(int32, FLinkStoryContext&)> OnSpecificInvoked;
private:
    void BeginExecution();
    bool SpendOperation();
    bool Dispatch(const TArray<int32>& References, FLinkStoryContext& Context);
    const FLinkStoryDatabase& Database;
    FString Error;
    int32 Depth = 0, Operations = 0;
};

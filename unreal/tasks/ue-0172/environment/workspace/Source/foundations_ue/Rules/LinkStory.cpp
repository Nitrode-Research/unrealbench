#include "Rules/LinkStory.h"

namespace
{
bool IsRuleKind(ELinkStoryKind Kind)
{
    // Restore the documented B gameplay contract.
    return {};
}
}

FLinkFactMap* FLinkStoryContext::Resolve(int32 Scope)
{
    // Restore the documented B gameplay contract.
    return nullptr;
}
const FLinkFactMap* FLinkStoryContext::Resolve(int32 Scope) const
{
    // Restore the documented B gameplay contract.
    return nullptr;
}

bool FLinkStoryDatabase::Initialize(TArray<FLinkStoryEntry> InEntries, FString& Error)
{
    // Restore the documented B gameplay contract.
    return {};
}

const FLinkStoryEntry* FLinkStoryDatabase::Find(int32 Id) const
{
    // Restore the documented B gameplay contract.
    return nullptr;
}
const FLinkStoryEntry* FLinkStoryDatabase::FindByKey(const FString& Key, const FString& Table) const
{
    // Restore the documented B gameplay contract.
    return nullptr;
}
const TArray<int32>& FLinkStoryDatabase::Related(int32 Id) const
{
    // Restore the documented B gameplay contract.
    static TArray<int32> Missing; return Missing;
}

int32 FLinkStoryEngine::Get(int32 Fact, const FLinkStoryContext& Context) const
{
    // Restore the documented B gameplay contract.
    return {};
}
bool FLinkStoryEngine::Set(int32 Fact, int32 Value, FLinkStoryContext& Context) const
{
    // Restore the documented B gameplay contract.
    return {};
}

bool FLinkStoryEngine::Test(int32 Id, const FLinkStoryContext& Context) const
{
    // Restore the documented B gameplay contract.
    return {};
}

TArray<int32> FLinkStoryEngine::Matching(int32 Trigger, const FLinkStoryContext& Context) const
{
    // Restore the documented B gameplay contract.
    return {};
}

void FLinkStoryEngine::BeginExecution()
{
    // Restore the documented B gameplay contract.
    
}
bool FLinkStoryEngine::SpendOperation()
{
    // Restore the documented B gameplay contract.
    return {};
}
bool FLinkStoryEngine::Dispatch(const TArray<int32>& References, FLinkStoryContext& Context)
{
    // Restore the documented B gameplay contract.
    return {};
}

bool FLinkStoryEngine::Apply(int32 Id, FLinkStoryContext& Context)
{
    // Restore the documented B gameplay contract.
    return {};
}

bool FLinkStoryEngine::Invoke(int32 Id, FLinkStoryContext& Context)
{
    // Restore the documented B gameplay contract.
    return {};
}

bool FLinkStoryEngine::Process(int32 Id, FLinkStoryContext& Context)
{
    // Restore the documented B gameplay contract.
    return {};
}

bool FLinkStoryEngine::TryProcess(int32 Id, FLinkStoryContext& Context)
{
    // Restore the documented B gameplay contract.
    return {};
}

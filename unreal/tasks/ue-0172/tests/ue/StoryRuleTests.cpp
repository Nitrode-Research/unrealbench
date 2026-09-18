#include "Misc/AutomationTest.h"
#include "Rules/LinkStory.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FLinkStoryEntry Entry(int32 Id, int32 Scope=LinkFacts::GlobalScope)
{
    FLinkStoryEntry Value;
    Value.Id=Id; Value.Scope=Scope;
    Value.Key=FString::Printf(TEXT("Fixture_%d"),Id);
    Value.Table=TEXT("Fixture");
    return Value;
}
bool InitializeFixture(FLinkStoryDatabase& Database,TArray<FLinkStoryEntry> Entries,FString& Error)
{
    // Scope IDs are registry references too, as in the supplied narrative.
    for (int32 Id : {LinkFacts::GlobalScope,LinkFacts::InteractionScope,LinkFacts::ContextScope})
    {
        auto Scope=Entry(Id,0); Scope.Kind=ELinkStoryKind::Scope;
        Entries.Add(Scope);
    }
    return Database.Initialize(MoveTemp(Entries),Error);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkStoryRules,
    "Task0172.Headless.Story.ScopesPriorityAndOnce", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkStoryRules::RunTest(const FString& Parameters)
{
    auto Start=Entry(1), Low=Entry(2), High=Entry(3), Local=Entry(4,LinkFacts::InteractionScope);
    Start.Kind=Low.Kind=High.Kind=ELinkStoryKind::Rule;
    Low.Triggers={1}; Low.Criteria={{4,1,2}};
    High.Triggers={1}; High.Padding=3; High.bOnce=true;
    FLinkStoryDatabase Database; FString Error;
    if (!TestTrue(TEXT("Fixture registry loads"), InitializeFixture(Database,{Start,Low,High,Local},Error)))
    {
        AddError(Error); return false;
    }
    FLinkFactMap Global, InteractionA, InteractionB;
    FLinkStoryContext Context{&Global,&InteractionA};
    FLinkStoryEngine Engine(Database);
    if (!TestTrue(TEXT("Local condition fact can be set"),Engine.Set(4,2,Context))) { return false; }
    TestTrue(TEXT("Condition includes upper bound"), Engine.Test(2,Context));
    Context.Interaction=&InteractionB;
    TestFalse(TEXT("Other interaction does not inherit local facts"), Engine.Test(2,Context));
    Context.Interaction=&InteractionA;
    TArray<int32> Emitted;
    Engine.OnInvoked=[&](int32 Id,FLinkStoryContext&) { Emitted.Add(Id); };
    TestTrue(TEXT("Process selects a response"), Engine.Process(1,Context));
    TestEqual(TEXT("Padding participates in response priority"), (Emitted.IsEmpty()?INDEX_NONE:Emitted.Last()), 3);
    TestEqual(TEXT("Invoking does not apply the displayed response"), Engine.Get(3,Context), 0);
    TestTrue(TEXT("Selected response can be applied"),Engine.Apply(3,Context));
    TestFalse(TEXT("Applied once-only response stops matching"), Engine.Test(3,Context));
    TestTrue(TEXT("Second process selects the remaining response"),Engine.Process(1,Context));
    TestEqual(TEXT("Next valid response is selected"), (Emitted.IsEmpty()?INDEX_NONE:Emitted.Last()), 2);
    TestEqual(TEXT("Trigger application count persists globally"), Engine.Get(1,Context), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkStoryEffects,
    "Task0172.Headless.Story.EffectsDispatchAndMissingReferences", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkStoryEffects::RunTest(const FString& Parameters)
{
    auto Line=Entry(1), Value=Entry(2), Event=Entry(3), Child=Entry(4);
    auto Speaker=Entry(LinkFacts::CurrentSpeaker,LinkFacts::ContextScope);
    auto RT=Entry(LinkFacts::RT,LinkFacts::ContextScope);
    auto MissingScope=Entry(5), LocalValue=Entry(6,LinkFacts::InteractionScope);
    Line.Kind=ELinkStoryKind::Dialogue; Line.Speaker=LinkFacts::RT;
    // Assignment (operation 0) is exercised by the supplied Narrative.json.
    // Integer operation codes 1/2 are not documented in the solver contract.
    Line.Modifications={{2,7,0},{2,LinkFacts::LT,0}};
    Line.OnApply={3}; Event.Kind=ELinkStoryKind::Rule; Event.OnInvoke={4};
    Child.Kind=ELinkStoryKind::Rule;
    MissingScope.Kind=ELinkStoryKind::Rule; MissingScope.Criteria={{6,0,0}};
    FLinkStoryDatabase Database; FString Error;
    if (!TestTrue(TEXT("Effects fixture registry loads"),InitializeFixture(Database,{Line,Value,Event,Child,Speaker,RT,MissingScope,LocalValue},Error)))
    {
        AddError(Error); return false;
    }
    FLinkFactMap Global, Local;
    FLinkStoryContext Context{&Global,&Local};
    FLinkStoryEngine Engine(Database);
    TArray<int32> Events;
    Engine.OnInvoked=[&](int32 Id,FLinkStoryContext&) { Events.Add(Id); };
    Engine.OnSpecificInvoked=[&](int32 Id,FLinkStoryContext&) { Events.Add(-Id); };
    // Keep the registry valid: missing references are runtime probes, not
    // unresolved references hidden in an unchecked initialization call.
    TestFalse(TEXT("Unknown entry cannot match"),Engine.Test(999,Context));
    Context.Interaction=nullptr;
    TestFalse(TEXT("Missing fact scope is false even for range zero"),Engine.Test(5,Context));
    Context.Interaction=&Local;
    TestFalse(TEXT("No response means Process returns false"), Engine.Process(1,Context));
    TestEqual(TEXT("Effects still apply when no response matches"), Engine.Get(2,Context), LinkFacts::LT);
    TestEqual(TEXT("Dialogue apply sets source speaker reference"), Engine.Get(LinkFacts::CurrentSpeaker,Context), LinkFacts::RT);
    const TArray<int32> Expected{4,-4,3,-3};
    TestTrue(TEXT("Child dispatch precedes parent, global precedes specific"), Events==Expected);
    TestEqual(TEXT("Dispatched event is invoked, not implicitly applied"), Engine.Get(3,Context), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkStoryCycle,
    "Task0172.Headless.Story.DispatchCycleIsBounded", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkStoryCycle::RunTest(const FString& Parameters)
{
    auto Loop=Entry(1); Loop.Kind=ELinkStoryKind::Rule; Loop.OnInvoke={1};
    FLinkStoryDatabase Database; FString Error;
    if (!TestTrue(TEXT("Cycle fixture registry loads"),InitializeFixture(Database,{Loop},Error)))
    {
        AddError(Error); return false;
    }
    FLinkStoryContext Context; FLinkStoryEngine Engine(Database);
    TestFalse(TEXT("Recursive dispatcher fails safely"), Engine.Invoke(1,Context));
    TestFalse(TEXT("Cycle produces an actionable error"), Engine.GetError().IsEmpty());
    return true;
}
#endif

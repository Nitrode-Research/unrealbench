#include "Misc/AutomationTest.h"
#include "Rules/LinkStory.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FLinkStoryEntry Entry(int32 Id, int32 Scope=LinkFacts::GlobalScope)
{
    FLinkStoryEntry Value; Value.Id=Id; Value.Scope=Scope; return Value;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkStoryRules,
    "Task0171.Headless.Story.ScopesPriorityAndOnce", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkStoryRules::RunTest(const FString& Parameters)
{
    auto Start=Entry(1), Low=Entry(2), High=Entry(3), Local=Entry(4,LinkFacts::InteractionScope);
    Low.Triggers={1}; Low.Criteria={{4,1,2}};
    High.Triggers={1}; High.Padding=3; High.bOnce=true;
    FLinkStoryDatabase Database; FString Error;
    TestTrue(TEXT("Fixture registry loads"), Database.Initialize({Start,Low,High,Local},Error));
    FLinkFactMap Global, InteractionA, InteractionB;
    FLinkStoryContext Context{&Global,&InteractionA};
    FLinkStoryEngine Engine(Database);
    Engine.Set(4,2,Context);
    TestTrue(TEXT("Condition includes upper bound"), Engine.Test(2,Context));
    Context.Interaction=&InteractionB;
    TestFalse(TEXT("Other interaction does not inherit local facts"), Engine.Test(2,Context));
    Context.Interaction=&InteractionA;
    TArray<int32> Emitted;
    Engine.OnInvoked=[&](int32 Id,FLinkStoryContext&) { Emitted.Add(Id); };
    TestTrue(TEXT("Process selects a response"), Engine.Process(1,Context));
    TestEqual(TEXT("Padding participates in response priority"), (Emitted.IsEmpty()?INDEX_NONE:Emitted.Last()), 3);
    TestEqual(TEXT("Invoking does not apply the displayed response"), Engine.Get(3,Context), 0);
    Engine.Apply(3,Context);
    TestFalse(TEXT("Applied once-only response stops matching"), Engine.Test(3,Context));
    Engine.Process(1,Context);
    TestEqual(TEXT("Next valid response is selected"), (Emitted.IsEmpty()?INDEX_NONE:Emitted.Last()), 2);
    TestEqual(TEXT("Trigger application count persists globally"), Engine.Get(1,Context), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkStoryEffects,
    "Task0171.Headless.Story.EffectsDispatchAndMissingReferences", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkStoryEffects::RunTest(const FString& Parameters)
{
    auto Line=Entry(1), Value=Entry(2), Event=Entry(3), Child=Entry(4), Speaker=Entry(LinkFacts::CurrentSpeaker,LinkFacts::ContextScope), Missing=Entry(5);
    Line.Kind=ELinkStoryKind::Dialogue; Line.Speaker=LinkFacts::RT;
    Line.Modifications={{2,7,0},{2,5,2},{2,LinkFacts::LT,1}};
    Line.OnApply={3,999}; Event.Kind=ELinkStoryKind::Rule; Event.OnInvoke={4};
    Missing.Criteria={{999,0,0}};
    FLinkStoryDatabase Database; FString Error;
    Database.Initialize({Line,Value,Event,Child,Speaker,Missing},Error);
    FLinkFactMap Global, Local;
    FLinkStoryContext Context{&Global,&Local};
    FLinkStoryEngine Engine(Database);
    TArray<int32> Events;
    Engine.OnInvoked=[&](int32 Id,FLinkStoryContext&) { Events.Add(Id); };
    Engine.OnSpecificInvoked=[&](int32 Id,FLinkStoryContext&) { Events.Add(-Id); };
    TestFalse(TEXT("Missing fact is false even for range zero"), Engine.Test(5,Context));
    TestFalse(TEXT("No response means Process returns false"), Engine.Process(1,Context));
    TestEqual(TEXT("Effects still apply when no response matches"), Engine.Get(2,Context), LinkFacts::LT);
    TestEqual(TEXT("Dialogue apply sets source speaker reference"), Engine.Get(LinkFacts::CurrentSpeaker,Context), LinkFacts::RT);
    const TArray<int32> Expected{4,-4,3,-3};
    TestTrue(TEXT("Child dispatch precedes parent, global precedes specific"), Events==Expected);
    TestEqual(TEXT("Dispatched event is invoked, not implicitly applied"), Engine.Get(3,Context), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkStoryCycle,
    "Task0171.Headless.Story.DispatchCycleIsBounded", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkStoryCycle::RunTest(const FString& Parameters)
{
    auto Loop=Entry(1); Loop.Kind=ELinkStoryKind::Rule; Loop.OnInvoke={1};
    FLinkStoryDatabase Database; FString Error; Database.Initialize({Loop},Error);
    FLinkStoryContext Context; FLinkStoryEngine Engine(Database);
    TestFalse(TEXT("Recursive dispatcher fails safely"), Engine.Invoke(1,Context));
    TestFalse(TEXT("Cycle produces an actionable error"), Engine.GetError().IsEmpty());
    return true;
}
#endif

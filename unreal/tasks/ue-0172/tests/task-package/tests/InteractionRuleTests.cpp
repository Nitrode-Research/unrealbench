#include "Misc/AutomationTest.h"
#include "Rules/LinkInteraction.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkInteractionAllocation,
    "Task0172.Headless.Interactions.WaypointAllocationAndRoles", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkInteractionAllocation::RunTest(const FString& Parameters)
{
    const TArray<FVector> Points{FVector(-100,0,0),FVector(100,0,0)};
    FLinkInteractionState State;
    TestTrue(TEXT("LT claims closest waypoint"),State.Focus(0,FVector(-200,0,0),Points));
    TestEqual(TEXT("First participant is initiator"),State.GetInitiator(),LinkFacts::LT);
    State.SetReady(0,true);
    TestTrue(TEXT("RT can approach from the occupied side"),State.Focus(1,FVector(-150,0,0),Points));
    TestEqual(TEXT("Incoming RT gets the near waypoint"),State.Waypoint(1),0);
    TestEqual(TEXT("Existing LT relocates to alternate waypoint"),State.Waypoint(0),1);
    TestEqual(TEXT("Second participant is listener"),State.GetListener(),LinkFacts::RT);
    TestTrue(TEXT("Relocation retains area-based readiness until world update"),State.IsReady(0));
    TestFalse(TEXT("Incoming participant is not ready before arrival"),State.IsReady(1));
    State.Leave(0);
    TestEqual(TEXT("Remaining participant becomes initiator"),State.GetInitiator(),LinkFacts::RT);
    TestEqual(TEXT("Leaving clears listener"),State.GetListener(),0);
    TestEqual(TEXT("Only RT remains active"),State.ActiveMask(),uint8(2));
    FLinkInteractionState Alternative;
    Alternative.Focus(0,FVector(-200,0,0),Points);
    Alternative.Focus(1,FVector(0,50,0),Points);
    TestEqual(TEXT("Positive-side approach uses free waypoint"),Alternative.Waypoint(1),1);
    TestEqual(TEXT("Existing participant stays when relocation unnecessary"),Alternative.Waypoint(0),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkInteractionPresence,
    "Task0172.Headless.Interactions.PresenceReadinessAndInvalidLayout", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkInteractionPresence::RunTest(const FString& Parameters)
{
    FLinkInteractionState State;
    TestEqual(TEXT("Free character is absent"),State.Presence(0,false),0);
    TestEqual(TEXT("Character at another interaction is distinct from free"),State.Presence(0,true),-1);
    TestFalse(TEXT("Malformed one-waypoint layout is rejected"),State.Focus(0,FVector::ZeroVector,{FVector::ZeroVector}));
    TestEqual(TEXT("Rejected layout does not partially activate"),State.ActiveMask(),uint8(0));
    State.SetReady(0,true);
    TestFalse(TEXT("Inactive character cannot be ready"),State.IsReady(0));
    const TArray<FVector> Points{FVector(-100,0,0),FVector(100,0,0)};
    State.Focus(0,FVector::ZeroVector,Points);
    TestEqual(TEXT("Here overrides elsewhere when context updates"),State.Presence(0,true),1);
    State.SetReady(0,true);
    TestTrue(TEXT("Area arrival enables readiness"),State.IsReady(0));
    State.SetReady(0,false);
    TestFalse(TEXT("Area departure removes readiness"),State.IsReady(0));
    return true;
}
#endif

#include "Misc/AutomationTest.h"
#include "Rules/LinkCommands.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkCommandHandoff,
    "Task0172.Headless.Commands.HoldAndGrace", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)

bool FLinkCommandHandoff::RunTest(const FString& Parameters)
{
    FLinkCommands Commands;
    auto Actions = Commands.Update({1, 1, 1.0});
    TestEqual(TEXT("Left press commands LT"), Actions.Navigate, uint8(1));
    Actions = Commands.Update({2, 3, 1.05});
    TestEqual(TEXT("Second press requests RT follow"), Actions.Follow, uint8(2));
    TestEqual(TEXT("First command retains leadership"), Commands.GetLeader(), 0);
    TestTrue(TEXT("Both held requests tight spacing"), Commands.IsTightFollowing());
    Actions = Commands.Update({0, 2, 1.10});
    TestEqual(TEXT("Release inside grace does not hand off"), Actions.Navigate, uint8(0));
    TestEqual(TEXT("Previous leader retained during grace"), Commands.GetLeader(), 0);
    Actions = Commands.Update({0, 2, 1.16});
    TestEqual(TEXT("Remaining held button takes leadership after grace"), Actions.Navigate, uint8(2));
    TestEqual(TEXT("Focus follows handoff"), Commands.GetFocus(), 1);
    TestFalse(TEXT("Single held button ends tight spacing"), Commands.IsTightFollowing());
    Commands.Update({0, 0, 1.2});
    TestEqual(TEXT("All buttons released clears command ownership"), Commands.GetLeader(), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkCommandEdges,
    "Task0172.Headless.Commands.SimultaneousAndInvalidTargets", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)

bool FLinkCommandEdges::RunTest(const FString& Parameters)
{
    FLinkCommands Commands;
    auto Actions = Commands.Update({3, 3, 2.0});
    TestEqual(TEXT("Simultaneous buttons choose LT first"), Actions.Navigate, uint8(1));
    TestEqual(TEXT("Simultaneous RT becomes follower"), Actions.Follow, uint8(2));
    Commands.Update({0, 0, 2.1});
    TestTrue(TEXT("Source retains tight-follow latch after simultaneous release"), Commands.IsTightFollowing());
    Commands.Update({1, 1, 2.2});
    TestFalse(TEXT("Next performed input resets tight-follow latch"), Commands.IsTightFollowing());
    Commands.Reset();
    Actions = Commands.Update({1, 1, 3.0, false});
    TestEqual(TEXT("Press outside viewport cannot issue move"), Actions.Navigate, uint8(0));
    TestEqual(TEXT("Press outside viewport cannot steal focus"), Commands.GetFocus(), INDEX_NONE);
    Commands.Reset();
    Actions = Commands.Update({2, 2, 4.0, true, ELinkTargetKind::None});
    TestEqual(TEXT("No ground hit cannot issue move"), Actions.Navigate, uint8(0));
    TestEqual(TEXT("No ground hit cannot own held command"), Commands.GetLeader(), INDEX_NONE);
    Commands.Reset();
    Actions = Commands.Update({2, 2, 5.0, true, ELinkTargetKind::Interaction});
    TestEqual(TEXT("Interaction remains distinct from navigation"), Actions.Interact, uint8(2));
    TestEqual(TEXT("Interaction does not capture movement ownership"), Commands.GetLeader(), INDEX_NONE);
    return true;
}
#endif

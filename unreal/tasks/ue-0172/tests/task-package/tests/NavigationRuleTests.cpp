#include "Misc/AutomationTest.h"
#include "Rules/LinkNavigation.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkPathLimitTest,
    "Task0172.Headless.Navigation.PathDistanceLimit", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkPathLimitTest::RunTest(const FString& Parameters)
{
    const TArray<FVector> AroundCorner{FVector(0,0,0), FVector(600,0,0), FVector(600,600,0)};
    FVector Limited;
    TestTrue(TEXT("1200 cm path exceeds 800 cm limit"), LinkNavigation::ClipPath(AroundCorner, 800, Limited));
    TestTrue(TEXT("Limit follows the corner, not a radial clamp"), Limited.Equals(FVector(600,200,0), 0.001));
    TestFalse(TEXT("Exact path length is allowed"), LinkNavigation::ClipPath(AroundCorner, 1200, Limited));
    TestFalse(TEXT("Empty path has no clipping point"), LinkNavigation::ClipPath({}, 800, Limited));
    TestTrue(TEXT("Zero-length segment does not divide by zero"), LinkNavigation::ClipPath({FVector::ZeroVector, FVector::ZeroVector, FVector(900,0,0)}, 800, Limited));
    TestTrue(TEXT("Zero-length segment preserves the limit"), Limited.Equals(FVector(800,0,0), 0.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkFollowRulesTest,
    "Task0172.Headless.Navigation.FollowSpacingAndAvoidance", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkFollowRulesTest::RunTest(const FString& Parameters)
{
    FLinkMotionTuning Tuning;
    TestTrue(TEXT("Source tuning is valid"), Tuning.IsValid());
    auto Output = LinkNavigation::Follow({FVector(0,0,0), FVector(600,0,0), FVector(500,0,0), FVector(1000,0,0), FVector::ZeroVector, 600, false}, Tuning);
    TestEqual(TEXT("Far follower reaches walk speed"), Output.Speed, 500.f);
    TestEqual(TEXT("Normal stop separation"), Output.StopDistance, 300.f);
    Output = LinkNavigation::Follow({FVector::ZeroVector, FVector(100,0,0), FVector::ZeroVector, FVector(100,0,0), FVector::ZeroVector, 100, true}, Tuning);
    TestEqual(TEXT("Tight spacing uses 50 cm stop distance"), Output.StopDistance, 50.f);
    TestEqual(TEXT("Mid tight range interpolates speed"), Output.Speed, 300.f);
    Output = LinkNavigation::Follow({FVector(100,0,0), FVector::ZeroVector, FVector(500,0,0), FVector(500,0,0), FVector::ZeroVector, 100, false}, Tuning);
    TestTrue(TEXT("Approaching partner produces lateral avoidance"), !FMath::IsNearlyZero(Output.Destination.Y));
    TestEqual(TEXT("Avoidance disables follow stop distance"), Output.StopDistance, 0.f);
    TestEqual(TEXT("Avoidance scales with overlap risk"), Output.Speed, 250.f);
    Tuning.MaxDistance = Tuning.MinDistance;
    TestFalse(TEXT("Invalid interpolation range rejected"), Tuning.IsValid());
    return true;
}
#endif

#include "Misc/AutomationTest.h"
#include "Rules/LinkLocomotion.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTripletLocomotion,"Task0171.Headless.Runtime.LocomotionClockAndEvents",EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FTripletLocomotion::RunTest(const FString& Parameters)
{
    FLinkLocomotion Clock;
    TestEqual(TEXT("Long remaining route blends actual and requested speed"),FLinkLocomotion::SmoothSpeed(0,100,500,1000,5,.1f,Clock.Config),.6f);
    TestEqual(TEXT("Near goal uses actual speed only"),FLinkLocomotion::SmoothSpeed(0,100,500,20,5,.1f,Clock.Config),.2f);
    auto Frame=Clock.Advance(.01f,.1f);
    TestFalse(TEXT("Threshold equality does not enter locomotion"),Frame.bTransition);
    Frame=Clock.Advance(.01f,.5f);TestTrue(TEXT("Above threshold starts idle-to-movement transition"),Frame.bTransition&&!Frame.bNextIdle);
    int32 Events=0;for(int32 I=0;I<200;++I){Frame=Clock.Advance(.01f,.5f);Events+=Frame.Steps.Num();}
    TestFalse(TEXT("Moving clock leaves idle"),Frame.bIdle);
    TestTrue(TEXT("Dominant clip produces step events"),Events>=2);
    Clock.Reset();Frame=Clock.Advance(0,0);
    TestTrue(TEXT("Reset is idle with no stale events"),Frame.bIdle&&!Frame.bTransition&&Frame.Steps.IsEmpty());
    FLinkLocomotion EqualBlend;Events=0;
    for(int32 I=0;I<200;++I){Events+=EqualBlend.Advance(.01f,.25f).Steps.Num();}
    TestEqual(TEXT("Equal final clip weights do not emit footsteps"),Events,0);
    return true;
}
#endif

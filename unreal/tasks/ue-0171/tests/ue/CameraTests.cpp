#include "Misc/AutomationTest.h"
#include "Rules/LinkCamera.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkCameraFraming,
    "Task0171.Headless.Camera.SharedFramingAndStableSmoothing",EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkCameraFraming::RunTest(const FString& Parameters)
{
    FLinkCameraTuning Tuning;
    const FVector Left(0,-400,100), Right(800,400,100);
    for (float Aspect : {0.6f,16.f/9.f,2.4f})
    {
        FLinkCameraState State;
        State.Step(Left,Right,FVector(500,0,0),FVector::ZeroVector,0,false,Aspect,1.f/60,Tuning);
        for (const FVector& Point : {Left,Right})
        {
            const FVector Delta=Point-State.Aim;
            TestTrue(TEXT("Both characters retain horizontal framing margin"),FMath::Abs(FVector::DotProduct(Delta,Tuning.Rotation().RotateVector(FVector::RightVector)))+100 <= State.Width/2);
            TestTrue(TEXT("Both characters retain vertical framing margin"),FMath::Abs(FVector::DotProduct(Delta,Tuning.Rotation().RotateVector(FVector::UpVector)))+100 <= State.Width/(2*Aspect));
        }
    }
    FLinkCameraState At30,At60;
    At30.Step(FVector::ZeroVector,FVector(400,0,0),FVector::ZeroVector,FVector::ZeroVector,INDEX_NONE,false,1.7f,0,Tuning);
    At60=At30;
    for (int32 Frame=0; Frame<30; ++Frame) { At30.Step(FVector(600,0,0),FVector(1000,0,0),FVector::ZeroVector,FVector::ZeroVector,INDEX_NONE,false,1.7f,1.f/30,Tuning); }
    for (int32 Frame=0; Frame<60; ++Frame) { At60.Step(FVector(600,0,0),FVector(1000,0,0),FVector::ZeroVector,FVector::ZeroVector,INDEX_NONE,false,1.7f,1.f/60,Tuning); }
    TestTrue(TEXT("Position smoothing is stable across frame rates"),At30.Aim.Equals(At60.Aim,0.01));
    TestTrue(TEXT("Camera approaches without overshooting"),At60.Aim.X > 790 && At60.Aim.X <= 800);
    At60.Step(FVector(10000,0,0),FVector(10400,0,0),FVector::ZeroVector,FVector::ZeroVector,INDEX_NONE,false,1.7f,1.f/60,Tuning);
    TestTrue(TEXT("Scene-scale teleport snaps rather than flying across the map"),At60.Aim.Equals(FVector(10200,0,0),0.01));
    return true;
}
#endif


#include "Misc/AutomationTest.h"
#include "Rules/LinkCamera.h"
#include "Rules/LinkCameraResponse.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Rules/LinkSceneCoordinates.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkCameraResponseOracle,"Task0172.Headless.Camera.SourceResponseOracle",EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkCameraResponseOracle::RunTest(const FString& Parameters)
{
    FString Text;TSharedPtr<FJsonObject> Data;
    if(!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Data/Tests/CameraResponse.json")))||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data)){AddError(TEXT("Missing live Unity camera response fixture"));return false;}
    auto Vector=[](const TSharedPtr<FJsonValue>& Value){const auto& A=Value->AsArray();return LinkSceneCoordinates::Position(FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber()));};
    double MaxSpring=0,MaxVelocity=0,MaxBody=0,MaxGroup=0;int32 SpringCount=0,BodyCount=0,FocusCount=0,FocusMismatch=0;
    for(const auto& Case:Data->GetArrayField(TEXT("cases")))
    {
        FLinkCameraTuning Tuning=FLinkCameraTuning::Load(true);FLinkCameraFocusState Spring;
        for(const auto& Value:Case->AsObject()->GetArrayField(TEXT("spring")))
        {
            const auto& A=Value->AsArray();if(A[5]->AsBool()){Spring.Settle();}
            Spring.Focus=ELinkCameraFocus(int32(A[1]->AsNumber()));Tuning.CameraWeight=A[2]->AsNumber();
            Spring.StepFixed(A[0]->AsNumber(),Tuning);
            MaxSpring=FMath::Max(MaxSpring,FMath::Abs(Spring.Position-A[3]->AsNumber()));
            MaxVelocity=FMath::Max(MaxVelocity,FMath::Abs(Spring.Velocity-A[4]->AsNumber()));++SpringCount;
        }
        FLinkCameraState Camera;bool bFirst=true;
        for(const auto& Value:Case->AsObject()->GetArrayField(TEXT("body")))
        {
            const auto& A=Value->AsArray();const FVector Target=Vector(A[1]),Expected=Vector(A[2]);
            if(bFirst){Camera.Aim=Expected+Tuning.Rotation().Vector()*Tuning.Distance;Camera.bInitialized=true;bFirst=false;}
            else {Camera.StepSource(Target,16.f/9.f,A[0]->AsNumber(),Tuning,A[6]->AsBool());}
            const FVector Actual=Camera.Aim-Tuning.Rotation().Vector()*Tuning.Distance;
            MaxBody=FMath::Max(MaxBody,FVector::Distance(Actual,Expected));
            FLinkCameraFocusState Group;Group.Position=A[5]->AsNumber();
            MaxGroup=FMath::Max(MaxGroup,FVector::Distance(Group.GroupPosition(Vector(A[3]),Vector(A[4])),Target));++BodyCount;
        }
        ELinkCameraFocus Focus=ELinkCameraFocus::None;
        for(const auto& Value:Case->AsObject()->GetArrayField(TEXT("focus")))
        {
            const auto& A=Value->AsArray();Focus=ResolveLinkCameraFocus(Focus,ELinkCameraFocus(int32(A[0]->AsNumber())),A[1]->AsBool(),A[2]->AsBool(),A[3]->AsBool(),A[4]->AsBool());
            if(int32(Focus)!=int32(A[5]->AsNumber())){++FocusMismatch;}++FocusCount;
        }
    }
    TestTrue(TEXT("Three frame-rate captures cover over 7000 original spring steps"),SpringCount>7000);
    TestTrue(TEXT("Original weight and velocity traces agree"),MaxSpring<.000005&&MaxVelocity<.00002);
    TestTrue(TEXT("Original camera body trace agrees within 0.2 mm"),BodyCount>2500&&MaxBody<.02);
    TestTrue(TEXT("Weighted group excludes nonpositive source members"),MaxGroup<.002);
    TestTrue(TEXT("All observed focus transitions agree"),FocusCount>2500&&FocusMismatch==0);
    AddInfo(FString::Printf(TEXT("Camera oracle: spring=%d weight=%.9g velocity=%.9g body=%d maxCm=%.9g groupCm=%.9g focus=%d mismatches=%d"),SpringCount,MaxSpring,MaxVelocity,BodyCount,MaxBody,MaxGroup,FocusCount,FocusMismatch));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkCameraResponseEdges,"Task0172.Headless.Camera.SourceResponseEdges",EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkCameraResponseEdges::RunTest(const FString& Parameters)
{
    const auto Tuning=FLinkCameraTuning::Load(true);
    FLinkCameraFocusState A,B;A.Focus=B.Focus=ELinkCameraFocus::Left;
    A.Advance(.1,Tuning);for(int32 I=0;I<20;++I){B.Advance(.005,Tuning);}
    TestEqual(TEXT("Render-frame grouping preserves the 5 ms spring"),A.Position,B.Position);
    const float Before=A.Position;A.Advance(0,Tuning);TestEqual(TEXT("Paused time does not advance focus"),A.Position,Before);
    A.Settle();TestEqual(TEXT("Exploration return retains current weight"),A.Position,Before);TestEqual(TEXT("Exploration return clears spring velocity"),A.Velocity,0.f);
    TestTrue(TEXT("Idle retains prior selection"),ResolveLinkCameraFocus(ELinkCameraFocus::Right,ELinkCameraFocus::Left,false,false,false,false)==ELinkCameraFocus::Right);
    TestTrue(TEXT("Dialogue holds the prior camera focus"),ResolveLinkCameraFocus(ELinkCameraFocus::Right,ELinkCameraFocus::Left,true,false,false,true)==ELinkCameraFocus::Right);
    TestTrue(TEXT("Shared interaction takes precedence over single selection"),ResolveLinkCameraFocus(ELinkCameraFocus::Left,ELinkCameraFocus::Left,true,true,false,false)==ELinkCameraFocus::Both);
    FLinkCameraState Camera;Camera.StepSource(FVector::ZeroVector,16.f/9.f,0,Tuning,true);
    const FVector Jump=Tuning.Rotation().RotateVector(FVector(0,2500,1400));Camera.StepSource(Jump,16.f/9.f,1.f/120,Tuning);
    const FVector Remaining=Tuning.Rotation().UnrotateVector(Jump-Camera.Aim);
    TestTrue(TEXT("Large target step obeys source horizontal hard guide"),FMath::Abs(Remaining.Y)<=Tuning.VerticalHalfHeight*Tuning.SoftZoneWidth*16/9+.001);
    TestTrue(TEXT("Large target step obeys source vertical hard guide"),FMath::Abs(Remaining.Z)<=Tuning.VerticalHalfHeight*Tuning.SoftZoneHeight+.001);
    TestFalse(TEXT("Large target steps do not use the prototype teleport snap"),Camera.Aim.Equals(Jump,.001));
    return true;
}
#endif


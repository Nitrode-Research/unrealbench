#include "Misc/AutomationTest.h"
#include "Rules/LinkInteractionCamera.h"
#include "Rules/LinkCamera.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkInteractionCameraOracle,"Task0171.Headless.Camera.InteractionTransitionOracle",EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkInteractionCameraOracle::RunTest(const FString& Parameters)
{
    FLinkInteractionCameraLibrary Library;if(!TestTrue(TEXT("Original camera profiles load"),Library.Load())){return false;}
    TestEqual(TEXT("Every EntryGate and LoadingDocks interaction is present"),Library.Profiles.Num(),12);
    const auto Tuning=FLinkCameraTuning::Load(true);FString Text;TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Data/Tests/InteractionCamera.json")))||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)){AddError(TEXT("Missing live interaction camera oracle"));return false;}
    auto Point=[](const TSharedPtr<FJsonValue>& V){const auto& A=V->AsArray();return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());};
    int32 Count=0,Mismatches=0;double MaxPosition=0,MaxHeight=0,MaxTime=0,MaxWeight=0;
    for(const auto& Case:Root->GetArrayField(TEXT("cases")))
    {
        FLinkCameraTransition Blend;
        for(const auto& Value:Case->AsObject()->GetArrayField(TEXT("samples")))
        {
            const auto& A=Value->AsArray();Blend.Step(FName(*A[1]->AsString()),A[0]->AsNumber(),Library.BlendSeconds);
            const auto Pose=Blend.Evaluate([&](FName Id)
            {
                if(const auto* P=Library.Profiles.Find(Id)){return FLinkCameraPose{P->Target-Tuning.Rotation().Vector()*Tuning.Distance,P->HalfHeight};}
                return FLinkCameraPose{Point(A[2]),A[3]->AsNumber()};
            });
            MaxPosition=FMath::Max(MaxPosition,FVector::Distance(Pose.Position,Point(A[4])));
            MaxHeight=FMath::Max(MaxHeight,FMath::Abs(Pose.HalfHeight-A[5]->AsNumber()));
            if(!Blend.From.IsEmpty()!=A[6]->AsBool()){++Mismatches;}
            MaxTime=FMath::Max3(MaxTime,FMath::Abs(Blend.Elapsed-A[7]->AsNumber()),FMath::Abs(Blend.Duration-A[8]->AsNumber()));
            MaxWeight=FMath::Max(MaxWeight,FMath::Abs(Blend.Weight()-A[9]->AsNumber()));++Count;
        }
    }
    TestTrue(TEXT("All recorded positions match within 0.1 mm"),Count>2800&&MaxPosition<.01);
    TestTrue(TEXT("Zoom agrees within 0.002 mm"),MaxHeight<.0002);
    TestTrue(TEXT("Interrupted/reversed timing and completion match"),Mismatches==0&&MaxTime<.000003&&MaxWeight<.000003);
    AddInfo(FString::Printf(TEXT("Interaction camera oracle: samples=%d positionCm=%.9g halfHeightCm=%.9g seconds=%.9g weight=%.9g completionMismatches=%d"),Count,MaxPosition,MaxHeight,MaxTime,MaxWeight,Mismatches));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkInteractionCameraEdges,"Task0171.Headless.Camera.InteractionTransitionEdges",EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkInteractionCameraEdges::RunTest(const FString& Parameters)
{
    FLinkInteractionCameraLibrary Library;TestFalse(TEXT("Malformed profiles are rejected"),Library.Parse(TEXT("{}")));
    TestTrue(TEXT("Profile library remains available after a rejected input"),Library.Load());
    const auto* Pole=Library.Profiles.Find(TEXT("LoadingDocks.TelephonePoleInteraction"));
    TestTrue(TEXT("Pole camera includes all five authored waypoints"),Pole&&Pole->Waypoints.Num()==5);
    FLinkCameraTransition Blend;Blend.Step(TEXT("Explore"),0,.6f);Blend.Step(TEXT("A"),.15f,.6f);
    const double Before=Blend.Weight();Blend.Step(TEXT("A"),0,.6f);TestEqual(TEXT("Zero elapsed time freezes blend progress"),Blend.Weight(),Before);
    Blend.Step(TEXT("Explore"),0,.6f);TestTrue(TEXT("Early cancellation shortens the return"),FMath::Abs(Blend.Duration-.15f)<1e-6);
    Blend.Step(TEXT("B"),0,0);TestTrue(TEXT("Zero-duration transition cuts"),Blend.From.IsEmpty()&&Blend.Selected==TEXT("B"));
    Blend.Step(TEXT("Explore"),0,.6f,true);TestTrue(TEXT("Explicit scene reset clears every pending blend"),Blend.From.IsEmpty()&&Blend.Selected==TEXT("Explore"));
    return true;
}
#endif


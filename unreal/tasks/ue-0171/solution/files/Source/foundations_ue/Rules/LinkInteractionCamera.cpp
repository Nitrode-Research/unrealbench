#include "Rules/LinkInteractionCamera.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FLinkInteractionCameraLibrary::Load()
{
    FString Text;return FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Data/Scenes/InteractionCameras.json")))&&Parse(Text);
}
bool FLinkInteractionCameraLibrary::Parse(const FString& Text)
{
    Profiles.Reset();TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;double Seconds=0;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root||
       !Root->TryGetNumberField(TEXT("blendSeconds"),Seconds)||!FMath::IsFinite(Seconds)||Seconds<0||
       !Root->TryGetArrayField(TEXT("profiles"),Rows)||Rows->IsEmpty()){return false;}
    auto ReadPoint=[](const TSharedPtr<FJsonValue>& Value,FVector& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!Value||!Value->TryGetArray(A)||A->Num()!=3){return false;}
        for(int32 I=0;I<3;++I){double N=0;if(!(*A)[I]->TryGetNumber(N)||!FMath::IsFinite(N)){return false;}Out[I]=N;}return true;
    };
    TMap<FName,FLinkInteractionCameraProfile> Loaded;
    for(const auto& Row:*Rows)
    {
        const auto P=Row->AsObject();FString Id,Scene;double Height=0,Priority=0;FLinkInteractionCameraProfile Profile;
        const TArray<TSharedPtr<FJsonValue>>* Points=nullptr;
        if(!P||!P->TryGetStringField(TEXT("id"),Id)||Id.IsEmpty()||Id==TEXT("Explore")||
            !P->TryGetStringField(TEXT("scene"),Scene)||Scene.IsEmpty()||!P->TryGetNumberField(TEXT("halfHeight"),Height)||
            !FMath::IsFinite(Height)||Height<=0||!P->TryGetNumberField(TEXT("priority"),Priority)||Priority!=100||
            !ReadPoint(P->TryGetField(TEXT("target")),Profile.Target)||!P->TryGetArrayField(TEXT("waypoints"),Points)||Points->IsEmpty()){return false;}
        FVector Sum=FVector::ZeroVector;
        for(const auto& Point:*Points){FVector Position;if(!ReadPoint(Point,Position)){return false;}Profile.Waypoints.Add(Position);Sum+=Position;}
        if(!Profile.Target.Equals(Sum/Points->Num()+FVector(0,0,100),.01)){return false;}
        Profile.Id=FName(*Id);Profile.Scene=FName(*Scene);Profile.HalfHeight=Height;Profile.Priority=100;
        if(Loaded.Contains(Profile.Id)){return false;}Loaded.Add(Profile.Id,MoveTemp(Profile));
    }
    Profiles=MoveTemp(Loaded);BlendSeconds=Seconds;return true;
}
double FLinkCameraTransition::Weight() const
{
    if(From.IsEmpty()||Duration<=0){return 1;}
    const double T=FMath::Clamp(double(Elapsed)/Duration,0.,1.);return T*T*(3-2*T);
}
TMap<FName,double> FLinkCameraTransition::Weights() const
{
    TMap<FName,double> Result;const double W=Weight();
    for(const auto& Pair:From){Result.FindOrAdd(Pair.Key)+=Pair.Value*(1-W);}
    Result.FindOrAdd(Selected)+=W;return Result;
}
void FLinkCameraTransition::Step(FName Requested,float DeltaTime,float BlendSeconds,bool bSnap)
{
    if(Requested.IsNone()){Requested=TEXT("Explore");}
    if(bSnap||!bInitialized||DeltaTime<0)
    {Selected=Requested;From.Reset();Backtrack=NAME_None;Elapsed=Duration=StartFraction=0;bInitialized=true;return;}
    if(Requested!=Selected)
    {
        const bool bReversing=!From.IsEmpty()&&Requested==Backtrack;
        const float Progress=bReversing?StartFraction+(1-StartFraction)*FMath::Clamp(Elapsed/Duration,0.f,1.f):1;
        auto PreviousWeights=Weights();Backtrack=Selected;Selected=Requested;
        From=MoveTemp(PreviousWeights);Elapsed=0;Duration=FMath::Max(0.f,BlendSeconds)*Progress;
        StartFraction=bReversing?1-Progress:0;
    }
    if(!From.IsEmpty())
    {
        Elapsed+=FMath::Max(0.f,DeltaTime);
        if(Duration<=.0001f||Elapsed>=Duration){From.Reset();Elapsed=Duration=StartFraction=0;Backtrack=NAME_None;}
    }
}
FLinkCameraPose FLinkCameraTransition::Evaluate(TFunctionRef<FLinkCameraPose(FName)> PoseFor) const
{
    FLinkCameraPose Result;Result.HalfHeight=0;
    for(const auto& Pair:Weights())
    {const auto Pose=PoseFor(Pair.Key);Result.Position+=Pose.Position*Pair.Value;Result.HalfHeight+=Pose.HalfHeight*Pair.Value;}
    return Result;
}

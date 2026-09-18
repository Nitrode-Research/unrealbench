#include "Gameplay/LinkSceneLayout.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

ALinkSceneLayout::ALinkSceneLayout() { SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("SceneLayout"))); }
ALinkSceneLayout* ALinkSceneLayout::Find(const UWorld* World)
{
    if (!World) { return nullptr; }
    TActorIterator<ALinkSceneLayout> It(World);
    return It ? *It : nullptr;
}
bool ALinkSceneLayout::Load()
{
    if (bLoaded) { return bValid; } bLoaded=true;
    if (SourceScene!=TEXT("EntryGate")) { return false; }
    FString Text; TSharedPtr<FJsonObject> Data;
    if (!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Data/Scenes/EntryGate.json"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data)) { return false; }
    const TArray<TSharedPtr<FJsonValue>>* Players=nullptr; const TArray<TSharedPtr<FJsonValue>>* Centres=nullptr;
    if (!Data->TryGetArrayField(TEXT("players"),Players) || Players->Num()!=2 ||
        !Data->TryGetArrayField(TEXT("chainCentres"),Centres) || Centres->Num()!=82) { return false; }
    auto Vector=[](const TSharedPtr<FJsonValue>& Value,FVector& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
        if (!Value || !Value->TryGetArray(A) || A->Num()!=3) { return false; }
        for (int32 I=0;I<3;++I) { double N=0; if (!(*A)[I]->TryGetNumber(N) || !FMath::IsFinite(N) || FMath::Abs(N)>1000000) { return false; } Out[I]=N; }
        return true;
    };
    for (int32 I=0;I<2;++I)
    {
        const auto Player=(*Players)[I]->AsObject(); FVector Position; double Yaw=0;
        if (!Player || !Vector(Player->TryGetField(TEXT("position")),Position) || !Player->TryGetNumberField(TEXT("yaw"),Yaw) || !FMath::IsFinite(Yaw)) { return false; }
        Starts[I]=FTransform(FRotator(0,Yaw,0),Position);
    }
    for (const auto& Value:*Centres) { FVector Position; if (!Vector(Value,Position)) { ChainCentres.Reset(); return false; } ChainCentres.Add(Position); }
    bValid=true; return true;
}
bool ALinkSceneLayout::GetPlayerStart(int32 Index,FTransform& Out)
{
    if (Index<0 || Index>1 || !Load()) { return false; } Out=Starts[Index]; return true;
}
const TArray<FVector>& ALinkSceneLayout::GetChainCentres() { Load(); return ChainCentres; }

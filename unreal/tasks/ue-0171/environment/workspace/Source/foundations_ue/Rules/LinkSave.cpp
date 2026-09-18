#include "Rules/LinkSave.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "String/LexFromString.h"

namespace
{
constexpr int32 FormatVersion=1;
const FString SourceCommit=TEXT("b4ac4409ea4fdfccd2e7f741a1f32874b50e0d1b");
TSharedRef<FJsonObject> WriteFacts(const FLinkFactMap& Facts)
{
    auto Object=MakeShared<FJsonObject>();TArray<int32> Keys;Facts.GetKeys(Keys);Keys.Sort();
    for(int32 Id:Keys){Object->SetNumberField(FString::FromInt(Id),Facts[Id]);}return Object;
}
bool ReadFacts(const TSharedPtr<FJsonObject>& Object,int32 Scope,const FLinkStoryDatabase& Database,FLinkFactMap& Out)
{
    if(!Object || Object->Values.Num()>4096){return false;}
    for(const auto& Pair:Object->Values)
    {
        const FString Key(*Pair.Key);
        int32 Id=0;double Value=0;
        if(!LexTryParseString(Id,*Key)||FString::FromInt(Id)!=Key||!Pair.Value->TryGetNumber(Value)
            ||!FMath::IsFinite(Value)||Value<MIN_int32||Value>MAX_int32||Value!=FMath::FloorToDouble(Value)){return false;}
        const auto* Entry=Database.Find(Id);if(!Entry||Entry->Scope!=Scope){return false;}
        Out.Add(Id,int32(Value));
    }
    return true;
}
}
FString LinkSave::Encode(const FLinkSaveSnapshot& Snapshot)
{
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("version"),FormatVersion);Root->SetStringField(TEXT("source"),SourceCommit);
    Root->SetStringField(TEXT("map"),Snapshot.Map);Root->SetObjectField(TEXT("globals"),WriteFacts(Snapshot.Globals));
    Root->SetBoolField(TEXT("completed"),Snapshot.bCompleted);
    auto Interactions=MakeShared<FJsonObject>();TArray<FString> Keys;Snapshot.Interactions.GetKeys(Keys);Keys.Sort();
    for(const auto& Key:Keys){Interactions->SetObjectField(Key,WriteFacts(Snapshot.Interactions[Key]));}
    Root->SetObjectField(TEXT("interactions"),Interactions);
    TArray<TSharedPtr<FJsonValue>> Players;
    for(int32 Index=0;Index<2;++Index)
    {
        auto Player=MakeShared<FJsonObject>();const auto& P=Snapshot.Positions[Index];
        Player->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)});
        Player->SetNumberField(TEXT("yaw"),Snapshot.Yaws[Index]);Players.Add(MakeShared<FJsonValueObject>(Player));
    }
    Root->SetArrayField(TEXT("players"),Players);FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
}
bool LinkSave::Decode(const FString& Json,const FLinkStoryDatabase& Database,FLinkSaveSnapshot& Out,FString& Error)
{
    Error=TEXT("Save data is invalid or incompatible.");
    if(Json.Len()>1024*1024){return false;}
    TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root){return false;}
    double Version=0;FString Source;FLinkSaveSnapshot Candidate;
    if(!Root->TryGetNumberField(TEXT("version"),Version)||Version!=FormatVersion||!Root->TryGetStringField(TEXT("source"),Source)||Source!=SourceCommit
        ||!Root->TryGetStringField(TEXT("map"),Candidate.Map)){return false;}
    if(Candidate.Map!=TEXT("EntryGate")&&Candidate.Map!=TEXT("LoadingDocks")&&Candidate.Map!=TEXT("PortFixture")&&Candidate.Map!=TEXT("EntryGate_Source")){return false;}
    if(Root->HasField(TEXT("completed"))&&!Root->TryGetBoolField(TEXT("completed"),Candidate.bCompleted)){return false;}
    const TSharedPtr<FJsonObject>* Globals=nullptr;
    if(!Root->TryGetObjectField(TEXT("globals"),Globals)||!ReadFacts(*Globals,LinkFacts::GlobalScope,Database,Candidate.Globals)){return false;}
    for(int32 Id:{LinkFacts::LTItem,LinkFacts::RTItem})
    {
        const int32 Held=Candidate.Globals.FindRef(Id);const auto* Entry=Database.Find(Held);
        if(Held!=0&&(!Entry||Entry->Kind!=ELinkStoryKind::Item)){return false;}
    }
    const TSharedPtr<FJsonObject>* Interactions=nullptr;
    if(!Root->TryGetObjectField(TEXT("interactions"),Interactions)||(*Interactions)->Values.Num()>256){return false;}
    for(const auto& Pair:(*Interactions)->Values)
    {
        const FString Key(*Pair.Key);
        const TSharedPtr<FJsonObject>* Facts=nullptr;
        if(Key.IsEmpty()||Key.Len()>128||!Pair.Value->TryGetObject(Facts)
            ||!ReadFacts(*Facts,LinkFacts::InteractionScope,Database,Candidate.Interactions.FindOrAdd(Key))){return false;}
    }
    const TArray<TSharedPtr<FJsonValue>>* Players=nullptr;
    if(!Root->TryGetArrayField(TEXT("players"),Players)||Players->Num()!=2){return false;}
    for(int32 Index=0;Index<2;++Index)
    {
        const TSharedPtr<FJsonObject>* Player=nullptr;const TArray<TSharedPtr<FJsonValue>>* Position=nullptr;double Yaw=0;
        if(!(*Players)[Index]->TryGetObject(Player)||!(*Player)->TryGetArrayField(TEXT("position"),Position)||Position->Num()!=3
            ||!(*Player)->TryGetNumberField(TEXT("yaw"),Yaw)||!FMath::IsFinite(Yaw)||FMath::Abs(Yaw)>36000){return false;}
        for(int32 Axis=0;Axis<3;++Axis)
        {
            double Value=0;if(!(*Position)[Axis]->TryGetNumber(Value)||!FMath::IsFinite(Value)||FMath::Abs(Value)>1000000){return false;}
            Candidate.Positions[Index][Axis]=Value;
        }
        Candidate.Yaws[Index]=float(Yaw);
    }
    Out=MoveTemp(Candidate);Error.Reset();return true;
}

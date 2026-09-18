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
    // Restore the documented B gameplay contract.
    return MakeShared<FJsonObject>();
}
bool ReadFacts(const TSharedPtr<FJsonObject>& Object,int32 Scope,const FLinkStoryDatabase& Database,FLinkFactMap& Out)
{
    // Restore the documented B gameplay contract.
    return {};
}
}
FString LinkSave::Encode(const FLinkSaveSnapshot& Snapshot)
{
    // Restore the documented B gameplay contract.
    return {};
}
bool LinkSave::Decode(const FString& Json,const FLinkStoryDatabase& Database,FLinkSaveSnapshot& Out,FString& Error)
{
    // Restore the documented B gameplay contract.
    return {};
}

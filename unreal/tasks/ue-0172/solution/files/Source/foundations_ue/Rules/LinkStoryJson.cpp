#include "Rules/LinkStory.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
struct FStoryReader
{
    FString& Error;
    void Fail(const FString& Message) { if (Error.IsEmpty()) { Error = Message; } }
    TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonValue>& Value)
    {
        if (Value && Value->Type == EJson::Object) { return Value->AsObject(); }
        Fail(TEXT("Expected narrative JSON object")); return MakeShared<FJsonObject>();
    }
    FString String(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        FString Value;
        if (!Object->TryGetStringField(Field, Value)) { Fail(FString::Printf(TEXT("Missing/invalid narrative string: %s"), Field)); }
        return Value;
    }
    int32 IntegerValue(const TSharedPtr<FJsonValue>& Value)
    {
        if (!Value || Value->Type != EJson::Number) { Fail(TEXT("Expected narrative integer")); return 0; }
        const double Number = Value->AsNumber();
        if (!FMath::IsFinite(Number) || Number < MIN_int32 || Number > MAX_int32 || FMath::FloorToDouble(Number) != Number)
        {
            Fail(TEXT("Narrative integer is outside signed 32-bit range")); return 0;
        }
        return int32(Number);
    }
    int32 Integer(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        const auto* Value = Object->Values.Find(Field);
        return IntegerValue(Value ? *Value : nullptr);
    }
    bool Boolean(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        bool Value = false;
        if (!Object->TryGetBoolField(Field, Value)) { Fail(FString::Printf(TEXT("Missing/invalid narrative boolean: %s"), Field)); }
        return Value;
    }
    TArray<TSharedPtr<FJsonValue>> Array(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        const TArray<TSharedPtr<FJsonValue>>* Value = nullptr;
        if (!Object->TryGetArrayField(Field, Value)) { Fail(FString::Printf(TEXT("Missing/invalid narrative array: %s"), Field)); return {}; }
        return *Value;
    }
    TArray<int32> Integers(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        TArray<int32> Result;
        for (const auto& Value : Array(Object, Field)) { Result.Add(IntegerValue(Value)); }
        return Result;
    }
};

bool KnownLegacyReference(const FLinkStoryEntry& Entry, int32 Reference)
{
    return Entry.Table == TEXT("SampleScene") && Reference == -2028872245
        && (Entry.Id == -1843988935 || Entry.Id == -1843988949);
}
}

bool FLinkStoryDatabase::LoadFile(const FString& Path, FString& Error)
{
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *Path)) { Error = FString::Printf(TEXT("Cannot read narrative: %s"), *Path); return false; }
    return LoadJson(Json, Error);
}

bool FLinkStoryDatabase::LoadJson(const FString& Json, FString& Error)
{
    Error.Reset();
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root)
    {
        Error = TEXT("Narrative JSON is malformed"); return false;
    }
    FStoryReader Read{Error};
    if (Read.Integer(Root, TEXT("schemaVersion")) != 1) { Read.Fail(TEXT("Unsupported narrative schema version")); }
    const auto* SourceValue = Root->Values.Find(TEXT("source"));
    const auto Source = Read.Object(SourceValue ? *SourceValue : nullptr);
    if (Read.String(Source, TEXT("revision")) != TEXT("b4ac4409ea4fdfccd2e7f741a1f32874b50e0d1b"))
    {
        Read.Fail(TEXT("Narrative revision differs from the pinned source"));
    }
    const TMap<FString, ELinkStoryKind> Kinds{
        {TEXT("RuleEntry"),ELinkStoryKind::Rule}, {TEXT("DialogueEntry"),ELinkStoryKind::Dialogue},
        {TEXT("ChoiceEntry"),ELinkStoryKind::Choice}, {TEXT("EventEntry"),ELinkStoryKind::Event},
        {TEXT("FactEntry"),ELinkStoryKind::Fact}, {TEXT("ItemEntry"),ELinkStoryKind::Item}, {TEXT("ScopeEntry"),ELinkStoryKind::Scope}};
    TArray<FLinkStoryEntry> Parsed;
    for (const auto& Value : Read.Array(Root, TEXT("entries")))
    {
        const auto Data = Read.Object(Value);
        FLinkStoryEntry Entry;
        Entry.Id = Read.Integer(Data,TEXT("id"));
        Entry.Key = Read.String(Data,TEXT("key"));
        Entry.Table = Read.String(Data,TEXT("table"));
        const auto* Kind = Kinds.Find(Read.String(Data,TEXT("kind")));
        if (!Kind) { Read.Fail(TEXT("Unknown narrative entry kind")); } else { Entry.Kind = *Kind; }
        Entry.Scope = Read.Integer(Data,TEXT("scope"));
        Entry.Padding = Read.Integer(Data,TEXT("padding"));
        Entry.bOnce = Read.Boolean(Data,TEXT("once"));
        Entry.Triggers = Read.Integers(Data,TEXT("triggers"));
        Entry.OnApply = Read.Integers(Data,TEXT("onApply"));
        Entry.OnInvoke = Read.Integers(Data,TEXT("onInvoke"));
        for (const auto& Criterion : Read.Array(Data,TEXT("criteria")))
        {
            const auto Item = Read.Object(Criterion);
            Entry.Criteria.Add({Read.Integer(Item,TEXT("fact")),Read.Integer(Item,TEXT("min")),Read.Integer(Item,TEXT("max"))});
        }
        for (const auto& Modification : Read.Array(Data,TEXT("modifications")))
        {
            const auto Item = Read.Object(Modification);
            FLinkModification Mod{Read.Integer(Item,TEXT("fact")),Read.Integer(Item,TEXT("value")),Read.Integer(Item,TEXT("operation"))};
            if (Mod.Operation < 0 || Mod.Operation > 2) { Read.Fail(TEXT("Unknown narrative modification operation")); }
            Entry.Modifications.Add(Mod);
        }
        if (Entry.Kind == ELinkStoryKind::Dialogue)
        {
            Entry.Text = Read.String(Data,TEXT("text")); Entry.Speaker = Read.Integer(Data,TEXT("speaker"));
            Entry.Style = Read.Integer(Data,TEXT("style")); Entry.Icon = Read.Integer(Data,TEXT("icon"));
        }
        if (Entry.Kind == ELinkStoryKind::Choice) { Entry.bCancellable = Read.Boolean(Data,TEXT("cancellable")); }
        Parsed.Add(MoveTemp(Entry));
    }
    if (Parsed.IsEmpty()) { Read.Fail(TEXT("Narrative contains no entries")); }
    if (!Error.IsEmpty()) { return false; }
    FLinkStoryDatabase Candidate;
    if (!Candidate.Initialize(MoveTemp(Parsed), Error)) { return false; }
    for (const auto& Entry : Candidate.All())
    {
        TArray<int32> References = Entry.Triggers;
        References.Append(Entry.OnApply); References.Append(Entry.OnInvoke);
        References.Add(Entry.Scope); References.Add(Entry.Speaker);
        for (const auto& Criterion : Entry.Criteria) { References.Add(Criterion.Fact); }
        for (const auto& Modification : Entry.Modifications) { References.Add(Modification.Fact); }
        for (int32 Reference : References)
        {
            if (Reference != 0 && !Candidate.Find(Reference) && !KnownLegacyReference(Entry,Reference))
            {
                Error = FString::Printf(TEXT("Entry %d references missing ID %d"),Entry.Id,Reference); return false;
            }
        }
    }
    const auto* RelationValue = Root->Values.Find(TEXT("relations"));
    const auto RelationsJson = Read.Object(RelationValue ? *RelationValue : nullptr);
    if (RelationsJson->Values.Num() != Candidate.Relations.Num()) { Read.Fail(TEXT("Narrative relation count mismatch")); }
    for (const auto& Pair : RelationsJson->Values)
    {
        int32 Trigger = 0;
        if (!LexTryParseString(Trigger, *Pair.Key)) { Read.Fail(TEXT("Invalid trigger ID")); continue; }
        if (Read.Integers(RelationsJson,*Pair.Key) != Candidate.Related(Trigger)) { Read.Fail(TEXT("Native rule ordering differs from exported source ordering")); }
    }
    if (!Error.IsEmpty()) { return false; }
    *this = MoveTemp(Candidate);
    return true;
}

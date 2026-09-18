#include "Misc/AutomationTest.h"
#include "Rules/LinkStory.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkStoryContent,
    "Task0172.Headless.Story.PinnedContentAndUnityOracle", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkStoryContent::RunTest(const FString& Parameters)
{
    FLinkStoryDatabase Database; FString Error;
    if (!TestTrue(TEXT("Pinned runtime content loads: ") + Error, Database.LoadFile(FPaths::ProjectContentDir()/TEXT("Data/Narrative.json"),Error)))
    {
        AddError(Error); return false;
    }
    TestEqual(TEXT("Every source entry is present"),Database.All().Num(),812);
    int32 PlayableDialogue = 0;
    for (const auto& Entry : Database.All())
    {
        if (Entry.Kind==ELinkStoryKind::Dialogue && (Entry.Table==TEXT("EntryGate") || Entry.Table==TEXT("LoadingDocs"))) { ++PlayableDialogue; }
    }
    TestEqual(TEXT("Both playable scenes retain all authored dialogue"),PlayableDialogue,712);
    TestNull(TEXT("Duplicate cross-scene key requires explicit table"),Database.FindByKey(TEXT("E_rolling_gate")));
    const auto* Gate = Database.FindByKey(TEXT("E_rolling_gate"),TEXT("EntryGate"));
    TestTrue(TEXT("Table-qualified key resolves the correct gate"),Gate && Gate->Id==1389383);
    FString OracleJson; TSharedPtr<FJsonObject> Oracle;
    if (!TestTrue(TEXT("Independent Unity oracle is readable"),FFileHelper::LoadFileToString(OracleJson,*(FPaths::ProjectDir()/TEXT("Docs/Fixtures/UnityRelations.json"))))
        || !TestTrue(TEXT("Independent Unity oracle parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OracleJson),Oracle))) { return false; }
    const auto& Relations = Oracle->GetArrayField(TEXT("relations"));
    TestEqual(TEXT("Oracle includes all 662 trigger lists"),Relations.Num(),662);
    for (const auto& Relation : Relations)
    {
        const auto Object=Relation->AsObject(); const int32 Trigger=Object->GetIntegerField(TEXT("trigger"));
        TArray<int32> Expected;
        for (const auto& Value : Object->GetArrayField(TEXT("entries"))) { Expected.Add(int32(Value->AsNumber())); }
        TestTrue(FString::Printf(TEXT("Source trigger %d has identical native ordering"),Trigger),Database.Related(Trigger)==Expected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLinkStoryIntro,
    "Task0172.Headless.Story.AuthoredIntroPresenceAndOnce", EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FLinkStoryIntro::RunTest(const FString& Parameters)
{
    FLinkStoryDatabase Database; FString Error;
    if (!Database.LoadFile(FPaths::ProjectContentDir()/TEXT("Data/Narrative.json"),Error)) { AddError(Error); return false; }
    FLinkFactMap Global,Interaction; FLinkStoryContext Context{&Global,&Interaction}; FLinkStoryEngine Engine(Database);
    TArray<int32> Emitted; Engine.OnInvoked=[&](int32 Id,FLinkStoryContext&) { Emitted.Add(Id); };
    TestFalse(TEXT("Source intro waits for RT presence"),Engine.Process(1389444,Context));
    Engine.Set(LinkFacts::IsRTPresent,1,Context); Engine.Set(LinkFacts::IsLTPresent,1,Context);
    TestTrue(TEXT("Source intro starts when RT is present"),Engine.Process(1389444,Context));
    if (!TestEqual(TEXT("Exactly one authored opening line is invoked"),Emitted.Num(),1)) { return false; }
    TestEqual(TEXT("Opening is LR_intro_1"),Emitted[0],1389462);
    Engine.Apply(1389462,Context);
    TestFalse(TEXT("Applied intro opening cannot repeat"),Engine.Process(1389444,Context));
    TestFalse(TEXT("Legacy sample missing-fact condition stays false"),Engine.Test(-1843988935,Context));
    const int32 Before=Database.All().Num();
    TestFalse(TEXT("Malformed replacement data is rejected"),Database.LoadJson(TEXT("{}"),Error));
    TestEqual(TEXT("Rejected reload preserves valid data"),Database.All().Num(),Before);
    return true;
}
#endif

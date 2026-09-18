#include "ModifierArenaProbe.h"
#include "ModifierFixture.h"
#include "NightSkyEngine/UI/ModifierSetupWidget.h"
#include "NightSkyEngine/UI/NightSkyModifierWidget.h"
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "NightSkyEngine/Miscellaneous/ReplayInfo.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
void UModifierArenaProbe::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (FParse::Value(FCommandLine::Get(), TEXT("ModifierArenaProbe="), Directory))
	{
		IFileManager::Get().MakeDirectory(*Directory, true);
		Deadline = FPlatformTime::Seconds() + 90;
	}
}
void UModifierArenaProbe::Check(bool Value, const FString& Message)
{
	++Assertions;
	if (!Value)
		Errors.Add(Message);
}
void UModifierArenaProbe::Finish()
{
	Finished = true;
	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("passed"), Errors.IsEmpty());
	Result->SetNumberField(TEXT("assertions"), Assertions);
	Result->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
	Result->SetStringField(TEXT("map"),
	                       GetWorld() ? GetWorld()->GetOutermost()->GetName() : TEXT("missing"));
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const auto& Error : Errors)
		Values.Add(MakeShared<FJsonValueString>(Error));
	Result->SetArrayField(TEXT("errors"), Values);
	FString Json;
	FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
	FFileHelper::SaveStringToFile(Json, *(Directory / TEXT("result.json")));
	FPlatformMisc::RequestExit(false);
}
void UModifierArenaProbe::Tick(float)
{
	if (FPlatformTime::Seconds() > Deadline)
	{
		Check(false, TEXT("Native arena setup/play exceeded 90-second infrastructure deadline"));
		Finish();
		return;
	}
	auto* World = GetWorld();
	if (!World || !World->HasBegunPlay())
		return;
	auto* Battle = World->GetGameState<ANightSkyGameState>();
	auto* GI = Cast<UModifierFixtureGameInstance>(GetGameInstance());
	auto* PC = Cast<ANightSkyPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
	if (!Battle || !GI || !PC || Battle->Players.Num() != 2)
		return;
	if (!Started)
	{
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets,
		                                              UModifierSetupWidget::StaticClass(), true);
		if (Widgets.IsEmpty())
			return;
		auto* Setup = Cast<UModifierSetupWidget>(Widgets[0]);
		if (Setup->GetSetupSummary().IsEmpty())
			return;
		Check(World->GetOutermost()->GetName() == TEXT("/Game/ModifierFixture/ModifierArena"),
		      TEXT("Loaded the saved authored arena"));
		Check(Setup->GetSetupSummary().ToString().Contains(TEXT("Conversion")),
		      TEXT("Real setup presents the four-rule preset"));
		Check(Battle->GetPaused(), TEXT("Setup pauses ordinary local simulation"));
		Check(Setup->StartSelectedMatch(), TEXT("Player Start control accepts preset"));
		Check(!Setup->IsInViewport() && !Battle->GetPaused(),
		      TEXT("Start closes setup and resumes local runner"));
		InitialX = Battle->GetMainPlayer(true)->PosX;
		InitialY = Battle->GetMainPlayer(true)->PosY;
		Battle->GetMainPlayer(true)->AddMeter(20);
		PC->PressRight();
		PC->PressUp();
		PC->PressA();
		Started = true;
		return;
	}
	auto* A = Battle->GetMainPlayer(true);
	auto* B = Battle->GetMainPlayer(false);
	SawContact |= A->ComboCounter > 0 || B->Hitstop > 0;
	if (Battle->GetPlayableRoundFrame() >= 10 && !CheckedEarly)
	{
		CheckedEarly = true;
		Check(Battle->GetActiveModifiers().Num() == 4,
		      TEXT("All four selected rules activate in ordinary local play"));
		Check(Battle->BattleState.Meter[0] == 0,
		      TEXT("Configured drain consumes the publicly earned 20 meter"));
		Check(A->PosX == InitialX && A->PosY == InitialY,
		      TEXT("New walking and jumping remain blocked in real controller play"));
		Check(B->CurrentHealth == 1000,
		      TEXT("Conversion preserves health during actual early contacts"));
		auto* HUD = Battle->BattleHudActor->ModifierWidget;
		Check(HUD && HUD->IsInViewport(), TEXT("Actual game viewport contains rule HUD"));
		if (HUD)
		{
			HUD->RefreshRules();
			Check(HUD->GetRenderedRuleText().ToString().Contains(TEXT("Conversion")),
			      TEXT("Actual HUD displays conversion and duration"));
			FFileHelper::SaveStringToFile(HUD->GetRenderedRuleText().ToString(),
			                              *(Directory / TEXT("early-hud.txt")));
		}
	}
	if (Battle->GetCurrentRoundResult() != WIN_None)
	{
		Check(CheckedEarly && SawContact, TEXT("Observed early modified combat before result"));
		Check(Battle->GetCurrentRoundResult() == WIN_P1,
		      TEXT("Health contact after conversion expiry awards sudden death"));
		Check(B->CurrentHealth > 0 && B->CurrentHealth < 1000,
		      TEXT("Victory follows nonlethal real health damage"));
		Check(Battle->GetActiveModifiers().IsEmpty(),
		      TEXT("Round result cleans active rule display"));
		Check(GI->GetRecordedReplay() && GI->GetRecordedReplay()->LengthInFrames >= 120,
		      TEXT("Ordinary runner records full modified input tape"));
		Check(GI->SaveRecordedReplay(TEXT("ModifierArenaProbe_") +
		                             FString::FromInt(FPlatformProcess::GetCurrentProcessId())),
		      TEXT("Saved native arena replay"));
		Finish();
	}
}

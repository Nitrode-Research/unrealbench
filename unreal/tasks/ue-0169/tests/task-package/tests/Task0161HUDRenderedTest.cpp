// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Serialization/BufferArchive.h"
#include "Slate/WidgetRenderer.h"
#include "UI/RTSHUDSnapshot.h"
#include "UI/SRTSStatusOverlay.h"
#include "Units/RTSTeams.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SOverlay.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
struct FHUDRenderFixture
{
	const TCHAR* Name = TEXT("");
	FRTSHUDSnapshot Snapshot;
};

FRTSHUDSnapshot BaseSnapshot()
{
	FRTSHUDSnapshot Snapshot;
	Snapshot.bAvailable = true;
	Snapshot.Economy.TeamId = RTSTeams::Player;
	Snapshot.Economy.Materials = 1200;
	Snapshot.Economy.PowerGeneration = 220;
	Snapshot.Economy.PowerDemand = 180;
	Snapshot.Economy.SupplyUsed = 21;
	Snapshot.Economy.SupplyReserved = 3;
	Snapshot.Economy.SupplyCapacity = 40;
	Snapshot.MaterialsText = TEXT("MATERIALS  1200");
	Snapshot.PowerText = TEXT("POWER  220 GENERATED / 180 REQUIRED");
	Snapshot.SupplyText = TEXT("SUPPLY  21 USED + 3 QUEUED / 40 CAP");
	Snapshot.SelectionTitle = TEXT("NO SELECTION");
	Snapshot.AvailableActionsText = TEXT("DRAG TO SELECT UNITS OR STRUCTURES");
	return Snapshot;
}

TArray<FHUDRenderFixture> MakeRenderFixtures()
{
	TArray<FHUDRenderFixture> Fixtures;
	Fixtures.Add({TEXT("NoSelection"), BaseSnapshot()});

	FRTSHUDSnapshot Unit = BaseSnapshot();
	Unit.SelectionMode = ERTSHUDSelectionMode::SingleUnit;
	Unit.SelectedUnitType = ERTSUnitType::InfantrySquad;
	Unit.SelectedHealth.CurrentHealth = 72.0f;
	Unit.SelectedHealth.MaximumHealth = 100.0f;
	Unit.SelectionTitle = TEXT("INFANTRY  U14     HP 72%");
	Unit.SelectionDetail = TEXT("RECLAIMING     W8 VALUE 125     PROGRESS 64%");
	Unit.AvailableActionsText = TEXT("AVAILABLE     MOVE     ATTACK     RECLAIM");
	Unit.Actions = {
		{ERTSHUDActionKind::Move, TEXT("RMB"), TEXT("MOVE")},
		{ERTSHUDActionKind::Attack, TEXT("RMB"), TEXT("ATTACK")},
		{ERTSHUDActionKind::Reclaim, TEXT("RMB"), TEXT("RECLAIM")}};
	Fixtures.Add({TEXT("SingleUnit"), Unit});

	FRTSHUDSnapshot Structure = BaseSnapshot();
	Structure.SelectionMode = ERTSHUDSelectionMode::SingleStructure;
	Structure.SelectedStructureType = ERTSStructureType::DefensiveTurret;
	Structure.SelectedHealth.CurrentHealth = 84.0f;
	Structure.SelectedHealth.MaximumHealth = 100.0f;
	Structure.SelectionTitle = TEXT("DEFENSIVE TURRET  S12     HP 84%");
	Structure.SelectionDetail = TEXT("FIRING     TARGET U106");
	Structure.AvailableActionsText = TEXT("AUTOMATIC DEFENSE");
	Fixtures.Add({TEXT("SingleStructure"), Structure});

	FRTSHUDSnapshot UnpoweredTurret = Structure;
	UnpoweredTurret.SelectionDetail = TEXT("UNPOWERED");
	Fixtures.Add({TEXT("UnpoweredTurret"), UnpoweredTurret});

	FRTSHUDSnapshot Mixed = BaseSnapshot();
	Mixed.SelectionMode = ERTSHUDSelectionMode::Multiple;
	Mixed.SelectedUnitCount = 12;
	Mixed.SelectedStructureCount = 2;
	Mixed.SelectionTitle = TEXT("SELECTION     12 UNITS     2 STRUCTURES");
	Mixed.SelectionDetail = TEXT("IDLE 3     MOVING 4     ATTACKING 4     RECLAIMING 1");
	Mixed.AvailableActionsText = TEXT("AVAILABLE     MOVE     ATTACK     RECLAIM FILTERS TO INFANTRY");
	Mixed.Actions = Unit.Actions;
	Fixtures.Add({TEXT("MixedSelection"), Mixed});

	FRTSHUDSnapshot Queue = BaseSnapshot();
	Queue.SelectionMode = ERTSHUDSelectionMode::SingleStructure;
	Queue.SelectedStructureType = ERTSStructureType::Factory;
	Queue.SelectedHealth.CurrentHealth = 100.0f;
	Queue.SelectedHealth.MaximumHealth = 100.0f;
	Queue.SelectionTitle = TEXT("FACTORY  S7     HP 100%");
	Queue.SelectionDetail = TEXT("PRODUCING");
	Queue.QueueText = TEXT("QUEUE 5 / 5\n1 INFANTRY 16%    2 LIGHT VEHICLE 0%    3 HEAVY VEHICLE 0%\n4 INFANTRY 0%    5 HEAVY VEHICLE 0%");
	Queue.AvailableActionsText = TEXT("PRODUCE     [Z] INFANTRY     [X] LIGHT     [C] HEAVY");
	Queue.Actions = {
		{ERTSHUDActionKind::ProduceInfantry, TEXT("Z"), TEXT("INFANTRY")},
		{ERTSHUDActionKind::ProduceLightVehicle, TEXT("X"), TEXT("LIGHT")},
		{ERTSHUDActionKind::ProduceHeavyVehicle, TEXT("C"), TEXT("HEAVY")}};
	Fixtures.Add({TEXT("FullQueue"), Queue});

	FRTSHUDSnapshot Construction = BaseSnapshot();
	Construction.SelectionMode = ERTSHUDSelectionMode::SingleStructure;
	Construction.SelectedStructureType = ERTSStructureType::Factory;
	Construction.SelectedHealth.CurrentHealth = 100.0f;
	Construction.SelectedHealth.MaximumHealth = 100.0f;
	Construction.SelectionTitle = TEXT("FACTORY  S18     HP 100%");
	Construction.SelectionDetail = TEXT("CONSTRUCTION     46%");
	Construction.AvailableActionsText = TEXT("PRODUCTION LOCKED UNTIL CONSTRUCTION COMPLETES");
	Fixtures.Add({TEXT("Construction"), Construction});

	FRTSHUDSnapshot PowerWarning = Queue;
	PowerWarning.Economy.PowerDemand = 240;
	PowerWarning.PowerText = TEXT("POWER  220 GENERATED / 240 REQUIRED");
	PowerWarning.WarningText = TEXT("LOW POWER     ADD 20 GENERATION     FACTORIES AND TURRETS PAUSED");
	PowerWarning.WarningTone = ERTSHUDMessageTone::Critical;
	PowerWarning.SelectionDetail = TEXT("PAUSED - LOW POWER");
	Fixtures.Add({TEXT("PowerWarning"), PowerWarning});

	FRTSHUDSnapshot SupplyWarning = BaseSnapshot();
	SupplyWarning.Economy.SupplyUsed = 37;
	SupplyWarning.SupplyText = TEXT("SUPPLY  37 USED + 3 QUEUED / 40 CAP");
	SupplyWarning.WarningText = TEXT("SUPPLY CAP REACHED     BUILD A SUPPLY DEPOT");
	SupplyWarning.WarningTone = ERTSHUDMessageTone::Warning;
	Fixtures.Add({TEXT("SupplyWarning"), SupplyWarning});

	FRTSHUDSnapshot PlacementWarning = BaseSnapshot();
	PlacementWarning.ContextText = TEXT("PLACEMENT REFUSED     INSUFFICIENT MATERIALS");
	PlacementWarning.ContextTone = ERTSHUDMessageTone::Critical;
	Fixtures.Add({TEXT("PlacementWarning"), PlacementWarning});

	FRTSHUDSnapshot ReclaimWarning = Unit;
	ReclaimWarning.SelectionDetail = TEXT("IDLE");
	ReclaimWarning.WarningText = TEXT("WRECKAGE ALREADY CLAIMED");
	ReclaimWarning.WarningTone = ERTSHUDMessageTone::Warning;
	Fixtures.Add({TEXT("ReclaimWarning"), ReclaimWarning});

	FRTSHUDSnapshot Victory = BaseSnapshot();
	Victory.Match.State = ERTSMatchState::Resolved;
	Victory.bPlayerVictory = true;
	Victory.ResultTitle = TEXT("VICTORY");
	Victory.ResultReason = TEXT("ENEMY HEADQUARTERS DESTROYED");
	Victory.ResultDuration = TEXT("MATCH TIME  09:42");
	Victory.PlayerResultStatistics = TEXT("YOU     PRODUCED 24     LOST 17     RECLAIMED 1125 MATERIALS");
	Victory.EnemyResultStatistics = TEXT("ENEMY     PRODUCED 22     LOST 24     RECLAIMED 875 MATERIALS");
	Fixtures.Add({TEXT("Victory"), Victory});

	FRTSHUDSnapshot Defeat = Victory;
	Defeat.bPlayerVictory = false;
	Defeat.ResultTitle = TEXT("DEFEAT");
	Defeat.ResultReason = TEXT("YOUR HEADQUARTERS DESTROYED");
	Fixtures.Add({TEXT("Defeat"), Defeat});
	return Fixtures;
}

bool SaveWidgetCapture(
	FAutomationTestBase& Test,
	FWidgetRenderer& Renderer,
	UTextureRenderTarget2D& RenderTarget,
	const FIntPoint Resolution,
	const FHUDRenderFixture& Fixture,
	TArray<FColor>& BaselinePixels)
{
	const TSharedRef<SRTSStatusOverlay> HUD = SNew(SRTSStatusOverlay).Snapshot(Fixture.Snapshot);
	const TSharedRef<SWidget> Surface =
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SColorBlock)
			.Color(FLinearColor(0.13f, 0.15f, 0.17f, 1.0f))
		]
		+ SOverlay::Slot()
		[
			HUD
		];
	const float DPIScale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Resolution);
	Renderer.DrawWidget(
		&RenderTarget,
		Surface,
		DPIScale,
		FVector2D(Resolution.X, Resolution.Y),
		0.0f,
		false);
	FlushRenderingCommands();

	TArray<FColor> Pixels;
	if (!RenderTarget.GameThread_GetRenderTargetResource()->ReadPixels(Pixels)
		|| Pixels.Num() != Resolution.X * Resolution.Y)
	{
		Test.AddError(TEXT("Real HUD render-target readback failed."));
		return false;
	}
	int32 NonBackground = 0;
	for (const FColor Pixel : Pixels)
	{
		if (Pixel != Pixels[0]) ++NonBackground;
	}
	Test.TestTrue(TEXT("The HUD draws visible content, not an empty surface"), NonBackground > 500);
	if (BaselinePixels.IsEmpty()) BaselinePixels = Pixels;
	else
	{
		int32 Changed = 0;
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
			if (Pixels[Index] != BaselinePixels[Index]) ++Changed;
		Test.TestTrue(FString::Printf(TEXT("%s changes rendered pixels from no-selection state"), Fixture.Name), Changed > 30);
	}

	FBufferArchive PNGData;
	if (!FImageUtils::ExportRenderTarget2DAsPNG(&RenderTarget, PNGData))
	{
		Test.AddError(FString::Printf(TEXT("Could not encode the %s HUD fixture."), Fixture.Name));
		return false;
	}
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/RTS/M3Slice4"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(
		Directory,
		FString::Printf(TEXT("%dx%d-%s.png"), Resolution.X, Resolution.Y, Fixture.Name));
	if (!FFileHelper::SaveArrayToFile(PNGData, *Path))
	{
		Test.AddError(FString::Printf(TEXT("Could not save the HUD capture at %s."), *Path));
		return false;
	}
	Test.AddInfo(FString::Printf(TEXT("HUD capture: %s"), *Path));
	return IFileManager::Get().FileSize(*Path) > 0;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3HUDRenderedTest,
	"Task0169.Rendered.Task0161.HUD.RenderedMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone3HUDRenderedTest::RunTest(const FString& Parameters)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
	{
		AddError(TEXT("This task requires real RHI; NullRHI is not valid rendered evidence."));
		return false;
	}

	const FString CaptureDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Screenshots/RTS/M3Slice4"));
	if (IFileManager::Get().DirectoryExists(*CaptureDirectory)
		&& !IFileManager::Get().DeleteDirectory(*CaptureDirectory, false, true))
	{
		AddError(FString::Printf(TEXT("Could not clear stale HUD captures at %s."), *CaptureDirectory));
		return false;
	}

	const FIntPoint Resolutions[] = {
		FIntPoint(1280, 720),
		FIntPoint(1920, 1080),
		FIntPoint(3840, 2160)};
	const TArray<FHUDRenderFixture> Fixtures = MakeRenderFixtures();
	FWidgetRenderer Renderer(true, true);
	for (const FIntPoint Resolution : Resolutions)
	{
		UTextureRenderTarget2D* RenderTarget = FWidgetRenderer::CreateTargetFor(
			FVector2D(Resolution.X, Resolution.Y),
			TF_Bilinear,
			true);
		if (!TestNotNull(TEXT("The exact-size HUD render target is created"), RenderTarget))
		{
			return false;
		}
		TestEqual(TEXT("The HUD render target preserves requested width"), RenderTarget->SizeX, Resolution.X);
		TestEqual(TEXT("The HUD render target preserves requested height"), RenderTarget->SizeY, Resolution.Y);
		TArray<FColor> BaselinePixels;
		for (const FHUDRenderFixture& Fixture : Fixtures)
		{
			if (!SaveWidgetCapture(*this, Renderer, *RenderTarget, Resolution, Fixture, BaselinePixels))
			{
				return false;
			}
		}
		RenderTarget->MarkAsGarbage();
	}
	return true;
}

#endif

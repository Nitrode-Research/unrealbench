#include "ModifierScenarioWorker.h"
#include "ModifierCapture.h"
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyModifierWidget.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "NightSkyEngine/UI/ModifierSetupWidget.h"
#include "TimerManager.h"
#include "NightSkyEngine/Network/NetworkPawn.h"
#include "NightSkyEngine/Battle/FighterRunners/FighterMultiplayerRunner.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Kismet/GameplayStatics.h"

void UModifierWorkerGameInstance::Init()
{
	UGameInstance::Init();
	FString Role;
	const bool Peer = FParse::Value(FCommandLine::Get(), TEXT("ModifierPeerRole="), Role);
	if (!Peer && !FParse::Param(FCommandLine::Get(), TEXT("ModifierSample")))
		return;
	auto* Data = GetMutableDefault<UModifierFixtureCharaData>();
	BattleData.PlayerListP1 = {Data};
	BattleData.PlayerListP2 = {Data};
	BattleData.RoundCount = 1;
	BattleData.Random = FRandomManager(41041);
	BattleData.BattleFormat = EBattleFormat::Rounds;
	BattleData.TimeUntilRoundStart = 0;
	BattleData.StartRoundTimer = 999;
	FModifierConfiguration Configuration;
	for (FString Identifier : {FString(TEXT("Drain")), FString(TEXT("Conversion")),
	                           FString(TEXT("Restriction")), FString(TEXT("SuddenDeath"))})
	{
		FModifierDefinition Definition;
		Definition.Identifier = Identifier;
		Definition.DisplayName = Identifier;
		FModifierInterval Interval;
		Interval.Identifier = Identifier;
		if (Identifier == TEXT("Drain"))
		{
			Definition.Drain = 1;
			Interval.Duration = 180;
		}
		if (Identifier == TEXT("Conversion"))
		{
			Definition.ConvertDamage = true;
			Interval.Duration = 90;
			FModifierOperation Operation;
			Operation.Operation = EModifierOperation::ChildMeter;
			Operation.Amount = 7;
			Operation.ToAttacker = true;
			Definition.Operations.Add(Operation);
		}
		if (Identifier == TEXT("Restriction"))
		{
			Definition.RestrictMovement = true;
			Definition.OwnedAttack = State_ModifierFixture_ProjectileA;
			Interval.Start = 60;
			Interval.Duration = 20;
		}
		if (Identifier == TEXT("SuddenDeath"))
		{
			Definition.SuddenDeath = true;
			Interval.Start = 200;
			Interval.Duration = 40;
		}
		Configuration.Definitions.Add(Definition);
		Configuration.Schedule.Add(Interval);
	}
	FModifierInterval Again;
	Again.Identifier = TEXT("Restriction");
	Again.Start = 100;
	Again.Duration = 40;
	Configuration.Schedule.Add(Again);
	if (FParse::Param(FCommandLine::Get(), TEXT("ModifierMismatch")))
	{
		Configuration.Definitions[0].Revision = 2;
		Configuration.Schedule[0].Revision = 2;
	}
	int32 EpisodeSeed = 0;
	FParse::Value(FCommandLine::Get(), TEXT("ModifierEpisodeSeed="), EpisodeSeed);
	if (Peer && EpisodeSeed > 0)
	{
		BattleData.Random = FRandomManager(EpisodeSeed);
		FModifierDefinition Scale;
		Scale.Identifier = TEXT("HeldoutScale");
		Scale.DisplayName = TEXT("Held-out damage scale");
		Scale.Priority = EpisodeSeed % 3 - 1;
		FModifierOperation Add;
		Add.Operation = EModifierOperation::Add;
		Add.Amount = EpisodeSeed % 4 - 1;
		Scale.Operations.Add(Add);
		FModifierOperation Multiply;
		Multiply.Operation = EModifierOperation::Multiply;
		Multiply.Amount = 3;
		Multiply.Denominator = 2;
		Scale.Operations.Add(Multiply);
		FModifierDefinition Nested;
		Nested.Identifier = TEXT("HeldoutNested");
		Nested.DisplayName = TEXT("Held-out nested meter");
		Nested.Subscription = EModifierEvent::Meter;
		FModifierOperation MeterScale;
		MeterScale.Operation = EModifierOperation::Multiply;
		MeterScale.Amount = 3;
		MeterScale.Denominator = 2;
		Nested.Operations.Add(MeterScale);
		FModifierOperation Child;
		Child.Operation = EModifierOperation::ChildDamage;
		Child.Amount = EpisodeSeed % 2;
		Nested.Operations.Add(Child);
		Configuration.Definitions.Add(Scale);
		Configuration.Definitions.Add(Nested);
		for (FString Identifier : {Scale.Identifier, Nested.Identifier})
		{
			FModifierInterval Interval;
			Interval.Identifier = Identifier;
			Interval.Duration = 240;
			Configuration.Schedule.Add(Interval);
		}
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("ModifierLifecycle")))
	{
		Configuration = {};
		for (FString Identifier :
		     {FString(TEXT("Drain")), FString(TEXT("Conversion")), FString(TEXT("Restriction")),
		      FString(TEXT("SuddenDeath")), FString(TEXT("Shots"))})
		{
			FModifierDefinition Definition;
			Definition.Identifier = Identifier;
			Definition.DisplayName = Identifier;
			if (Identifier == TEXT("Drain"))
				Definition.Drain = 1;
			if (Identifier == TEXT("Conversion"))
				Definition.ConvertDamage = true;
			if (Identifier == TEXT("Restriction"))
				Definition.RestrictMovement = true;
			if (Identifier == TEXT("SuddenDeath"))
				Definition.SuddenDeath = true;
			if (Identifier == TEXT("Shots"))
				Definition.OwnedAttack = State_ModifierFixture_ProjectileA;
			Configuration.Definitions.Add(Definition);
			for (int32 Round = 1; Round <= 2; ++Round)
			{
				if (Identifier == TEXT("Shots") && Round == 2)
					continue;
				FModifierInterval Interval;
				Interval.Identifier = Identifier;
				Interval.Round = Round;
				Interval.Duration = Round == 1 ? 4 : 2;
				if (Identifier == TEXT("Drain"))
					Interval.Duration = 4;
				if (Identifier == TEXT("Shots"))
					Interval.Duration = 20;
				if (Identifier == TEXT("SuddenDeath"))
				{
					Interval.Start = Round == 1 ? 8 : 0;
					Interval.Duration = Round == 1 ? 1 : 40;
				}
				Configuration.Schedule.Add(Interval);
			}
		}
		FModifierDefinition Parent;
		Parent.Identifier = TEXT("NestedParent");
		Parent.DisplayName = TEXT("Nested parent");
		FModifierOperation Child;
		Child.Operation = EModifierOperation::ChildMeter;
		Child.Amount = 20;
		Parent.Operations.Add(Child);
		FModifierDefinition Grand;
		Grand.Identifier = TEXT("NestedGrandchild");
		Grand.DisplayName = TEXT("Nested grandchild");
		Grand.Subscription = EModifierEvent::Meter;
		Grand.MatchAmount = true;
		Grand.RequiredAmount = 20;
		Child.Operation = EModifierOperation::ChildDamage;
		Child.Amount = 3;
		Grand.Operations.Add(Child);
		FModifierDefinition Cancel;
		Cancel.Identifier = TEXT("NestedCancel");
		Cancel.DisplayName = TEXT("Nested branch cancellation");
		Cancel.Subscription = EModifierEvent::Meter;
		Cancel.MatchAmount = true;
		Cancel.RequiredAmount = 20;
		Cancel.Priority = 10;
		Child.Operation = EModifierOperation::Cancel;
		Child.Amount = 0;
		Cancel.Operations.Add(Child);
		Configuration.Definitions.Add(Parent);
		Configuration.Definitions.Add(Grand);
		Configuration.Definitions.Add(Cancel);
		for (FString Identifier : {Parent.Identifier, Grand.Identifier})
			for (int32 Round = 1; Round <= 2; ++Round)
			{
				FModifierInterval Interval;
				Interval.Identifier = Identifier;
				Interval.Round = Round;
				Interval.Duration = 40;
				Configuration.Schedule.Add(Interval);
			}
		FModifierInterval CancelWindow;
		CancelWindow.Identifier = Cancel.Identifier;
		CancelWindow.Start = 8;
		CancelWindow.Duration = 1;
		Configuration.Schedule.Add(CancelWindow);
	}
	if (!Peer)
		Configuration = FModifierConfiguration::FourRulePreset();
	BattleData.Modifiers = Configuration;
	AvailableModifiers = Configuration.Definitions;
	IsTraining = true;
	IsReplay = false;
	FighterRunner = Peer ? Multiplayer : LocalPlay;
	PlayerIndex = Role == TEXT("client") ? 1 : 0;
	FString ReplaySlot;
	if (FParse::Value(FCommandLine::Get(), TEXT("ModifierReplaySlot="), ReplaySlot))
	{
		BattleData.Modifiers = {};
		PlayReplayFromBP(ReplaySlot);
		IsTraining = true;
	}
}
void UModifierPeerWorker::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Enabled = FParse::Value(FCommandLine::Get(), TEXT("ModifierPeerDir="), Directory) &&
	          FParse::Value(FCommandLine::Get(), TEXT("ModifierPeerRole="), Role);
	if (Enabled)
		IFileManager::Get().MakeDirectory(*Directory, true);
}
void UModifierPeerWorker::Observe(ANightSkyGameState* Battle, int32 PlayerOneInput,
                                  int32 PlayerTwoInput, bool bResimulation)
{
	FString LedgerLine = FString::Printf(
	    TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%d,%u"), Battle->LocalFrame, PlayerOneInput, PlayerTwoInput,
	    bResimulation, Battle->BattleState.RoundCount, Battle->GetPlayableRoundFrame(),
	    Battle->BattleState.P1RoundsWon, Battle->BattleState.P2RoundsWon,
	    int32(Battle->BattleState.BattlePhase), Battle->BattleState.RandomManager.GetSeed());
	for (int Team = 0; Team < 2; ++Team)
	{
		const auto* Fighter = Battle->GetMainPlayer(Team == 0);
		LedgerLine += FString::Printf(TEXT(",%d,%d,%d,%d,%d,%d,%d,%d"), Fighter->CurrentHealth,
		                              Battle->BattleState.Meter[Team], Fighter->PosX, Fighter->PosY,
		                              Fighter->SpeedX, Fighter->SpeedY, Fighter->Hitstop,
		                              Fighter->ComboCounter);
	}
	TArray<FString> Rules, Effects;
	for (const auto& Rule : Battle->GetActiveModifiers())
		Rules.Add(FString::Printf(TEXT(",rule:%s:%d:%d"), *Rule.Identifier, Rule.Revision,
		                          Rule.RemainingFrames));
	for (const auto* Object : Battle->Objects)
		if (Object->IsActive)
			Effects.Add(FString::Printf(
			    TEXT(",object:%d:%s:%d:%d:%d:%d:%d:%d:%d"),
			    Object->Player ? Object->Player->PlayerIndex : -1,
			    *(Object->ObjectState ? Object->ObjectState->Name : FGameplayTag()).ToString(),
			    Object->PosX, Object->PosY, Object->SpeedX, Object->SpeedY, Object->ActionTime,
			    Object->Hitstop, int32(Object->Direction)));
	Rules.Sort();
	Effects.Sort();
	for (const auto& Item : Rules)
		LedgerLine += Item;
	for (const auto& Item : Effects)
		LedgerLine += Item;
	LedgerLine += FString::Printf(TEXT(",result:%d:%d:%d"), Battle->GetCurrentRoundResult(),
	                              Battle->BattleState.CurrentWinSide,
	                              Battle->BattleState.BattlePhase == EBattlePhase::EndScreen);
	auto* Widget = Battle->BattleHudActor ? Battle->BattleHudActor->ModifierWidget : nullptr;
	if (Widget)
	{
		Widget->TakeWidget();
		if (Role != TEXT("replay"))
			Widget->RefreshRules();
		FString Label = Widget->GetRenderedRuleText().ToString();
		Label.ReplaceInline(TEXT("\n"), TEXT(" / "));
		Label.ReplaceInline(TEXT("\""), TEXT("\"\""));
		LedgerLine += TEXT(",\"hud:") + Label + TEXT("\"");
		LedgerLine += FString::Printf(TEXT(",hud-frame:%d:%d"), Battle->BattleState.RoundCount,
		                              Battle->GetPlayableRoundFrame());
		if (Role == TEXT("replay") &&
		    FParse::Param(FCommandLine::Get(), TEXT("ModifierLifecycle")) &&
		    Battle->GetPlayableRoundFrame() == 0)
		{
			ModifierCapture::FullPaint(Widget->TakeWidget());
			FWidgetRenderer Renderer(true);
			Renderer.DrawWidget(Widget->TakeWidget(), FVector2D(1024, 512));
			auto* Target = Renderer.DrawWidget(Widget->TakeWidget(), FVector2D(1024, 512));
			FlushRenderingCommands();
			TArray<FColor> Pixels;
			int32 Ink = 0;
			if (Target && Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))
				for (const auto& Pixel : Pixels)
					if (Pixel.R > 16 || Pixel.G > 16 || Pixel.B > 16)
						++Ink;
			const FString Stem =
			    Directory / FString::Printf(TEXT("replay-hud-%d"), Battle->LocalFrame);
			TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*(Stem + TEXT(".png"))));
			const bool SavedImage =
			    Target && File && FImageUtils::ExportRenderTarget2DAsPNG(Target, *File);
			File.Reset();
			const FString Proof =
			    FString::Printf(TEXT("%d,%d,%d,%d\n%s"), Battle->BattleState.RoundCount,
			                    Battle->GetPlayableRoundFrame(), Ink, SavedImage ? 1 : 0,
			                    *Widget->GetRenderedRuleText().ToString());
			FFileHelper::SaveStringToFile(Proof, *(Stem + TEXT(".txt")),
			                              FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
	}
	else
		LedgerLine += TEXT(",hud:missing,hud-frame:-1:-1");
	LedgerLine += FString::Printf(TEXT(",battle-frame:%d\n"), Battle->BattleState.FrameNumber);
	FFileHelper::SaveStringToFile(LedgerLine, *(Directory / (Role + TEXT("-frames.csv"))),
	                              FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
	                              &IFileManager::Get(), FILEWRITE_Append | FILEWRITE_AllowRead);
}
void UModifierPeerWorker::Tick(float)
{
	auto* World = GetWorld();
	if (!World || !World->HasBegunPlay())
		return;
	auto* Battle = World->GetGameState<ANightSkyGameState>();
	auto* GI = Cast<UNightSkyGameInstance>(GetGameInstance());
	if (!Battle || !GI || Battle->Players.Num() != 2)
		return;
	if (!Bound)
	{
		Battle->ModifierFrameObservation = [this](ANightSkyGameState* G, int32 A, int32 C, bool R)
		{ Observe(G, A, C, R); };
		Bound = true;
	}
	if (!Sent && Role != TEXT("replay"))
	{
		for (TActorIterator<ANetworkPawn> It(World); It; ++It)
		{
			if (Role == TEXT("host") && It->GetController() && !It->IsLocallyControlled())
			{
				It->ClientModifierAgreement(GI->BattleData.Modifiers);
				Sent = true;
				break;
			}
			if (Role == TEXT("client") && It->IsLocallyControlled())
			{
				It->ServerModifierAgreement(GI->BattleData.Modifiers);
				Sent = true;
				break;
			}
		}
	}
	if (!Started && Role != TEXT("replay") && GI->ModifierPeerAccepted)
	{
		GI->IsTraining = true;
		Battle->MatchInit();
		GI->IsTraining = false;
		Started = Battle->FighterRunner != nullptr;
	}
	auto* Runner = Cast<AFighterMultiplayerRunner>(Battle->FighterRunner);
	const bool Lifecycle = FParse::Param(FCommandLine::Get(), TEXT("ModifierLifecycle"));
	const int32 EndFrame = Lifecycle ? 900 : 400;
	if (Role == TEXT("host") && Battle->GetConfirmedInputFrame() >= EndFrame && !Saved)
	{
		FString Slot;
		if (FParse::Value(FCommandLine::Get(), TEXT("ModifierSaveSlot="), Slot))
			Saved = GI->SaveRecordedReplay(Slot);
	}
	if (Started)
		for (TActorIterator<ANightSkyPlayerController> It(World); It; ++It)
			if (It->IsLocalController())
			{
				const int32 F = Battle->LocalFrame;
				if (Lifecycle)
				{
					if (!RequestedRematch && Battle->GetConfirmedInputFrame() >= 400 &&
					    Battle->BattleState.BattlePhase == EBattlePhase::EndScreen)
					{
						It->Rematch();
						RequestedRematch = true;
					}
					It->Inputs = (It->Inputs & INP_Rematch) |
					             (Role == TEXT("host") && Battle->BattleState.RoundCount >= 2
					                  ? INP_A
					                  : (F % 4 < 2 ? INP_B : 0));
					continue;
				}
				// After early two-sided combat, client B-only input keeps corrections active
				// without blocking the host's positive sudden-death contact.
				It->Inputs = Role == TEXT("client") && F >= 168
				                 ? (F % 4 < 2 ? INP_B : 0)
				                 : ((F % 4 < 2 ? INP_Right : 0) | (F % 28 < 3 ? INP_A : 0) |
				                    (F >= 63 && F <= 66 ? INP_D : 0));
			}
	if (FPlatformTime::Seconds() < Heartbeat)
		return;
	Heartbeat = FPlatformTime::Seconds() + .1;
	auto* Driver = World->GetNetDriver();
	FString Reason = GI->ModifierSetupError;
	Reason.ReplaceInline(TEXT("\""), TEXT("'"));
	Reason.ReplaceInline(TEXT("\n"), TEXT(" "));
	FString Status = FString::Printf(
	    TEXT("{\"pid\":%u,\"mode\":%d,\"frame\":%d,\"running\":%s,\"confirmed\":%d,\"loads\":%d,"
	         "\"resimulated\":%d,\"connections\":%d,\"server_connection\":%s,\"accepted\":%s,"
	         "\"saved\":%s,\"reason\":\"%s\"}"),
	    FPlatformProcess::GetCurrentProcessId(), int32(World->GetNetMode()),
	    Battle->BattleState.FrameNumber,
	    Battle->IsOnlineBattleReady() ? TEXT("true") : TEXT("false"),
	    Battle->GetConfirmedInputFrame(), Runner ? Runner->RollbackLoads : 0,
	    Runner ? Runner->ResimulatedFrames : 0, Driver ? Driver->ClientConnections.Num() : 0,
	    Driver && Driver->ServerConnection ? TEXT("true") : TEXT("false"),
	    GI->ModifierPeerAccepted ? TEXT("true") : TEXT("false"),
	    Saved ? TEXT("true") : TEXT("false"), *Reason);
	Status.RemoveAt(Status.Len() - 1);
	Status += FString::Printf(TEXT(",\"session_frame\":%d,\"requested_rematch\":%s"),
	                          Battle->LocalFrame, RequestedRematch ? TEXT("true") : TEXT("false"));
	Status += FString::Printf(
	    TEXT(",\"match_complete\":%s,\"winner_side\":%d}"),
	    Battle->BattleState.BattlePhase == EBattlePhase::EndScreen ? TEXT("true") : TEXT("false"),
	    Battle->BattleState.CurrentWinSide);
	FFileHelper::SaveStringToFile(Status, *(Directory / (Role + TEXT("-status.json"))),
	                              FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
	                              &IFileManager::Get(), FILEWRITE_AllowRead);
	if (Role == TEXT("replay") && Battle->LocalFrame >= (Lifecycle ? 895 : 395))
		FPlatformMisc::RequestExit(false);
}

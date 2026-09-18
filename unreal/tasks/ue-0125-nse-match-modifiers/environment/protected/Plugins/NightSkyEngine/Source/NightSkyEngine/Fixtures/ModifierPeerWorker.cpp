#include "ModifierPeerWorker.h"
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

AModifierPeerGameMode::AModifierPeerGameMode()
{
	GameStateClass = AModifierFixtureBattle::StaticClass();
	PlayerControllerClass = AModifierSampleController::StaticClass();
	DefaultPawnClass = ANetworkPawn::StaticClass();
}
void AModifierPeerGameMode::InitGame(const FString& MapName, const FString& Options,
                                     FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	FString PeerRole;
	if (FParse::Value(FCommandLine::Get(), TEXT("ModifierPeerRole="), PeerRole))
		return;
	auto* GI = Cast<UNightSkyGameInstance>(GetGameInstance());
	if (!GI)
		return;
	auto* Data = GetMutableDefault<UModifierFixtureCharaData>();
	GI->BattleData.PlayerListP1 = {Data};
	GI->BattleData.PlayerListP2 = {Data};
	GI->BattleData.BattleFormat = EBattleFormat::Rounds;
	GI->BattleData.TimeUntilRoundStart = 0;
	GI->BattleData.StartRoundTimer = 999;
	GI->BattleData.Modifiers = FModifierConfiguration::FourRulePreset();
	GI->AvailableModifiers = GI->BattleData.Modifiers.Definitions;
	GI->IsTraining = true;
	GI->IsReplay = false;
	GI->FighterRunner = LocalPlay;
}
void AModifierSampleController::BeginPlay()
{
	Super::BeginPlay();
	FString PeerRole;
	if (IsLocalController() &&
	    !FParse::Value(FCommandLine::Get(), TEXT("ModifierPeerRole="), PeerRole))
		GetWorldTimerManager().SetTimerForNextTick(this,
		                                           &AModifierSampleController::ShowModifierSetup);
}
void AModifierSampleController::ShowModifierSetup()
{
	FString PeerRole;
	if (!IsLocalController() ||
	    FParse::Value(FCommandLine::Get(), TEXT("ModifierPeerRole="), PeerRole))
		return;
	auto* Battle = GetWorld()->GetGameState<ANightSkyGameState>();
	auto* GI = Cast<UNightSkyGameInstance>(GetGameInstance());
	if (!Battle || !GI)
		return;
	Battle->SetPaused(true);
	if (ModifierSetup && ModifierSetup->IsInViewport())
		return;
	ModifierSetup = CreateWidget<UModifierSetupWidget>(this, UModifierSetupWidget::StaticClass());
	ModifierSetup->ConfigureForMatch(FModifierConfiguration::FourRulePreset());
	ModifierSetup->AddToViewport(100);
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(ModifierSetup->TakeWidget());
	SetInputMode(Mode);
	bShowMouseCursor = true;
}
void AModifierSampleController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this,
	                        &AModifierSampleController::ShowModifierSetup);
	InputComponent->BindKey(EKeys::Left, IE_Pressed, this, &ANightSkyPlayerController::PressLeft);
	InputComponent->BindKey(EKeys::Left, IE_Released, this,
	                        &ANightSkyPlayerController::ReleaseLeft);
	InputComponent->BindKey(EKeys::Right, IE_Pressed, this, &ANightSkyPlayerController::PressRight);
	InputComponent->BindKey(EKeys::Right, IE_Released, this,
	                        &ANightSkyPlayerController::ReleaseRight);
	InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &ANightSkyPlayerController::PressUp);
	InputComponent->BindKey(EKeys::Up, IE_Released, this, &ANightSkyPlayerController::ReleaseUp);
	InputComponent->BindKey(EKeys::A, IE_Pressed, this, &ANightSkyPlayerController::PressA);
	InputComponent->BindKey(EKeys::A, IE_Released, this, &ANightSkyPlayerController::ReleaseA);
	InputComponent->BindKey(EKeys::S, IE_Pressed, this, &ANightSkyPlayerController::PressB);
	InputComponent->BindKey(EKeys::S, IE_Released, this, &ANightSkyPlayerController::ReleaseB);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ANightSkyPlayerController::PressE);
	InputComponent->BindKey(EKeys::F, IE_Released, this, &ANightSkyPlayerController::ReleaseE);
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &ANightSkyPlayerController::PressF);
	InputComponent->BindKey(EKeys::G, IE_Released, this, &ANightSkyPlayerController::ReleaseF);
	InputComponent->BindKey(EKeys::D, IE_Pressed, this, &ANightSkyPlayerController::PressD);
	InputComponent->BindKey(EKeys::D, IE_Released, this, &ANightSkyPlayerController::ReleaseD);
}
void UModifierFixtureGameInstance::Init()
{
	UGameInstance::Init();
	if (!FParse::Param(FCommandLine::Get(), TEXT("ModifierSample")))
	{
		return;
	}
	auto* FighterData = GetMutableDefault<UModifierFixtureCharaData>();
	BattleData.PlayerListP1 = {FighterData};
	BattleData.PlayerListP2 = {FighterData};
	BattleData.BattleFormat = EBattleFormat::Rounds;
	BattleData.TimeUntilRoundStart = 0;
	BattleData.StartRoundTimer = 999;
	BattleData.Modifiers = FModifierConfiguration::FourRulePreset();
	AvailableModifiers = BattleData.Modifiers.Definitions;
	IsTraining = true;
	IsReplay = false;
	FighterRunner = LocalPlay;
}

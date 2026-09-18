#include "RelaySample.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Components/InputComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"

URelaySampleOne::URelaySampleOne()
{
    CharaName = TEXT("Vanguard");
    CharaFriendlyName = FText::FromString(TEXT("Vanguard"));
    PlayerClass = ARelayFixtureFighter::StaticClass();
}
URelaySampleTwo::URelaySampleTwo()
{
    CharaName = TEXT("Heavy");
    CharaFriendlyName = FText::FromString(TEXT("Heavy"));
    PlayerClass = ARelaySampleHeavy::StaticClass();
}
URelaySampleThree::URelaySampleThree()
{
    CharaName = TEXT("Light");
    CharaFriendlyName = FText::FromString(TEXT("Light"));
    PlayerClass = ARelaySampleLight::StaticClass();
}
URelayPeerThree::URelayPeerThree()
{
    CharaName = TEXT("Glass");
    CharaFriendlyName = FText::FromString(TEXT("Glass"));
    PlayerClass = ARelayPeerGlass::StaticClass();
}
ARelaySampleGameMode::ARelaySampleGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    GameStateClass = ARelayFixtureBattle::StaticClass();
    PlayerControllerClass = ARelaySampleController::StaticClass();
    DefaultPawnClass = nullptr;
}
void UNightSkyGameInstance::RelaySampleSlots(int32 FirstTeamSlot1, int32 FirstTeamSlot2,
                                             int32 FirstTeamSlot3, int32 SecondTeamSlot1,
                                             int32 SecondTeamSlot2, int32 SecondTeamSlot3,
                                             bool Training)
{
    const int32 Choices[6] = {FirstTeamSlot1,  FirstTeamSlot2,  FirstTeamSlot3,
                              SecondTeamSlot1, SecondTeamSlot2, SecondTeamSlot3};
    for (int32 Choice : Choices)
    {
        if (Choice < 1 || Choice > 3)
        {
            return;
        }
    }
    UPrimaryCharaData *Assets[3] = {GetMutableDefault<URelaySampleOne>(),
                                    GetMutableDefault<URelaySampleTwo>(),
                                    GetMutableDefault<URelaySampleThree>()};
    BattleData.Stage = GetMutableDefault<URelaySampleStage>();
    BattleData.PlayerListP1.Empty();
    BattleData.PlayerListP2.Empty();
    for (int SlotIndex = 0; SlotIndex < 3; ++SlotIndex)
    {
        BattleData.PlayerListP1.Add(Assets[Choices[SlotIndex] - 1]);
        BattleData.PlayerListP2.Add(Assets[Choices[SlotIndex + 3] - 1]);
    }
    BattleData.BattleFormat = EBattleFormat::Relay;
    BattleData.TimeUntilRoundStart = 0;
    BattleData.StartRoundTimer = 99;
    IsTraining = Training;
    IsReplay = false;
    FighterRunner = LocalPlay;
    UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true,
                                TEXT("game=/Script/NightSkyEngine.RelaySampleGameMode"));
}

void ARelaySampleController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::Left, IE_Pressed, this, &ANightSkyPlayerController::PressLeft);
    InputComponent->BindKey(EKeys::Left, IE_Released, this,
                            &ANightSkyPlayerController::ReleaseLeft);
    InputComponent->BindKey(EKeys::Right, IE_Pressed, this, &ANightSkyPlayerController::PressRight);
    InputComponent->BindKey(EKeys::Right, IE_Released, this,
                            &ANightSkyPlayerController::ReleaseRight);
    InputComponent->BindKey(EKeys::A, IE_Pressed, this,
                            &ANightSkyPlayerController::PressA);
    InputComponent->BindKey(EKeys::A, IE_Released, this,
                            &ANightSkyPlayerController::ReleaseA);
    InputComponent->BindKey(EKeys::S, IE_Pressed, this, &ANightSkyPlayerController::PressB);
    InputComponent->BindKey(EKeys::S, IE_Released, this, &ANightSkyPlayerController::ReleaseB);
    InputComponent->BindKey(EKeys::D, IE_Pressed, this,
                            &ANightSkyPlayerController::PressC);
    InputComponent->BindKey(EKeys::D, IE_Released, this,
                            &ANightSkyPlayerController::ReleaseC);
    InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ARelaySampleController::OneDown);
    InputComponent->BindKey(EKeys::One, IE_Released, this, &ARelaySampleController::OneUp);
    InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ARelaySampleController::TwoDown);
    InputComponent->BindKey(EKeys::Two, IE_Released, this, &ARelaySampleController::TwoUp);
    InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ARelaySampleController::ThreeDown);
    InputComponent->BindKey(EKeys::Three, IE_Released, this, &ARelaySampleController::ThreeUp);
    InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ANightSkyPlayerController::ResetTraining);
}

void ARelaySampleGameMode::BeginPlay()
{
    Super::BeginPlay();
    auto Sun = GetWorld()->SpawnActor<ADirectionalLight>();
    Sun->SetActorRotation(FRotator(-45, -90, 0));
    Cast<UDirectionalLightComponent>(Sun->GetLightComponent())->SetIntensity(100);
    auto Sky = GetWorld()->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetIntensity(1);
}

void URelayFixtureGameInstance::Init()
{
    UGameInstance::Init();
    if (!FParse::Param(FCommandLine::Get(), TEXT("RelaySample")))
    {
        return;
    }
    UPrimaryCharaData *Assets[3] = {GetMutableDefault<URelaySampleOne>(),
                                    GetMutableDefault<URelaySampleTwo>(),
                                    GetMutableDefault<URelaySampleThree>()};
    for (auto Asset : Assets)
    {
        BattleData.PlayerListP1.Add(Asset);
        BattleData.PlayerListP2.Add(Asset);
    }
    BattleData.BattleFormat = EBattleFormat::Relay;
    BattleData.TimeUntilRoundStart = 0;
    BattleData.StartRoundTimer = 999;
    BattleData.Stage = GetMutableDefault<URelaySampleStage>();
    BattleData.Random.Reseed(39039);
    IsTraining = true;
    IsReplay = false;
    FighterRunner = LocalPlay;
    FString PeerRole;
    if (FParse::Value(FCommandLine::Get(), TEXT("RelayPeerRole="), PeerRole))
    {
        BattleData.PlayerListP1[2] = GetMutableDefault<URelayPeerThree>();
        BattleData.PlayerListP2[2] = GetMutableDefault<URelayPeerThree>();
        BattleData.StartRoundTimer = 6;
        FighterRunner = Multiplayer;
        PlayerIndex = PeerRole == TEXT("client") ? 1 : 0;
    }
    FString ReplaySlot;
    if (FParse::Value(FCommandLine::Get(), TEXT("RelayReplaySlot="), ReplaySlot) &&
        UGameplayStatics::DoesSaveGameExist(ReplaySlot, 0))
    {
        PlayReplayFromBP(ReplaySlot);
    }
}
URelaySampleStage::URelaySampleStage()
{
    StageName = TEXT("Relay Training");
    StageFriendlyName = FText::FromString(TEXT("Relay Training"));
    StageURL = TEXT("/Engine/Maps/Entry?game=/Script/NightSkyEngine.RelaySampleGameMode");
}

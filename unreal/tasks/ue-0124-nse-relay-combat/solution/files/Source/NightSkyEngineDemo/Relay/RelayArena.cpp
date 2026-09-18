#include "RelayArena.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "NightSkyEngine/Miscellaneous/ReplayInfo.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    const FName ArenaMap(TEXT("/Game/Relay124/RelayArena"));
    const TCHAR* Names[] = {TEXT("VANGUARD"), TEXT("HEAVY"), TEXT("LIGHT")};
    constexpr int32 Health[] = {10000, 12000, 9000};
    URelayArenaSession* Session(const UObject* Context)
    {
        auto* GI = UGameplayStatics::GetGameInstance(Context);
        return GI ? GI->GetSubsystem<URelayArenaSession>() : nullptr;
    }
    ANightSkyGameState* Battle(const UObject* Context)
    {
        return Context && Context->GetWorld() ? Context->GetWorld()->GetGameState<ANightSkyGameState>() : nullptr;
    }
}

void URelayArenaSession::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Roster = {1, 2, 3, 1, 2, 3};
    // Select the latest locally saved arena replay after restarting the application.
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(FPaths::ProjectSavedDir() / TEXT("SaveGames/RELAY124_*.sav")), true, false);
    Files.Sort();
    if (!Files.IsEmpty()) SavedReplay = FPaths::GetBaseFilename(Files.Last());
}

URelayArenaStage::URelayArenaStage()
{
    StageName = TEXT("Jade Relay Arena");
    StageFriendlyName = FText::FromString(TEXT("Jade Relay Arena"));
    StageURL = ArenaMap.ToString();
}

void URelayArenaSession::ApplyRoster()
{
    auto* GI = CastChecked<UNightSkyGameInstance>(GetGameInstance());
    UPrimaryCharaData* Choices[] = {GetMutableDefault<URelaySampleOne>(), GetMutableDefault<URelaySampleTwo>(), GetMutableDefault<URelaySampleThree>()};
    GI->BattleData.PlayerListP1.Reset();
    GI->BattleData.PlayerListP2.Reset();
    GI->BattleData.ColorIndicesP1 = {1, 1, 1};
    GI->BattleData.ColorIndicesP2 = {2, 2, 2};
    for (int32 Index = 0; Index < 6; ++Index)
        (Index < 3 ? GI->BattleData.PlayerListP1 : GI->BattleData.PlayerListP2).Add(Choices[FMath::Clamp(Roster[Index], 1, 3) - 1]);
    GI->BattleData.bIsValid = true;
    GI->BattleData.BattleFormat = EBattleFormat::Relay;
    GI->BattleData.TimeUntilRoundStart = 0;
    GI->BattleData.StartRoundTimer = 99;
    GI->BattleData.Stage = GetMutableDefault<URelayArenaStage>();
    GI->BattleData.Random.Reseed(124124);
    GI->BattleVersion = TEXT("Relay124-1");
    GI->IsTraining = Training;
    GI->IsReplay = false;
    GI->IsCPUBattle = false;
    GI->FighterRunner = LocalPlay;
}

void URelayArenaSession::CycleSlot(int32 Index)
{
    if (Roster.IsValidIndex(Index)) Roster[Index] = Roster[Index] % 3 + 1;
}

FString URelayArenaSession::SlotLabel(int32 Index) const
{
    const int32 Choice = FMath::Clamp(Roster[Index], 1, 3) - 1;
    return FString::Printf(TEXT("SLOT %d\n%s\n%d HP"), Index % 3 + 1, Names[Choice], Health[Choice]);
}

void URelayArenaSession::StartMatch()
{
    ReplayTravel = false;
    StartOnTravel = true;
    SelectionOpen = false;
    Message = TEXT("New match. Relay inputs are press edges; release before routing.");
    UGameplayStatics::OpenLevel(this, ArenaMap);
}

void URelayArenaSession::SaveReplay()
{
    auto* GI = CastChecked<UNightSkyGameInstance>(GetGameInstance());
    const auto* Tape = GI->GetCurrentReplay();
    if (GI->IsReplay || !Tape || Tape->LengthInFrames <= 0)
    {
        Message = TEXT("Play a live match before saving its replay.");
        return;
    }
    // Snapshot all effective inputs and original initialization into an unused identity.
    const FString Slot = TEXT("RELAY124_") + FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (UGameplayStatics::SaveGameToSlot(const_cast<UReplaySaveInfo*>(Tape), Slot, 0))
    {
        SavedReplay = Slot;
        Message = TEXT("Replay saved. B watches the latest saved match; live recording continues.");
    }
    else Message = TEXT("Could not save replay. The live recording is retained; press V to retry.");
}

void URelayArenaSession::WatchReplay()
{
    auto* GI = CastChecked<UNightSkyGameInstance>(GetGameInstance());
    auto* Tape = Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(SavedReplay, 0));
    bool Valid = Tape && Tape->Version == TEXT("Relay124-1") && Tape->BattleData.BattleFormat == EBattleFormat::Relay &&
        Tape->BattleData.Stage == GetMutableDefault<URelayArenaStage>() && Tape->LengthInFrames >= 0 &&
        Tape->InputsP1.Num() == Tape->LengthInFrames && Tape->InputsP2.Num() == Tape->LengthInFrames &&
        Tape->BattleData.PlayerListP1.Num() == 3 && Tape->BattleData.PlayerListP2.Num() == 3;
    if (Valid)
        for (const auto& Team : {Tape->BattleData.PlayerListP1, Tape->BattleData.PlayerListP2})
            for (const auto& Chara : Team) Valid &= Chara.LoadSynchronous() && Chara->PlayerClass;
    if (!Valid) { Message = TEXT("No compatible saved arena replay. Press V after playing a match."); return; }
    GI->PlayReplayFromBP(SavedReplay);
    ReplayTravel = true;
    StartOnTravel = true;
    SelectionOpen = false;
    Message = TEXT("REPLAY: both teams use recorded inputs. P pauses. R returns to a fresh live match.");
    UGameplayStatics::OpenLevel(this, ArenaMap);
}

ARelayArenaGameMode::ARelayArenaGameMode()
{
    GameStateClass = ARelayFixtureBattle::StaticClass();
    PlayerControllerClass = ARelayArenaController::StaticClass();
    DefaultPawnClass = nullptr;
}

void ARelayArenaGameMode::InitGame(const FString& MapName, const FString& Options, FString& Error)
{
    Super::InitGame(MapName, Options, Error);
    auto* Settings = Session(this);
    CastChecked<UNightSkyGameInstance>(GetGameInstance())->bRelayArenaControls = true;
    if (!Settings->ReplayTravel) Settings->ApplyRoster();
    Settings->ReplayTravel = false;
    Settings->SelectionOpen = !Settings->StartOnTravel;
    Settings->StartOnTravel = false;
}

void ARelayArenaGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (!UGameplayStatics::GetPlayerController(this, 1)) UGameplayStatics::CreatePlayer(this, 1, true);
    GetWorld()->SpawnActor<ARelayArenaPresentation>();
}

void ARelayArenaController::SetupInputComponent()
{
    Super::SetupInputComponent();
    if (GetLocalPlayer() && GetLocalPlayer()->GetControllerId() != 0) return;
    InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ARelayArenaController::ToggleSelection);
    InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ARelayArenaController::StartSelectedMatch);
    InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ARelayArenaController::TogglePause);
    InputComponent->BindKey(EKeys::V, IE_Pressed, this, &ARelayArenaController::SaveMatchReplay);
    InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ARelayArenaController::WatchMatchReplay);
    InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ARelayArenaController::RestartMatch);
    const TPair<FKey, int32> Keys[] = {{EKeys::J, INP_Left}, {EKeys::L, INP_Right}, {EKeys::U, INP_A},
        {EKeys::I, INP_B}, {EKeys::O, INP_C}, {EKeys::Seven, RelaySlot1}, {EKeys::Eight, RelaySlot2}, {EKeys::Nine, RelaySlot3}};
    for (const auto& Key : Keys)
        for (bool Held : {true, false})
        {
            FInputKeyBinding Binding(FInputChord(Key.Key), Held ? IE_Pressed : IE_Released);
            Binding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &ARelayArenaController::SecondInput, Key.Value, Held);
            InputComponent->KeyBindings.Add(MoveTemp(Binding));
        }
}

void ARelayArenaController::BeginPlay()
{
    Super::BeginPlay();
    if (GetWorld()->GetFirstPlayerController() != this) return;
    SelectionWidget = CreateWidget<URelayArenaMenu>(this);
    SelectionWidget->AddToViewport(200);
    SetMenuInput(Session(this)->SelectionOpen);
}

void ARelayArenaController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (SelectionWidget) SelectionWidget->RemoveFromParent();
    Super::EndPlay(Reason);
}

void ARelayArenaController::SecondInput(int32 Mask, bool Held)
{
    if (Held) ManualSecondInput |= Mask;
    else ManualSecondInput &= ~Mask;
}

void ARelayArenaController::SetMenuInput(bool Open)
{
    if (SelectionWidget) SelectionWidget->SetVisibility(Open ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    bShowMouseCursor = Open;
    Inputs = 0;
    ManualSecondInput = 0;
    if (auto* State = Battle(this)) State->SetPaused(Open);
    if (Open)
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        SetInputMode(Mode);
    }
    else SetInputMode(FInputModeGameOnly());
}

void ARelayArenaController::ToggleSelection()
{
    if (GetWorld()->GetFirstPlayerController() != this) return;
    auto* Settings = Session(this);
    Settings->SelectionOpen = !Settings->SelectionOpen;
    SetMenuInput(Settings->SelectionOpen);
}
void ARelayArenaController::StartSelectedMatch()
{
    if (Session(this)->SelectionOpen) Session(this)->StartMatch();
}
void ARelayArenaController::TogglePause()
{
    if (auto* State = Battle(this); State && !Session(this)->SelectionOpen) State->SetPaused(!State->bPauseGame);
}
void ARelayArenaController::SaveMatchReplay() { Session(this)->SaveReplay(); }
void ARelayArenaController::WatchMatchReplay() { Session(this)->WatchReplay(); }
void ARelayArenaController::RestartMatch()
{
    if (CastChecked<UNightSkyGameInstance>(GetGameInstance())->IsReplay) { Session(this)->StartMatch(); return; }
    Session(this)->SelectionOpen = false;
    SetMenuInput(false);
    // Both input words carry the ordinary rematch command on the next gameplay update.
    RematchFrames = 1;
    if (auto* State = Battle(this)) State->SetPaused(false);
}

void ARelayArenaController::PostRematch()
{
    Super::PostRematch();
    RematchFrames = 0;
}

void ARelayArenaController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetWorld()->GetFirstPlayerController() != this) return;
    auto* State = Battle(this);
    auto* Settings = Session(this);
    auto* Second = Cast<ANightSkyPlayerController>(UGameplayStatics::GetPlayerController(this, 1));
    if (!State || !Second || State->Players.Num() != 6) return;
    CastChecked<UNightSkyGameInstance>(GetGameInstance())->RelayPresentationNotice = Settings->Message;
    if (Settings->SelectionOpen) State->SetPaused(true);
    int32 SecondWord = Settings->SelectionOpen ? 0 : ManualSecondInput;
    if (Settings->Sparring && !Settings->SelectionOpen && !CastChecked<UNightSkyGameInstance>(GetGameInstance())->IsReplay)
    {
        SecondWord = 0;
        const auto* Main = State->GetMainPlayer(false);
        const auto* Enemy = State->GetMainPlayer(true);
        const int32 GameplayFrame = State->BattleState.FrameNumber;
        const int32 Distance = FMath::Abs(Main->PosX - Enemy->PosX);
        if (Distance > 250000) SecondWord |= Main->PosX > Enemy->PosX ? INP_Left : INP_Right;
        else if (GameplayFrame % 90 < 25) SecondWord |= Main->PosX > Enemy->PosX ? INP_Right : INP_Left;
        if (GameplayFrame % 100 == 40) SecondWord |= INP_A;
        if (GameplayFrame % 360 == 200) SecondWord |= INP_B;
        const auto Status = State->GetRelayStatus(false);
        if ((Status.Stage == ERelayPhase::Idle && GameplayFrame % 240 == 100) || Status.Stage == ERelayPhase::Route)
            for (int32 Slot = 3; Slot >= 1; --Slot)
                if (State->RelayEligibility(false, Slot) == ERelayRejection::None)
                { SecondWord |= RelaySlot1 << (Slot - 1); break; }
    }
    if (RematchFrames > 0)
    {
        Inputs |= INP_Rematch;
        SecondWord |= INP_Rematch;
    }
    else Inputs &= ~INP_Rematch;
    Second->Inputs = SecondWord;
}

TSharedRef<SWidget> URelayArenaMenu::RebuildWidget()
{
    auto* Settings = Session(this);
    const FSlateFontInfo Title = FCoreStyle::GetDefaultFontStyle("Bold", 30);
    const FSlateFontInfo Normal = FCoreStyle::GetDefaultFontStyle("Regular", 18);
    auto Text = [&](const FString& Value) { return SNew(STextBlock).Text(FText::FromString(Value)).Font(Normal).ColorAndOpacity(FLinearColor(.8f,.88f,1.f)); };
    TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
    Body->AddSlot().AutoHeight().Padding(0,0,0,12)[SNew(STextBlock).Text(FText::FromString(TEXT("JADE CIRCUIT  /  THREE-FIGHTER RELAY"))).Font(Title)];
    Body->AddSlot().AutoHeight()[Text(TEXT("Choose the fighter in each immutable slot. Click a card to cycle.\nVanguard 10,000 HP  |  Heavy 12,000 HP  |  Light 9,000 HP"))];
    TSharedRef<SHorizontalBox> Teams = SNew(SHorizontalBox);
    for (int32 Side = 0; Side < 2; ++Side)
    {
        TSharedRef<SVerticalBox> Team = SNew(SVerticalBox);
        Team->AddSlot().AutoHeight().Padding(0,14,0,6)[Text(FString::Printf(TEXT("TEAM %d"),Side+1))];
        TSharedRef<SHorizontalBox> Cards = SNew(SHorizontalBox);
        for (int32 SlotIndex = 0; SlotIndex < 3; ++SlotIndex)
        {
            const int32 Index = Side * 3 + SlotIndex;
            Cards->AddSlot().FillWidth(1).Padding(3)[SNew(SButton).IsFocusable(false)
                .ContentPadding(FMargin(12,18)).OnClicked_Lambda([Settings,Index](){Settings->CycleSlot(Index);return FReply::Handled();})
                [SNew(STextBlock).Font(Normal).Justification(ETextJustify::Center)
                    .Text_Lambda([Settings,Index](){return FText::FromString(Settings->SlotLabel(Index));})]];
        }
        Team->AddSlot().AutoHeight()[Cards];
        Teams->AddSlot().FillWidth(1).Padding(8,0)[Team];
    }
    Body->AddSlot().AutoHeight()[Teams];
    Body->AddSlot().AutoHeight().Padding(0,12)[SNew(SButton).IsFocusable(false).OnClicked_Lambda([Settings](){Settings->Sparring=!Settings->Sparring;return FReply::Handled();})
        [SNew(STextBlock).Font(Normal).Text_Lambda([Settings](){return FText::FromString(Settings->Sparring ? TEXT("P2: SPARRING INPUTS  (click for manual controls)") : TEXT("P2: MANUAL CONTROLS  (click for sparring inputs)"));})]];
    Body->AddSlot().AutoHeight()[Text(TEXT("P1: Left / Right move + hold away to block   A strike   S throw   D burst   1 / 2 / 3 relay\nP2: J / L move + hold away to block   U strike   I throw   O burst   7 / 8 / 9 relay\nRelay: press an eligible reserve, RELEASE, then press again during ROUTE to hand off.\n6 entry  /  12 synchronized  /  10 route  /  optional 12 follow-up  /  6 exit\nEach relay costs 100 of 200. No round regeneration. Enrolled fighters cool down for 120 frames."))];
    TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
    Actions->AddSlot().FillWidth(1).Padding(3)[SNew(SButton).IsFocusable(false).ContentPadding(12).OnClicked_Lambda([Settings](){Settings->StartMatch();return FReply::Handled();})[Text(TEXT("ENTER  /  START SELECTED TEAMS"))]];
    Actions->AddSlot().AutoWidth().Padding(3)[SNew(SButton).IsFocusable(false).OnClicked_Lambda([Settings](){Settings->Training=!Settings->Training;return FReply::Handled();})[SNew(STextBlock).Font(Normal).Text_Lambda([Settings](){return FText::FromString(Settings->Training?TEXT("LETHAL TRAINING"):TEXT("VERSUS"));})]];
    Body->AddSlot().AutoHeight().Padding(0,12)[Actions];
    Body->AddSlot().AutoHeight()[Text(TEXT("Tab resumes current match  |  P pause  |  T training reset  |  R rematch\nV save full replay  |  B watch last saved replay  |  PIE: Esc exits play, Shift+F1 releases mouse"))];
    Body->AddSlot().AutoHeight().Padding(0,8)[SNew(STextBlock).Font(Normal).AutoWrapText(true).Text_Lambda([Settings](){return FText::FromString(Settings->Message);})];
    return SNew(SOverlay)+SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride(1260)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).Padding(24).BorderBackgroundColor(FLinearColor(.018f,.026f,.055f,.98f))[Body]]];
}

ARelayArenaPresentation::ARelayArenaPresentation()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("PresentationRoot"));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Mesh(TEXT("/Game/NightSkyEngine/CharacterAssets/AstralWarden/Meshes/SK_AstralWarden.SK_AstralWarden"));
    FighterMesh = Mesh.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Blue(TEXT("/Game/NightSkyEngine/CharacterAssets/AstralWarden/Materials/MI_AW_AetherLight_P1.MI_AW_AetherLight_P1"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Pink(TEXT("/Game/NightSkyEngine/CharacterAssets/AstralWarden/Materials/MI_AW_AetherLight_P2.MI_AW_AetherLight_P2"));
    TeamMaterials[0] = Blue.Object; TeamMaterials[1] = Pink.Object;
    static ConstructorHelpers::FObjectFinder<UAnimationAsset> A0(TEXT("/Game/NightSkyEngine/CharacterAssets/Mannequins/Animations/AS_manny_stand.AS_manny_stand"));
    static ConstructorHelpers::FObjectFinder<UAnimationAsset> A1(TEXT("/Game/NightSkyEngine/CharacterAssets/Mannequins/Animations/AS_manny_fwalk.AS_manny_fwalk"));
    static ConstructorHelpers::FObjectFinder<UAnimationAsset> A2(TEXT("/Game/NightSkyEngine/CharacterAssets/Mannequins/Animations/AS_manny_5a.AS_manny_5a"));
    static ConstructorHelpers::FObjectFinder<UAnimationAsset> A3(TEXT("/Game/NightSkyEngine/CharacterAssets/Mannequins/Animations/AS_manny_5b.AS_manny_5b"));
    static ConstructorHelpers::FObjectFinder<UAnimationAsset> A4(TEXT("/Game/NightSkyEngine/CharacterAssets/Mannequins/Animations/AS_manny_standblock.AS_manny_standblock"));
    static ConstructorHelpers::FObjectFinder<UAnimationAsset> A5(TEXT("/Game/NightSkyEngine/CharacterAssets/Mannequins/Animations/AS_manny_hitstun.AS_manny_hitstun"));
    static ConstructorHelpers::FObjectFinder<UAnimationAsset> A6(TEXT("/Game/NightSkyEngine/CharacterAssets/Mannequins/Animations/AS_manny_facedown.AS_manny_facedown"));
    Idle=A0.Object; Walk=A1.Object; Strike=A2.Object; Followup=A3.Object; Block=A4.Object; Hit=A5.Object; Knockout=A6.Object;
}

void ARelayArenaPresentation::InitializeFighters(ANightSkyGameState* State)
{
    for (int32 Index = 0; Index < 6; ++Index)
    {
        auto* Body = NewObject<USkeletalMeshComponent>(this);
        Body->SetupAttachment(RootComponent);
        Body->SetSkeletalMeshAsset(FighterMesh);
        Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        Body->SetComponentTickEnabled(false);
        Body->SetCastShadow(true);
        Body->RegisterComponent();
        if (Index >= 3)
        {
            for (int32 MaterialIndex = 0; MaterialIndex < Body->GetNumMaterials(); ++MaterialIndex)
            {
                const auto* Existing = Body->GetMaterial(MaterialIndex);
                if (!Existing) continue;
                FString Name = Existing->GetName();
                Name.RemoveFromEnd(TEXT("_P1"));
                Name.RemoveFromStart(TEXT("MI_"));
                Name.RemoveFromStart(TEXT("M_"));
                const FString Replacement = TEXT("/Game/NightSkyEngine/CharacterAssets/AstralWarden/Materials/MI_") + Name + TEXT("_P2");
                if (auto* Material = LoadObject<UMaterialInterface>(nullptr, *Replacement)) Body->SetMaterial(MaterialIndex, Material);
            }
        }
        Bodies.Add(Body);
        auto* Label = NewObject<UTextRenderComponent>(this);
        Label->SetupAttachment(RootComponent);
        Label->SetHorizontalAlignment(EHTA_Center);
        Label->SetWorldSize(17);
        Label->SetTextRenderColor(Index < 3 ? FColor(80,215,255) : FColor(255,135,205));
        Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Label->RegisterComponent();
        Labels.Add(Label);
    }
    auto* Sphere = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    for (int32 Index = 0; Index < State->Objects.Num(); ++Index)
    {
        auto* Ball = NewObject<UStaticMeshComponent>(this);
        Ball->SetupAttachment(RootComponent);
        Ball->SetStaticMesh(Sphere);
        Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Ball->SetCastShadow(false);
        Ball->SetWorldScale3D(FVector(.23,.23,.23));
        Ball->RegisterComponent();
        Projectiles.Add(Ball);
    }
}

void ARelayArenaPresentation::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* State = Battle(this);
    if (!State || State->Players.Num()!=6) return;
    if (Bodies.IsEmpty()) InitializeFighters(State);
    const auto Transform = State->BattleSceneTransform;
    for (int32 Index = 0; Index < 6; ++Index)
    {
        auto* Fighter = State->Players[Index];
        for (auto* Cube : TInlineComponentArray<UStaticMeshComponent*>(Fighter))
        {
            Cube->SetVisibility(false);
            // The battle camera fits actor bounds. Expand the hidden display proxy to
            // contain the armored mesh and its role label, without changing combat boxes.
            Cube->SetRelativeScale3D(FVector(1.f, 1.f, 3.f));
            Cube->SetRelativeLocation(FVector(0, Fighter->IsMainPlayer() ? 0.f : 72.f + 62.f * Fighter->TeamIndex, 150));
        }
        const bool Visible = Fighter->IsOnScreen();
        auto* Body = Bodies[Index].Get();
        Body->SetVisibility(Visible);
        Labels[Index]->SetVisibility(Visible);
        if (!Visible) continue;
        const bool Main = Fighter->IsMainPlayer();
        // Depth separation is visual only; real contacts still use the unmodified fixed-point origins.
        const float Depth = Main ? 0.f : 72.f + 62.f * Fighter->TeamIndex;
        const FVector Position(float(Fighter->PosX)/COORD_SCALE, Depth, float(Fighter->PosY)/COORD_SCALE);
        Body->SetWorldLocation(Transform.TransformPosition(Position));
        Body->SetWorldRotation(Transform.GetRotation() * FRotator(0,Fighter->Direction==DIR_Right ? 0.f:180.f,0).Quaternion());
        const float Bulk = Fighter->MaxHealth==12000 ? 1.08f : Fighter->MaxHealth==9000 ? .93f : 1.f;
        Body->SetWorldScale3D(FVector(Bulk,Bulk,1.05f));
        const auto* Current = Fighter->PrimaryStateMachine.CurrentState;
        const FString Move = Current ? Current->Name.ToString() : FString();
        UAnimationAsset* Animation = Idle;
        float Playback = Fighter->ActionTime/60.f;
        if (Fighter->CurrentHealth<=0) { Animation=Knockout; Playback=0; }
        else if (Move.Contains(TEXT("Block"))) Animation=Block;
        else if (Fighter->CheckIsStunned() || Fighter->PlayerFlags & PLF_IsThrowLock) Animation=Hit;
        else if (Move.Contains(TEXT("Followup"))) {Animation=Followup;Playback*=2.0f;}
        else if (Move.Contains(TEXT("Synchronized"))) {Animation=Strike;Playback*=2.0f;}
        else if (Move.Contains(TEXT("Throw"))) Animation=Followup;
        else if (Fighter->ObjectReg1>0) {Animation=Strike;Playback=(20-Fighter->ObjectReg1)/60.f;}
        else if (Fighter->Inputs & (INP_Left|INP_Right)) Animation=Walk;
        if (Animation)
        {
            if (!Body->GetSingleNodeInstance() || Body->GetSingleNodeInstance()->GetCurrentAsset()!=Animation) Body->SetAnimation(Animation);
            if (Animation == Idle || Animation == Walk)
                if (const auto* Sequence = Cast<UAnimSequenceBase>(Animation)) Playback = FMath::Fmod(Playback, FMath::Max(.01f, Sequence->GetPlayLength()));
            Body->SetPosition(Playback,false);
            Body->TickAnimation(0.f,false);
            Body->RefreshBoneTransforms();
        }
        Labels[Index]->SetWorldLocation(Transform.TransformPosition(Position+FVector(0,0,245)));
        Labels[Index]->SetWorldRotation(Transform.GetRotation()*FRotator(0,90,0).Quaternion());
        Labels[Index]->SetText(FText::FromString(FString::Printf(TEXT("%d  %s"),Fighter->TeamIndex+1,
            Fighter->CurrentHealth<=0?TEXT("KO"):Main?TEXT("MAIN"):TEXT("RELAY"))));
    }
    for (int32 Index=0; Index<Projectiles.Num(); ++Index)
    {
        auto* Object=State->Objects[Index];
        auto* Ball=Projectiles[Index].Get();
        Ball->SetVisibility(Object->IsActive && Object->Player);
        if (!Object->IsActive || !Object->Player) continue;
        Ball->SetMaterial(0,TeamMaterials[FMath::Clamp(Object->Player->PlayerIndex,0,1)]);
        Ball->SetWorldLocation(Transform.TransformPosition(FVector(float(Object->PosX)/COORD_SCALE,0,float(Object->PosY)/COORD_SCALE+100)));
        const float Pulse=.23f+.035f*FMath::Sin(Object->ActionTime*1.7f);
        Ball->SetWorldScale3D(FVector(Pulse*2.3f,Pulse,Pulse));
    }
}

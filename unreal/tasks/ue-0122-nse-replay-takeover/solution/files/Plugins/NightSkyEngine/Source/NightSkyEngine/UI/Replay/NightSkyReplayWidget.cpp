#include "NightSkyReplayWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "NightSkyEngine/Miscellaneous/ReplayInfo.h"

const TArray<FReplayUIBinding>& UNightSkyReplayWidget::GetBindings()
{
    static const TArray<FReplayUIBinding> Bindings = {
        {EKeys::Tab, EReplayUICommand::ToggleHelp, TEXT("Tab  show / hide help")},
        {EKeys::R, EReplayUICommand::WatchRecording, TEXT("R  watch current recording")},
        {EKeys::P, EReplayUICommand::Pause, TEXT("P  pause / resume")},
        {EKeys::Comma, EReplayUICommand::BackFrame, TEXT(",  back 1 frame")},
        {EKeys::Period, EReplayUICommand::ForwardFrame, TEXT(".  forward 1 frame")},
        {EKeys::LeftBracket, EReplayUICommand::BackSecond, TEXT("[  back 60 frames")},
        {EKeys::RightBracket, EReplayUICommand::ForwardSecond, TEXT("]  forward 60 frames")},
        {EKeys::Home, EReplayUICommand::Start, TEXT("Home  first frame")},
        {EKeys::End, EReplayUICommand::End, TEXT("End  source end")},
        {EKeys::One, EReplayUICommand::TakeP1, TEXT("1  take over P1")},
        {EKeys::Two, EReplayUICommand::TakeP2, TEXT("2  take over P2")},
        {EKeys::BackSpace, EReplayUICommand::Finish, TEXT("Backspace (Mac: Delete)  finish + save branch")},
        {EKeys::Semicolon, EReplayUICommand::PreviousSlot, TEXT(";  previous saved replay")},
        {EKeys::Apostrophe, EReplayUICommand::NextSlot, TEXT("'  next saved replay")},
        {EKeys::Enter, EReplayUICommand::LoadSlot, TEXT("Enter  load selected replay")}
    };
    return Bindings;
}

UTextBlock* UNightSkyReplayWidget::AddText(UVerticalBox* Box, const FString& Text, int32 Size, FLinearColor Color)
{
    auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
    FSlateFontInfo Font = Label->GetFont();
    Font.Size = Size;
    Label->SetFont(Font);
    Label->SetColorAndOpacity(FSlateColor(Color));
    Label->SetText(FText::FromString(Text));
    Label->SetAutoWrapText(true);
    Box->AddChildToVerticalBox(Label)->SetPadding(FMargin(0, 2));
    return Label;
}

void UNightSkyReplayWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget = Canvas;
    auto* Panel = WidgetTree->ConstructWidget<UBorder>();
    Panel->SetBrushColor(FLinearColor(0.012f, 0.022f, 0.042f, 0.94f));
    Panel->SetPadding(FMargin(16, 10));
    auto* Slot = Canvas->AddChildToCanvas(Panel);
    Slot->SetAnchors(FAnchors(0.02f, 0.98f, 0.98f, 0.98f));
    Slot->SetAlignment(FVector2D(0, 1));
    Slot->SetOffsets(FMargin(0));
    Slot->SetAutoSize(true);
    auto* Body = WidgetTree->ConstructWidget<UVerticalBox>();
    Panel->SetContent(Body);
    const FLinearColor Blue(0.24f, 0.78f, 1.0f);
    const FLinearColor Muted(0.65f, 0.74f, 0.83f);
    StatusText = AddText(Body, TEXT("REPLAY LAB"), 24, Blue);
    PositionText = AddText(Body, TEXT(""), 18, FLinearColor::White);
    Timeline = WidgetTree->ConstructWidget<UProgressBar>();
    Timeline->SetFillColorAndOpacity(Blue);
    Body->AddChildToVerticalBox(Timeline)->SetPadding(FMargin(0, 5));
    Details = WidgetTree->ConstructWidget<UVerticalBox>();
    Body->AddChildToVerticalBox(Details);
    ContextText = AddText(Details, TEXT(""), 18, FLinearColor::White);
    // Help comes from the same table used to bind the physical keys.
    const auto& Keys = GetBindings();
    for (const TPair<int32, int32> Range : {TPair<int32, int32>(1, 2), {3, 8}, {9, 11}, {12, 14}})
    {
        FString Line;
        for (int32 I = Range.Key; I <= Range.Value; ++I)
        {
            if (!Line.IsEmpty()) Line += TEXT("    |    ");
            Line += Keys[I].Hint;
        }
        AddText(Details, Line, 17, Muted);
    }
    LibraryText = AddText(Details, TEXT(""), 17, Muted);
    FeedbackText = AddText(Body, TEXT(""), 17, FLinearColor(1.0f, 0.78f, 0.35f));
    RefreshSlots(Cast<UNightSkyGameInstance>(GetGameInstance()));
    Refresh();
}

FString UNightSkyReplayWidget::DescribeState(const UNightSkyGameInstance* Game, bool bBattlePaused)
{
    if (!Game || !Game->IsReplay) return Game && !Game->IsTraining ? TEXT("LIVE MATCH") : TEXT("LIVE TRAINING / RECORDING");
    if (Game->IsReplayComplete()) return TEXT("BRANCH SAVED");
    const bool Paused = Game->bReplayPaused || bBattlePaused;
    if (Game->GetReplayOwner() >= 0)
        return FString::Printf(TEXT("P%d TAKEOVER  /  %s"), Game->GetReplayOwner() + 1,
            Paused ? TEXT("PAUSED") : TEXT("RECORDING BRANCH"));
    if (Game->GetReplaySource() && Game->GetReplayPosition() >= Game->GetReplaySource()->LengthInFrames)
        return TEXT("SOURCE END  /  TAKEOVER AVAILABLE");
    return Paused ? TEXT("PLAYBACK PAUSED") : TEXT("PLAYBACK");
}

void UNightSkyReplayWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    RefreshTime += DeltaTime;
    if (RefreshTime >= 0.1f)
    {
        RefreshTime = 0;
        Refresh();
    }
}

void UNightSkyReplayWidget::RefreshSlots(UNightSkyGameInstance* Game)
{
    if (!Game) return;
    const FString Previous = Slots.IsValidIndex(SelectedSlot) ? Slots[SelectedSlot] : FString();
    Slots = Game->GetSavedReplaySlots();
    SelectedSlot = FMath::Max(0, Slots.IndexOfByKey(Previous));
}

void UNightSkyReplayWidget::Refresh()
{
    auto* Game = Cast<UNightSkyGameInstance>(GetGameInstance());
    if (!Game || !StatusText) return;
    const auto* Battle = GetWorld()->GetGameState<ANightSkyGameState>();
    StatusText->SetText(FText::FromString(TEXT("REPLAY LAB   /   ") + DescribeState(Game, Battle && Battle->bPauseGame)
        + TEXT("                       [Tab] help")));
    auto* Source = Game->GetReplaySource();
    const int32 Length = Source ? Source->LengthInFrames : 0;
    const int32 Position = Game->IsReplay ? Game->GetReplayPosition() : Length;
    PositionText->SetText(FText::FromString(FString::Printf(TEXT("FRAME %d  /  SOURCE %d    |    %.2f s    |    %s"),
        Position, Length, Position / 60.0, Game->IsReplay ? TEXT("Position = input pairs already played") : TEXT("Play a few seconds, then press R"))));
    Timeline->SetPercent(Length > 0 ? FMath::Clamp(float(Position) / Length, 0.f, 1.f) : 0.f);
    FString Context;
    if (!Game->IsReplay)
        Context = TEXT("Start: play in local training, then R to watch. Seek to a frame, press 1 or 2, then P to play your continuation.");
    else if (Game->IsReplayComplete())
        Context = TEXT("Saved independently: ") + Game->GetSavedBranchSlot() + TEXT(". Press Enter to replay it. The original is unchanged.");
    else if (Game->GetReplayOwner() >= 0)
        Context = FString::Printf(TEXT("P%d uses its normal local combat controls. Seeking is locked. Other side follows the source, then goes neutral. P resumes; Backspace saves."), Game->GetReplayOwner() + 1);
    else
        Context = TEXT("Seeking pauses on the destination. 1 / 2 takes control BEFORE that frame runs. P resumes. Takeover also works at the source end.");
    if (Game->IsReplay && !Game->IsTraining) Context = TEXT("Playback only: takeover requires a local-training recording.");
    ContextText->SetText(FText::FromString(Context));
    LibraryText->SetText(FText::FromString((Slots.IsValidIndex(SelectedSlot)
        ? FString::Printf(TEXT("Library %d/%d: %s"), SelectedSlot + 1, Slots.Num(), *Slots[SelectedSlot])
        : TEXT("Library: no saved replays yet")) + FString(TEXT("    |    P1: keyboard / first controller. P2: second local controller.    |    PIE: Esc stops; Shift+F1 releases mouse."))));
    FeedbackText->SetText(FText::FromString(Feedback));
    FeedbackText->SetVisibility(Feedback.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    Details->SetVisibility(bShowHelp ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UNightSkyReplayWidget::ExecuteCommand(EReplayUICommand Command)
{
    ApplyCommand(Cast<UNightSkyGameInstance>(GetGameInstance()), Command);
    Refresh();
}

bool UNightSkyReplayWidget::ApplyCommand(UNightSkyGameInstance* Game, EReplayUICommand Command)
{
    if (!Game) return false;
    Feedback.Empty();
    if (Command == EReplayUICommand::ToggleHelp) { bShowHelp = !bShowHelp; return true; }
    if (!Game->GetWorld() || Game->GetWorld()->GetNetMode() != NM_Standalone)
    { Feedback = TEXT("Replay controls are available in local standalone training."); return false; }
    if (Command == EReplayUICommand::PreviousSlot || Command == EReplayUICommand::NextSlot)
    {
        RefreshSlots(Game);
        if (Slots.IsEmpty()) { Feedback = TEXT("No saved replay found. Play training, then R to watch your recording."); return false; }
        SelectedSlot = (SelectedSlot + (Command == EReplayUICommand::NextSlot ? 1 : Slots.Num() - 1)) % Slots.Num();
        return true;
    }
    if (Command == EReplayUICommand::LoadSlot || Command == EReplayUICommand::WatchRecording)
    {
        if (Game->IsReplay && Game->GetReplayOwner() >= 0 && !Game->IsReplayComplete())
        { Feedback = TEXT("Finish and save this branch with Backspace before loading another replay."); return false; }
        UReplaySaveInfo* Source = nullptr;
        if (Command == EReplayUICommand::WatchRecording)
        {
            if (Game->IsReplay) { Feedback = TEXT("Already watching a replay. Home returns to frame 0 before takeover."); return false; }
            if (!Game->IsTraining || !Game->GetReplaySource() || Game->GetReplaySource()->LengthInFrames == 0)
            { Feedback = TEXT("First play a few seconds in local training to record some inputs."); return false; }
            Source = DuplicateObject<UReplaySaveInfo>(Game->GetReplaySource(), Game);
        }
        else
        {
            RefreshSlots(Game);
            if (!Slots.IsValidIndex(SelectedSlot)) { Feedback = TEXT("No saved replay selected. R watches your current training recording."); return false; }
            Source = Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(Slots[SelectedSlot], 0));
        }
        // Stage presentation is world-owned; do not silently play a recording in a different arena.
        if (!Source || Source->BattleData.Stage != Game->BattleData.Stage)
        { Feedback = TEXT("Cannot load here: open the recording's training stage first, or choose another replay."); return false; }
        if (!Game->BeginReplaySession(Source))
        { Feedback = TEXT("Replay rejected: incompatible version, missing assets or invalid input data."); return false; }
        Game->PauseReplay(true);
        Feedback = TEXT("Loaded at frame 0, paused. P plays; 1 / 2 takes over; comma / period seeks one frame.");
        return true;
    }
    if (!Game->IsReplay || !Game->GetReplaySource())
    { Feedback = TEXT("No replay loaded. Press R to watch this recording, or Enter to load a saved replay."); return false; }
    if (Command == EReplayUICommand::Finish)
    {
        if (Game->GetReplayOwner() < 0) { Feedback = TEXT("Take over P1 or P2 first; only a branch can be saved."); return false; }
        if (!Game->FinishReplayBranch()) { Feedback = TEXT("Save failed. The branch is retained; press Backspace to retry."); return false; }
        RefreshSlots(Game);
        SelectedSlot = FMath::Max(0, Slots.IndexOfByKey(Game->GetSavedBranchSlot()));
        Feedback = TEXT("Saved: ") + Game->GetSavedBranchSlot() + TEXT(". Enter plays this independent branch.");
        return true;
    }
    if (Game->IsReplayComplete())
    { Feedback = TEXT("Branch finished. Press Enter to load a replay and begin a new session."); return false; }
    if (Command == EReplayUICommand::Pause)
    {
        auto* Battle = Game->GetWorld()->GetGameState<ANightSkyGameState>();
        const bool bWasPaused = Game->bReplayPaused || (Battle && Battle->bPauseGame);
        if (Battle) Battle->SetPaused(false);
        Game->PauseReplay(!bWasPaused);
        return true;
    }
    if (Command == EReplayUICommand::TakeP1 || Command == EReplayUICommand::TakeP2)
    {
        const int32 Side = Command == EReplayUICommand::TakeP1 ? 0 : 1;
        if (!Game->TakeOverReplay(Side))
        { Feedback = TEXT("Takeover unavailable: use a local-training replay before any previous takeover."); return false; }
        Feedback = FString::Printf(TEXT("P%d owns frame %d onward. %s"), Side + 1, Game->GetReplayPosition(),
            Game->bReplayPaused ? TEXT("Paused: press P when ready.") : TEXT("Branch is recording. Backspace finishes and saves."));
        return true;
    }
    if (Game->GetReplayOwner() >= 0)
    { Feedback = TEXT("Seeking is locked after takeover. Finish and save, then load a replay to seek again."); return false; }
    int32 Target = Game->GetReplayPosition();
    switch (Command)
    {
        case EReplayUICommand::BackFrame: --Target; break;
        case EReplayUICommand::ForwardFrame: ++Target; break;
        case EReplayUICommand::BackSecond: Target -= 60; break;
        case EReplayUICommand::ForwardSecond: Target += 60; break;
        case EReplayUICommand::Start: Target = 0; break;
        case EReplayUICommand::End: Target = Game->GetReplaySource()->LengthInFrames; break;
        default: return false;
    }
    Target = FMath::Clamp(Target, 0, Game->GetReplaySource()->LengthInFrames);
    if (!Game->SeekReplay(Target)) { Feedback = TEXT("Cannot seek in this session."); return false; }
    Game->PauseReplay(true);
    Feedback = FString::Printf(TEXT("Paused at frame %d. P plays; 1 / 2 takes over before this frame."), Target);
    return true;
}

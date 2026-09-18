#include "RoomPanel.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "Engine/TextureRenderTarget2D.h"

void URoomActionButton::Activate()
{
    if (Panel)
    {
        if (MatchIdentity.IsEmpty())
            Panel->Run(Operation);
        else
            Panel->SelectRetainedMatch(MatchIdentity);
    }
}

TSharedRef<SWidget> URoomPanel::RebuildWidget()
{
    auto *Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    WidgetTree->RootWidget = Scroll;
    auto *Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(Column);
    Playback = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Playback"));
    Column->AddChild(Playback);
    Status = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RoomStatus"));
    Column->AddChild(Status);
    Address = AddTextField(Column, TEXT("Address"), TEXT("Room server address, for example 127.0.0.1:7777"));
    Identity = AddTextField(Column, TEXT("Identity"), TEXT("Room identity"));
    Secret = AddTextField(Column, TEXT("Secret"), TEXT("Room secret"));
    Secret->SetIsPassword(true);
    AuthoritySlot = AddTextField(Column, TEXT("Slot"), TEXT("Authority save slot"));
    Delay = AddTextField(Column, TEXT("Delay"), TEXT("Spectator delay, 0 to 600 ticks"));
    Value = AddTextField(Column, TEXT("Value"), TEXT("Character, stage or queued viewer"));
    Frame = AddTextField(Column, TEXT("Frame"), TEXT("Frame or seat number"));
    Match = AddTextField(Column, TEXT("Match"), TEXT("Match identity"));
    RetainedMatchList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RetainedMatches"));
    Column->AddChild(RetainedMatchList);
    ShownMatches.Empty();
    for (const TCHAR *Operation :
         {TEXT("recover"),       TEXT("create"),        TEXT("host"),         TEXT("connect"),      TEXT("authenticate"),
          TEXT("character"),     TEXT("stage"),        TEXT("lock"),         TEXT("unlock"),
          TEXT("ready"),         TEXT("start"),        TEXT("combat-pause"), TEXT("combat-resume"),
          TEXT("queue"),         TEXT("withdraw"),     TEXT("offer"),        TEXT("accept"),
          TEXT("decline"),       TEXT("pause"),        TEXT("seek"),         TEXT("resume"),
          TEXT("live"),          TEXT("select-match"), TEXT("export"),       TEXT("leave"),
          TEXT("retry-content"), TEXT("list-replays"), TEXT("play-replay")})
    {
        AddActionButton(Column, Operation);
    }
    return Super::RebuildWidget();
}

void URoomPanel::NativeConstruct()
{
    Super::NativeConstruct();
    if (auto *Room = GetGameInstance()->GetSubsystem<USpectatorRoom>())
    {
        Identity->SetText(FText::FromString(Room->LoginIdentity));
        Secret->SetText(FText::FromString(Room->LoginSecret));
    }
    if (Connection)
    {
        Connection->OnDelivery.AddDynamic(this, &URoomPanel::UpdateDelivery);
        UpdateDelivery(Connection->LastDelivery);
    }
}

void URoomPanel::NativeDestruct()
{
    if (Connection)
        Connection->OnDelivery.RemoveDynamic(this, &URoomPanel::UpdateDelivery);
    Super::NativeDestruct();
}

void URoomPanel::Run(const FString &Operation)
{
    if (!Connection || !Value || !Frame || !Match)
        return;
    if (Operation == TEXT("recover"))
    {
        FRoomDelivery Delivery;
        Delivery.Status = GetWorld()->GetNetMode() != NM_Client &&
            GetGameInstance()->GetSubsystem<USpectatorRoom>()->Recover(AuthoritySlot->GetText().ToString())
                ? TEXT("recovered; authenticate to join") : TEXT("room recovery rejected");
        UpdateDelivery(Delivery);
        return;
    }
    if (Operation == TEXT("create"))
    {
        CreateRoomFromControls();
        return;
    }
    if (Operation == TEXT("host"))
    {
        HostRoomFromControls();
        return;
    }
    if (Operation == TEXT("connect"))
    {
        ConnectFromControls();
        return;
    }
    if (Operation == TEXT("list-replays") || Operation == TEXT("play-replay"))
    {
        RunReplayControl(Operation);
        return;
    }
    if (Operation == TEXT("authenticate") || Operation == TEXT("retry-content"))
    {
        AuthenticateFromControls();
        return;
    }
    FRoomCommand Request;
    Request.Operation = Operation;
    Request.Value = Value->GetText().ToString();
    Request.Match = Match->GetText().ToString();
    Request.Number = FCString::Atoi(*Frame->GetText().ToString());
    Request.Nonce = FGuid::NewGuid().ToString();
    Request.Assignment = (Operation == TEXT("accept") || Operation == TEXT("decline"))
                             ? Connection->LastDelivery.Offer
                             : Connection->LastDelivery.Assignment;
    Connection->Submit(Request);
}

void URoomPanel::SelectRetainedMatch(const FString &RetainedMatchId)
{
    if (!Connection || !Match || !Connection->LastDelivery.RetainedMatches.Contains(RetainedMatchId))
        return;
    Match->SetText(FText::FromString(RetainedMatchId));
    Run(TEXT("select-match"));
}

void URoomPanel::UpdateDelivery(const FRoomDelivery &Delivery)
{
    if (!Status)
        return;
    if (RetainedMatchList && ShownMatches != Delivery.RetainedMatches)
    {
        RetainedMatchList->ClearChildren();
        ShownMatches = Delivery.RetainedMatches;
        for (const FString &RetainedMatchId : ShownMatches)
        {
            auto *Button = WidgetTree->ConstructWidget<URoomActionButton>();
            Button->Panel = this;
            Button->MatchIdentity = RetainedMatchId;
            auto *Label = WidgetTree->ConstructWidget<UTextBlock>();
            Label->SetText(FText::FromString(TEXT("Select match ") + RetainedMatchId));
            Button->AddChild(Label);
            Button->OnClicked.AddDynamic(Button, &URoomActionButton::Activate);
            RetainedMatchList->AddChild(Button);
        }
    }
    if (Playback)
    {
        FSlateBrush Brush;
        if (auto *Texture = GetGameInstance()->GetSubsystem<USpectatorRoom>()->PresentationTexture())
        {
            Brush.SetResourceObject(Texture);
            Brush.ImageSize = FVector2D(960, 540);
        }
        Playback->SetBrush(Brush);
    }
    // Command acknowledgements can replace Observe's status, so report playback
    // availability from the already released delivery as well as the action result.
    const bool IntegrityError = Delivery.Status.Contains(TEXT("integrity error"));
    const bool ReleasedGameplay = !IntegrityError && Delivery.Frame >= 0 &&
                                  Delivery.Frame <= Delivery.Edge && !Delivery.Gameplay.State.IsEmpty();
    FString ActionStatus = Delivery.Status;
    // "pending" is the export response while completion or delay release is outstanding.
    if (ActionStatus == TEXT("pending"))
        ActionStatus = TEXT("Replay export pending: waiting for match completion and spectator delay");
    else if (ActionStatus == TEXT("exported"))
        ActionStatus = TEXT("Replay exported to saved replays");
    FString Summary =
        FString::Printf(TEXT("%s | room %s | match %s\n%s: %d / %d | %s\nSelections %s | combat %s | %s"),
                        *Delivery.Role,
                        *Delivery.Room,
                        *Delivery.Match,
                        *Delivery.Mode,
                        Delivery.Frame,
                        Delivery.Edge,
                        *ActionStatus,
                        Delivery.Locked ? TEXT("locked") : TEXT("open"),
                        Delivery.Paused ? TEXT("paused") : TEXT("running"),
                        *Delivery.Outcome);
    if (ReleasedGameplay)
    {
        // Retention removes whole matches; each delivered journal starts at frame zero.
        Summary += FString::Printf(TEXT("\nHistory start: 0 | released edge: %d"), Delivery.Edge);
        Summary += TEXT("\nPlayback: released gameplay available");
    }
    else if (!Delivery.Match.IsEmpty() && Delivery.Edge < 0 &&
             !Delivery.Status.StartsWith(TEXT("content:")) && !IntegrityError &&
             Delivery.Status != TEXT("history unavailable"))
        Summary += TEXT("\nPlayback: buffering; released gameplay is not available");
    else
        Summary += TEXT("\nPlayback: gameplay unavailable");
    Summary += IntegrityError ? TEXT(" | integrity error") : TEXT(" | no integrity error reported");
    Summary += Delivery.Recovering ? TEXT("\nRecovery: pending durable room restoration")
                                  : TEXT("\nRecovery: no recovery pending");
    Summary += TEXT("\nStage: ") + Delivery.SelectedStage + TEXT("\nCharacters: ") +
               FString::Join(Delivery.SelectedCharacters, TEXT(", "));
    if (auto *Battle = GetGameInstance()->GetSubsystem<USpectatorRoom>()->PresentationBattle();
        ReleasedGameplay && Battle && Battle->BattleState.FrameNumber == Delivery.Frame &&
        (Delivery.Status == TEXT("ok") || Delivery.Status == TEXT("accepted") ||
         Delivery.Status == TEXT("pending") || Delivery.Status == TEXT("exported")))
        Summary +=
            FString::Printf(TEXT("\nLocal presentation frame %d | P1 HP %d / P2 HP %d | meter %d / %d | timer %d"),
                            Battle->BattleState.FrameNumber,
                            Battle->GetMainPlayer(true)->CurrentHealth,
                            Battle->GetMainPlayer(false)->CurrentHealth,
                            Battle->BattleState.Meter[0],
                            Battle->BattleState.Meter[1],
                            Battle->BattleState.RoundTimer);
    Summary += TEXT("\nMembership: ") + Delivery.Membership + TEXT(" | assignment: ") + Delivery.Assignment;
    Summary += TEXT("\nOffer: ") + Delivery.Offer + TEXT(" | seat: ") + FString::FromInt(Delivery.OfferedSeat);
    Summary += TEXT("\nRoster: ") + FString::Join(Delivery.Roster, TEXT(", "));
    Status->SetText(FText::FromString(Summary));
    if (Match && Match->GetText().IsEmpty())
        Match->SetText(FText::FromString(Delivery.Match));
}

void URoomPanel::CreateRoomFromControls()
{
    int32 Ticks = 0;
    auto *Room = GetGameInstance()->GetSubsystem<USpectatorRoom>();
    const bool Created = GetWorld()->GetNetMode() != NM_Client &&
                         LexTryParseString(Ticks, *Delay->GetText().ToString()) &&
                         Room->Create(AuthoritySlot->GetText().ToString(),
                                      Identity->GetText().ToString(),
                                      Secret->GetText().ToString(),
                                      Ticks);
    FRoomDelivery Delivery;
    Delivery.Status = Created ? TEXT("created; authenticate to join") : TEXT("room creation rejected");
    UpdateDelivery(Delivery);
    return;
}

void URoomPanel::HostRoomFromControls()
{
    FURL URL = GetWorld()->URL;
    URL.AddOption(TEXT("listen"));
    FRoomDelivery Delivery;
    Delivery.Status = GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>()->IsEnabled() &&
                              GetWorld()->GetNetMode() == NM_Standalone && GetWorld()->Listen(URL)
                          ? TEXT("listening")
                          : TEXT("host request rejected");
    UpdateDelivery(Delivery);
    return;
}

void URoomPanel::ConnectFromControls()
{
    auto *Room = GetGameInstance()->GetSubsystem<USpectatorRoom>();
    Room->LoginIdentity = Identity->GetText().ToString();
    Room->LoginSecret = Secret->GetText().ToString();
    if (auto *Controller = Cast<APlayerController>(Connection->GetOwner());
        Controller && !Address->GetText().IsEmpty())
        Controller->ClientTravel(Address->GetText().ToString(), TRAVEL_Absolute);
    return;
}

void URoomPanel::RunReplayControl(const FString &Operation)
{
    auto *Room = GetGameInstance()->GetSubsystem<USpectatorRoom>();
    FRoomDelivery Delivery = Connection->LastDelivery;
    if (Operation == TEXT("list-replays"))
    {
        const auto Replays = Room->SavedReplays();
        Delivery.Status = Replays.IsEmpty() ? TEXT("No saved replays")
                                            : TEXT("Saved replays: ") + FString::Join(Replays, TEXT(", "));
    }
    else
        Delivery.Status = TEXT("Saved replay: ") + Room->PlaySavedReplay(Value->GetText().ToString());
    UpdateDelivery(Delivery);
    return;
}

void URoomPanel::AuthenticateFromControls()
{
    TArray<FString> Revisions;
    if (auto *GameInstance = Cast<UNightSkyGameInstance>(GetGameInstance()))
        for (const auto &Pair :
             Connection->LastDelivery.RequiredContent.IsEmpty()
                 ? USpectatorRoom::InspectContent(GameInstance->BattleData, GameInstance->BattleVersion)
                 : USpectatorRoom::InspectRequiredContent(Connection->LastDelivery.RequiredContent,
                                                          GameInstance->BattleVersion))
        {
            Revisions.Add(Pair.Key);
            Revisions.Add(Pair.Value);
        }
    Connection->Authenticate(Identity->GetText().ToString(), Secret->GetText().ToString(), Revisions);
    return;
}

UEditableTextBox *URoomPanel::AddTextField(UVerticalBox *Column, const TCHAR *Name, const TCHAR *Hint)
{
    auto *Field = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), Name);
    Field->SetHintText(FText::FromString(Hint));
    Column->AddChild(Field);
    return Field;
}

void URoomPanel::AddActionButton(UVerticalBox *Column, const TCHAR *Operation)
{
    auto *Button = WidgetTree->ConstructWidget<URoomActionButton>(
        URoomActionButton::StaticClass(), FName(*(TEXT("Action_") + FString(Operation))));
    Button->Panel = this;
    Button->Operation = Operation;
    auto *Label = WidgetTree->ConstructWidget<UTextBlock>();
    Label->SetText(FText::FromString(Operation));
    Button->AddChild(Label);
    Button->OnClicked.AddDynamic(Button, &URoomActionButton::Activate);
    Column->AddChild(Button);
}

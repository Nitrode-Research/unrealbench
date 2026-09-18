#include "RelayOverlay.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "NightSkyEngine/Miscellaneous/ReplayInfo.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> URelayOverlay::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("RelayHUDTree"));
    auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RelayHUDCanvas"));
    WidgetTree->RootWidget = Canvas;
    BoxRows.Reset();
    for (int32 Index = 0; Index < 32; ++Index)
    {
        auto* Box = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
        Box->SetBrush(*FCoreStyle::Get().GetBrush("WhiteBrush"));
        Box->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* Slot = Canvas->AddChildToCanvas(Box);
        Slot->SetZOrder(0);
        BoxRows.Add(Box);
    }
    TextRows.Reset();
    for (int32 Index = 0; Index < 32; ++Index)
    {
        auto* Row = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Row->SetVisibility(ESlateVisibility::HitTestInvisible);
        Canvas->AddChildToCanvas(Row)->SetZOrder(1);
        TextRows.Add(Row);
    }
    return Super::RebuildWidget();
}

void URelayOverlay::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    RefreshHUD(Geometry);
}

void URelayOverlay::RefreshHUD(const FGeometry& Geometry)
{
    if (!Battle || !Battle->IsRelayBattle()) return;
    PaintedText.Empty();
    PaintedFrame = Battle->BattleState.FrameNumber;
    const float Width=Geometry.GetLocalSize().X, Height=Geometry.GetLocalSize().Y;
    const float PanelWidth=FMath::Min(580.f,Width*.39f);
    int32 BoxIndex = 0;
    auto Box=[&](float X,float Y,float W,float H,FLinearColor Color)
    {
        if (!BoxRows.IsValidIndex(BoxIndex)) return;
        auto* Image = BoxRows[BoxIndex++].Get();
        Image->SetColorAndOpacity(Color);
        Image->SetVisibility(ESlateVisibility::HitTestInvisible);
        if (auto* Slot = Cast<UCanvasPanelSlot>(Image->Slot))
        {
            Slot->SetPosition(FVector2D(X,Y));
            Slot->SetSize(FVector2D(W,H));
        }
    };
    int32 TextIndex = 0;
    auto Text=[&](const FString& Value,float X,float Y,int32 Size,FLinearColor Color)
    {
        // These are the visible UMG labels, so UI automation and accessibility
        // observe the same text that the player sees over the painted panels.
        PaintedText.Add(Value);
        if (!TextRows.IsValidIndex(TextIndex)) return;
        auto* Row = TextRows[TextIndex++].Get();
        Row->SetText(FText::FromString(Value));
        Row->SetFont(FCoreStyle::GetDefaultFontStyle("Bold",Size));
        Row->SetColorAndOpacity(FSlateColor(Color));
        Row->SetVisibility(ESlateVisibility::HitTestInvisible);
        if (auto* Slot = Cast<UCanvasPanelSlot>(Row->Slot))
        {
            Slot->SetPosition(FVector2D(X,Y));
            Slot->SetSize(FVector2D(Width-X,50));
        }
    };
    const auto Rejections=StaticEnum<ERelayRejection>();
    const TCHAR* PhaseNames[]={TEXT("READY"),TEXT("ENTRY"),TEXT("SYNCHRONIZED"),TEXT("ROUTE NOW"),TEXT("FOLLOW-UP"),TEXT("EXIT")};
    const int32 Durations[]={0,6,12,10,12,6};
    for (int32 Side=0;Side<2;++Side)
    {
        const float X=Side==0?24:Width-PanelWidth-24;
        const FLinearColor Color=Side==0?FLinearColor(.18f,.8f,1.f):FLinearColor(1.f,.35f,.65f);
        Box(X,16,PanelWidth,282,FLinearColor(.014f,.022f,.045f,.93f));
        Box(X,16,PanelWidth,4,Color);
        const auto Status=Battle->GetRelayStatus(Side==0);
        Text(FString::Printf(TEXT("TEAM %d   RELAY %d/200   %s"),Side+1,Status.Resource,
            *Rejections->GetNameStringByValue(int64(Status.Rejection))),X+12,28,20,Color);
        const int32 Phase=int32(Status.Stage);
        Text(FString::Printf(TEXT("%s  %d / %d frames"),PhaseNames[Phase],Status.ElapsedFrames,Durations[Phase]),X+12,57,17,
            Status.Stage==ERelayPhase::Route?FLinearColor(1.f,.8f,.2f):FLinearColor::White);
        const auto Fighters=Battle->GetTeam(Side==0);
        for (const auto& Slot:Battle->GetRelaySlots(Side==0))
        {
            const float Y=88+(Slot.Slot-1)*67;
            Text(FString::Printf(TEXT("%d %s  %s"),Slot.Slot,*Slot.Identity,
                Slot.Health==0?TEXT("KO"):Slot.Main?TEXT("MAIN"):Slot.Visible?TEXT("EXPOSED"):TEXT("RESERVE")),
                X+12,Y,18,Slot.Health==0?FLinearColor::Gray:FLinearColor::White);
            Text(FString::Printf(TEXT("HP %d  Recover %d  CD %d  %s"),Slot.Health,Slot.Recoverable,Slot.Cooldown,
                Slot.Eligible?TEXT("READY"):TEXT("")),X+12,Y+23,16,Slot.Eligible?Color:FLinearColor(.75f,.8f,.88f));
            const float MaxHealth=Fighters.IsValidIndex(Slot.Slot-1)?Fighters[Slot.Slot-1]->MaxHealth:1;
            const float BarWidth=PanelWidth-24;
            Box(X+12,Y+49,BarWidth,5,FLinearColor(.09f,.12f,.17f));
            Box(X+12,Y+49,BarWidth*FMath::Clamp((Slot.Health+Slot.Recoverable)/MaxHealth,0.f,1.f),5,FLinearColor(1.f,.72f,.19f));
            Box(X+12,Y+49,BarWidth*FMath::Clamp(Slot.Health/MaxHealth,0.f,1.f),5,Color);
        }
    }
    FString Center=FString::Printf(TEXT("%02d"),FMath::Max(0,(Battle->BattleState.RoundTimer+59)/60));
    Text(Center,Width*.5f-30,22,36,FLinearColor::White);
    const bool Frozen=Battle->BattleState.SuperFreezeDuration>0||Battle->BattleState.SuperFreezeSelfDuration>0;
    if (Frozen) Text(TEXT("FREEZE"),Width*.5f-42,75,18,FLinearColor(1.f,.8f,.2f));
    else if (Battle->bPauseGame) Text(TEXT("PAUSED"),Width*.5f-42,75,18,FLinearColor::White);
    if (Battle->BattleState.CurrentWinSide!=WIN_None)
    {
        const FString Result=Battle->BattleState.CurrentWinSide==WIN_Draw?TEXT("DRAW"):
            Battle->BattleState.CurrentWinSide==WIN_P1?TEXT("TEAM 1 WINS"):TEXT("TEAM 2 WINS");
        Box(Width*.5f-230,Height*.44f,460,80,FLinearColor(.015f,.025f,.05f,.95f));
        Text(Result+TEXT("  /  R REMATCH"),Width*.5f-205,Height*.44f+20,24,FLinearColor(1.f,.83f,.4f));
    }
    auto* GI=Battle->GameInstance;
    if (GI && GI->bRelayArenaControls)
    {
        Box(24,Height-126,Width-48,110,FLinearColor(.014f,.022f,.045f,.94f));
        Text(TEXT("P1: Left/Right move + block   A strike   S throw   D burst   1/2/3 relay     |     P2: J/L move + block   U strike   I throw   O burst   7/8/9 relay"),36,Height-118,16,FLinearColor::White);
        Text(TEXT("Press an eligible reserve, RELEASE, then press a reserve in ROUTE to hand off.    Tab teams   P pause   T training reset   R rematch   V save   B replay"),36,Height-92,16,FLinearColor(.55f,.85f,1.f));
        FString Notice=GI->RelayPresentationNotice;
        if (GI->IsReplay && GI->GetCurrentReplay())
            Notice=FString::Printf(TEXT("REPLAY  %d / %d input pairs   |   %s"),Battle->LocalFrame,GI->GetCurrentReplay()->LengthInFrames,
                Battle->LocalFrame>=GI->GetCurrentReplay()->LengthInFrames?TEXT("END - R starts a live match"):TEXT("P pauses; R starts a live match"));
        Text(Notice,36,Height-64,16,FLinearColor(1.f,.8f,.35f));
        Text(TEXT("Resource: 100 per relay, 200 per round, no regeneration. Gold health recovers only off-screen. Training allows KOs and match results."),36,Height-39,15,FLinearColor(.75f,.8f,.88f));
    }
    for (; TextIndex < TextRows.Num(); ++TextIndex) TextRows[TextIndex]->SetVisibility(ESlateVisibility::Collapsed);
    for (; BoxIndex < BoxRows.Num(); ++BoxIndex) BoxRows[BoxIndex]->SetVisibility(ESlateVisibility::Collapsed);
}

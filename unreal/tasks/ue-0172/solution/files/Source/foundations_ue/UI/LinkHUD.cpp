#include "UI/LinkHUD.h"
#include "UI/LinkWorldHints.h"
#include "Gameplay/LinkPlayerController.h"
#include "Gameplay/LinkInteractionActor.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"

void SLinkHUD::Construct(const FArguments& Args)
{
    Owner=Args._Owner;
    auto Font=[](int32 Size){return FCoreStyle::GetDefaultFontStyle("Regular",Size);};
    auto Inventory=[this,Font](int32 Index,const FLinearColor& Color)
    {
        return SNew(SBorder).BorderImage(&Card).Padding(FMargin(16,10))[
            SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Index==0?TEXT("LT  /  LEFT MOUSE"):TEXT("RT  /  RIGHT MOUSE"))).Font(Font(10)).ColorAndOpacity(Color)]
            +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(STextBlock).Text_Lambda([this,Index](){return FText::FromString(Owner.IsValid()?Owner->InventoryLabel(Index):FString());}).Font(Font(16)).ColorAndOpacity(FLinearColor::White)]
        ];
    };
    ChildSlot[
        SNew(SOverlay)
        +SOverlay::Slot()[SNew(SLinkWorldHints).Owner(Owner.Get())]
        +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(24)[SNew(SBorder).BorderImage(&Card).Padding(FMargin(14,10))[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("SOLOMON'S LINK"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",25)).ColorAndOpacity(FLinearColor(0.94f,0.91f,0.83f))]
            +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(STextBlock).Text(FText::FromString(TEXT("Stay together. Find a way through."))).Font(Font(12)).ColorAndOpacity(FLinearColor(0.72f,0.79f,0.82f))]
        ]]
        +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(24)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[Inventory(0,FLinearColor(0.2f,0.8f,0.88f))]
            +SHorizontalBox::Slot().AutoWidth().Padding(10,0)[Inventory(1,FLinearColor(1,0.58f,0.2f))]
        ]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0,0,0,105)[SNew(STextBlock)
            .Text_Lambda([this](){return FText::FromString(Owner.IsValid()?Owner->HoverLabel():FString());}).Font(Font(13)).ColorAndOpacity(FLinearColor::White)]
        +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(24)[SNew(STextBlock).Text_Lambda([this](){return FText::FromString(TEXT("HOLD BOTH  ·  STAY CLOSE\nESC  ·  PAUSE / CLOSE\n")+(Owner.IsValid()?Owner->AudioLabel():FString()));}).Font(Font(10)).ColorAndOpacity(FLinearColor(0.73f,0.79f,0.83f))]
        +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(24)[SNew(SBox).WidthOverride(330)
            .Visibility_Lambda([this](){return Owner.IsValid()&&(Owner->GetActiveInteraction()||Owner->IsPaused())?EVisibility::Visible:EVisibility::Collapsed;})[
            SNew(SBorder).BorderImage(&Card).Padding(20)[SAssignNew(Actions,SVerticalBox)]
        ]]
    ];
}
void SLinkHUD::Tick(const FGeometry& Geometry,double Time,float DeltaTime)
{
    SCompoundWidget::Tick(Geometry,Time,DeltaTime);
    if(Owner.IsValid()&&LastRevision!=Owner->GetViewRevision()){LastRevision=Owner->GetViewRevision();RebuildActions();}
}
void SLinkHUD::RebuildActions()
{
    Actions->ClearChildren();if(!Owner.IsValid()){return;}
    const FString Title=Owner->IsPaused()?(Owner->IsRunCompleted()?TEXT("A way through"):TEXT("Paused")):Owner->GetActiveInteraction()?Owner->GetActiveInteraction()->DisplayName:FString();
    Actions->AddSlot().AutoHeight().Padding(0,0,0,12)[SNew(STextBlock).Text(FText::FromString(Title)).Font(FCoreStyle::GetDefaultFontStyle("Bold",21)).ColorAndOpacity(FLinearColor::White)];
    if(!Owner->GetFeedback().IsEmpty())
    {
        Actions->AddSlot().AutoHeight().Padding(0,0,0,12)[SNew(STextBlock).Text(FText::FromString(Owner->GetFeedback())).AutoWrapText(true).ColorAndOpacity(FLinearColor(0.5f,0.85f,0.8f))];
    }
    for(int32 Id:Owner->GetActionChoices())
    {
        Actions->AddSlot().AutoHeight().Padding(0,4)[SNew(SButton).ContentPadding(FMargin(12,10)).ButtonColorAndOpacity(FLinearColor(0.08f,0.15f,0.19f))
            .OnClicked_Lambda([this,Id](){if(Owner.IsValid()){Owner->ChooseAction(Id);}return FReply::Handled();})[
            SNew(STextBlock).Text(FText::FromString(Owner->ActionLabel(Id))).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).ColorAndOpacity(FLinearColor::White)]];
    }
    if(Owner->IsPaused())
    {
        Actions->AddSlot().AutoHeight().Padding(0,4)[SNew(SButton).ContentPadding(FMargin(12,10))
            .OnClicked_Lambda([this](){if(Owner.IsValid()){Owner->RestartRun();}return FReply::Handled();})[
            SNew(STextBlock).Text(FText::FromString(TEXT("Start a new run")))]];
        for(bool bSave:{true,false})
        {
            Actions->AddSlot().AutoHeight().Padding(0,4)[SNew(SButton).ContentPadding(FMargin(12,10))
                .OnClicked_Lambda([this,bSave](){if(Owner.IsValid()){if(bSave){Owner->QuickSave();}else{Owner->QuickLoad();}}return FReply::Handled();})[
                SNew(STextBlock).Text(FText::FromString(bSave?TEXT("Save game  ·  F5"):TEXT("Load game  ·  F9")))]];
        }
    }
    Actions->AddSlot().AutoHeight().Padding(0,14,0,0)[SNew(SButton).ContentPadding(FMargin(12,8)).ButtonColorAndOpacity(FLinearColor(0.12f,0.17f,0.20f))
        .OnClicked_Lambda([this](){if(Owner.IsValid()){Owner->TogglePauseOrClose();}return FReply::Handled();})[
        SNew(STextBlock).Text(FText::FromString(Owner->IsPaused()?(Owner->IsRunCompleted()?TEXT("Explore the yard"):TEXT("Resume")):TEXT("Back to the yard"))).ColorAndOpacity(FLinearColor::White)]];
}

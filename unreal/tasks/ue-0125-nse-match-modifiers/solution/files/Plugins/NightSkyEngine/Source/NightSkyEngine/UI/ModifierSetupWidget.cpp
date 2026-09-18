#include "ModifierSetupWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/PlayerController.h"

namespace
{
 TSharedRef<STextBlock> Text(const FString& S, int32 Size=14, FLinearColor C=FLinearColor(.84,.90,.98))
 { return SNew(STextBlock).Text(FText::FromString(S)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).ColorAndOpacity(C).AutoWrapText(true); }
 TSharedRef<SWidget> Button(const FString& S, TFunction<void()> Fn)
 { return SNew(SButton).ContentPadding(FMargin(10,6)).OnClicked_Lambda([Fn]{Fn();return FReply::Handled();})[Text(S)]; }
 TSharedRef<SWidget> Number(const FString& Label, TFunction<int32()> Get, TFunction<void(int32)> Set)
 { return SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Text(Label,11)]+SVerticalBox::Slot().AutoHeight()[SNew(SBox).WidthOverride(104)[SNew(SSpinBox<int32>).MinValue(TOptional<int32>()).MaxValue(TOptional<int32>()).MinSliderValue(TOptional<int32>()).MaxSliderValue(TOptional<int32>()).Value_Lambda([Get]{return Get();}).OnValueChanged_Lambda([Set](int32 V){Set(V);})]]; }
 TSharedRef<SWidget> Check(const FString& S, TFunction<bool()> Get,TFunction<void(bool)> Set)
 {return SNew(SCheckBox).IsChecked_Lambda([Get]{return Get()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([Set](ECheckBoxState V){Set(V==ECheckBoxState::Checked);})[Text(S)];}
 const TCHAR* OpName(EModifierOperation Op)
 {switch(Op){case EModifierOperation::Add:return TEXT("ADD");case EModifierOperation::Multiply:return TEXT("MULTIPLY");case EModifierOperation::Cancel:return TEXT("CANCEL");case EModifierOperation::ChildDamage:return TEXT("CHILD DAMAGE");default:return TEXT("CHILD METER");}}
}
void UModifierSetupWidget::ConfigureForMatch(const FModifierConfiguration& C)
{ Candidate=C;EnabledIds.Reset();for(const auto& D:C.Definitions)EnabledIds.Add(D.Identifier);Configured=true;if(RuleList)BuildCards();RefreshSummary(); }
bool UModifierSetupWidget::Enabled(const FString& Id)const
{return EnabledIds.ContainsByPredicate([&](const FString& S){return S.Equals(Id,ESearchCase::CaseSensitive);});}
bool UModifierSetupWidget::SetRuleEnabled(const FString& Id,bool Enable)
{
 if(!Candidate.Definitions.ContainsByPredicate([&](const auto& D){return D.Identifier.Equals(Id,ESearchCase::CaseSensitive);}))return false;
 EnabledIds.RemoveAll([&](const FString& S){return S.Equals(Id,ESearchCase::CaseSensitive);});if(Enable)EnabledIds.Add(Id);RefreshSummary();return true;
}
bool UModifierSetupWidget::SetInterval(int32 I,int32 R,int32 S,int32 D)
{if(!Candidate.Schedule.IsValidIndex(I))return false;auto& V=Candidate.Schedule[I];V.Round=R;V.Start=S;V.Duration=D;RefreshSummary();return true;}
FModifierConfiguration UModifierSetupWidget::GetSelection()const
{FModifierConfiguration C;for(const auto& D:Candidate.Definitions)if(Enabled(D.Identifier))C.Definitions.Add(D);for(const auto& I:Candidate.Schedule)if(Enabled(I.Identifier))C.Schedule.Add(I);return C;}
void UModifierSetupWidget::ShowError(const FString& S){Error=S;if(ErrorLabel)ErrorLabel->SetText(FText::FromString(S));}
FText UModifierSetupWidget::GetSetupError()const{return FText::FromString(Error);}
FText UModifierSetupWidget::GetSetupSummary()const{return SummaryLabel?SummaryLabel->GetText():FText::GetEmpty();}
void UModifierSetupWidget::RefreshSummary()
{
 if(!SummaryLabel)return;
 const auto C=GetSelection();FString S=TEXT("SELECTED SCHEDULE  •  60 playable frames = 1 second\n");
 for(const auto& I:C.Schedule)S+=FString::Printf(TEXT("%s · round %d · start %d · %s\n"),*I.Identifier,I.Round,I.Start,I.Duration==-1?TEXT("round end"):*FString::Printf(TEXT("%d frames"),I.Duration));
 if(C.Definitions.IsEmpty())S+=TEXT("Ordinary match: no modifiers selected.");
 SummaryLabel->SetText(FText::FromString(S));FString Reason;ShowError(C.Validate(Reason)?TEXT(""):Reason);
}
void UModifierSetupWidget::BuildCards()
{
 if(!RuleList)return;RuleList->ClearChildren();
 for(int32 Index=0;Index<Candidate.Definitions.Num();++Index)
 {
  auto Card=SNew(SVerticalBox);const auto& Def=Candidate.Definitions[Index];
  Card->AddSlot().AutoHeight().Padding(0,0,0,8)[Check(Def.DisplayName+TEXT("  [")+Def.Identifier+TEXT("]"),[this,Index]{return Enabled(Candidate.Definitions[Index].Identifier);},[this,Index](bool V){SetRuleEnabled(Candidate.Definitions[Index].Identifier,V);})];
  FString Help=Def.Drain?TEXT("METER  •  Drain both teams before combat each active frame."):Def.ConvertDamage?TEXT("DAMAGE  •  Convert transformed damage into victim meter loss; no health spill or normal meter gains."):Def.RestrictMovement?TEXT("MOVEMENT  •  Block new walk, dash and jump commands; preserve momentum, knockback and attacks."):Def.SuddenDeath?TEXT("VICTORY  •  Positive health damage wins after all events; simultaneous damage draws."):TEXT("CUSTOM RULE  •  Ordered damage or meter event operations.");
  Card->AddSlot().AutoHeight().Padding(0,0,0,8)[Text(Help,12,FLinearColor(.40,.82,.94))];
  auto Toggles=SNew(SHorizontalBox);
  Toggles->AddSlot().AutoWidth().Padding(0,0,14,0)[Number(TEXT("Meter drain / frame"),[this,Index]{return Candidate.Definitions[Index].Drain;},[this,Index](int32 V){Candidate.Definitions[Index].Drain=V;RefreshSummary();})];
  auto Flags=SNew(SVerticalBox);
  Flags->AddSlot().AutoHeight()[Check(TEXT("Convert damage to meter debit"),[this,Index]{return Candidate.Definitions[Index].ConvertDamage;},[this,Index](bool V){Candidate.Definitions[Index].ConvertDamage=V;RefreshSummary();})];
  Flags->AddSlot().AutoHeight()[Check(TEXT("Restrict new movement"),[this,Index]{return Candidate.Definitions[Index].RestrictMovement;},[this,Index](bool V){Candidate.Definitions[Index].RestrictMovement=V;RefreshSummary();})];
  Flags->AddSlot().AutoHeight()[Check(TEXT("Sudden-death victory"),[this,Index]{return Candidate.Definitions[Index].SuddenDeath;},[this,Index](bool V){Candidate.Definitions[Index].SuddenDeath=V;RefreshSummary();})];
  Toggles->AddSlot()[Flags];Card->AddSlot().AutoHeight()[Toggles];
  for(int32 J=0;J<Candidate.Schedule.Num();++J)
  {
   if(!Candidate.Schedule[J].Identifier.Equals(Def.Identifier,ESearchCase::CaseSensitive))continue;
   auto Row=SNew(SHorizontalBox);
   Row->AddSlot().AutoWidth().Padding(0,6,10,6)[Number(TEXT("Round (1+)"),[this,J]{return Candidate.Schedule[J].Round;},[this,J](int32 V){Candidate.Schedule[J].Round=V;RefreshSummary();})];
   Row->AddSlot().AutoWidth().Padding(0,6,10,6)[Number(TEXT("Start frame"),[this,J]{return Candidate.Schedule[J].Start;},[this,J](int32 V){Candidate.Schedule[J].Start=V;RefreshSummary();})];
   Row->AddSlot().AutoWidth().Padding(0,6,10,6)[Number(TEXT("Duration (-1=end)"),[this,J]{return Candidate.Schedule[J].Duration;},[this,J](int32 V){Candidate.Schedule[J].Duration=V;RefreshSummary();})];
   Row->AddSlot().AutoWidth().VAlign(VAlign_Bottom).Padding(0,6)[Button(TEXT("Remove interval"),[this,J]{Candidate.Schedule.RemoveAt(J);BuildCards();RefreshSummary();})];
   Card->AddSlot().AutoHeight()[Row];
  }
  Card->AddSlot().AutoHeight().HAlign(HAlign_Left)[Button(TEXT("+ Activation interval"),[this,Index]{FModifierInterval I;I.Identifier=Candidate.Definitions[Index].Identifier;I.Revision=Candidate.Definitions[Index].Revision;I.Round=1;I.Start=600;I.Duration=120;Candidate.Schedule.Add(I);BuildCards();RefreshSummary();})];
  auto Advanced=SNew(SVerticalBox);
  auto Meta=SNew(SHorizontalBox);
  Meta->AddSlot().FillWidth(1).Padding(0,0,8,0)[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Text(TEXT("Stable ASCII identifier"),11)]+SVerticalBox::Slot().AutoHeight()[SNew(SEditableTextBox).Text(FText::FromString(Def.Identifier)).OnTextCommitted_Lambda([this,Index](const FText& T,ETextCommit::Type){const FString Old=Candidate.Definitions[Index].Identifier,New=T.ToString();for(auto& I:Candidate.Schedule)if(I.Identifier.Equals(Old,ESearchCase::CaseSensitive))I.Identifier=New;bool E=Enabled(Old);EnabledIds.Remove(Old);Candidate.Definitions[Index].Identifier=New;if(E)EnabledIds.Add(New);BuildCards();RefreshSummary();})]];
  Meta->AddSlot().AutoWidth().Padding(0,0,8,0)[Number(TEXT("Revision"),[this,Index]{return Candidate.Definitions[Index].Revision;},[this,Index](int32 V){Candidate.Definitions[Index].Revision=V;for(auto& I:Candidate.Schedule)if(I.Identifier.Equals(Candidate.Definitions[Index].Identifier,ESearchCase::CaseSensitive))I.Revision=V;RefreshSummary();})];
  Meta->AddSlot().AutoWidth()[Number(TEXT("Priority (low first)"),[this,Index]{return Candidate.Definitions[Index].Priority;},[this,Index](int32 V){Candidate.Definitions[Index].Priority=V;RefreshSummary();})];Advanced->AddSlot().AutoHeight()[Meta];
  Advanced->AddSlot().AutoHeight().Padding(0,6)[Text(TEXT("Exclusive groups (comma separated) • overlapping schedules in a group are rejected"),11)];
  Advanced->AddSlot().AutoHeight()[SNew(SEditableTextBox).Text(FText::FromString(FString::Join(Def.ExclusiveGroups,TEXT(",")))).OnTextCommitted_Lambda([this,Index](const FText& T,ETextCommit::Type){auto& G=Candidate.Definitions[Index].ExclusiveGroups;T.ToString().ParseIntoArray(G,TEXT(","),true);for(auto& S:G)S.TrimStartAndEndInline();RefreshSummary();})];
  Advanced->AddSlot().AutoHeight().Padding(0,6)[Button(Def.Subscription==EModifierEvent::Damage?TEXT("Event: DAMAGE  (click for METER)"):TEXT("Event: METER  (click for DAMAGE)"),[this,Index]{auto& D=Candidate.Definitions[Index];D.Subscription=D.Subscription==EModifierEvent::Damage?EModifierEvent::Meter:EModifierEvent::Damage;BuildCards();RefreshSummary();})];
  auto Match=SNew(SHorizontalBox);
  Match->AddSlot().AutoWidth().Padding(0,0,12,0)[Check(TEXT("Only match original incoming amount"),[this,Index]{return Candidate.Definitions[Index].MatchAmount;},[this,Index](bool V){Candidate.Definitions[Index].MatchAmount=V;RefreshSummary();})];
  Match->AddSlot().AutoWidth()[Number(TEXT("Required amount"),[this,Index]{return Candidate.Definitions[Index].RequiredAmount;},[this,Index](int32 V){Candidate.Definitions[Index].RequiredAmount=V;RefreshSummary();})];Advanced->AddSlot().AutoHeight()[Match];
  Advanced->AddSlot().AutoHeight().Padding(0,6)[Text(TEXT("Operations run top to bottom. Multiply floors amount × numerator / denominator. Child events inherit source; cancel discards descendants."),12)];
  for(int32 J=0;J<Def.Operations.Num();++J)
  {
   auto Row=SNew(SHorizontalBox);
   Row->AddSlot().AutoWidth().VAlign(VAlign_Bottom).Padding(0,0,8,0)[Button(OpName(Def.Operations[J].Operation),[this,Index,J]{auto& O=Candidate.Definitions[Index].Operations[J];O.Operation=EModifierOperation((int32(O.Operation)+1)%5);BuildCards();RefreshSummary();})];
   Row->AddSlot().AutoWidth().Padding(0,0,8,0)[Number(TEXT("Amount / numerator"),[this,Index,J]{return Candidate.Definitions[Index].Operations[J].Amount;},[this,Index,J](int32 V){Candidate.Definitions[Index].Operations[J].Amount=V;RefreshSummary();})];
   Row->AddSlot().AutoWidth().Padding(0,0,8,0)[Number(TEXT("Denominator"),[this,Index,J]{return Candidate.Definitions[Index].Operations[J].Denominator;},[this,Index,J](int32 V){Candidate.Definitions[Index].Operations[J].Denominator=V;RefreshSummary();})];
   Row->AddSlot().AutoWidth().VAlign(VAlign_Bottom)[Check(TEXT("Child → source"),[this,Index,J]{return Candidate.Definitions[Index].Operations[J].ToAttacker;},[this,Index,J](bool V){Candidate.Definitions[Index].Operations[J].ToAttacker=V;RefreshSummary();})];
   Row->AddSlot().AutoWidth().VAlign(VAlign_Bottom)[Button(TEXT("−"),[this,Index,J]{Candidate.Definitions[Index].Operations.RemoveAt(J);BuildCards();RefreshSummary();})];Advanced->AddSlot().AutoHeight().Padding(0,3)[Row];
  }
  Advanced->AddSlot().AutoHeight().HAlign(HAlign_Left)[Button(TEXT("+ Event operation"),[this,Index]{Candidate.Definitions[Index].Operations.Add(FModifierOperation{});BuildCards();RefreshSummary();})];
  Advanced->AddSlot().AutoHeight().Padding(0,6)[Text(TEXT("Owned attack state (optional gameplay tag; removed on rule expiry)"),11)];
  Advanced->AddSlot().AutoHeight()[SNew(SEditableTextBox).Text(FText::FromString(Def.OwnedAttack.ToString())).OnTextCommitted_Lambda([this,Index](const FText& T,ETextCommit::Type){const auto Tag=FGameplayTag::RequestGameplayTag(FName(T.ToString()),false);if(!T.IsEmpty()&&!Tag.IsValid()){ShowError(TEXT("Unknown owned attack tag: ")+T.ToString());return;}Candidate.Definitions[Index].OwnedAttack=Tag;RefreshSummary();})];
  Card->AddSlot().AutoHeight().Padding(0,10)[SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[Text(TEXT("AUTHORING  •  damage / meter operations, identity & priority"),13)].BodyContent()[Advanced]];
  RuleList->AddSlot().AutoHeight().Padding(0,0,0,10)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025,.05,.08,1)).Padding(14)[Card]];
 }
}
TSharedRef<SWidget> UModifierSetupWidget::RebuildWidget()
{
 if(!Configured)if(auto* GI=Cast<UNightSkyGameInstance>(GetGameInstance()))ConfigureForMatch(GI->BattleData.Modifiers);
 if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
 SummaryLabel=WidgetTree->ConstructWidget<UTextBlock>();SummaryLabel->SetColorAndOpacity(FSlateColor(FLinearColor(.65,.81,.91)));auto F=SummaryLabel->GetFont();F.Size=12;SummaryLabel->SetFont(F);
 ErrorLabel=WidgetTree->ConstructWidget<UTextBlock>();ErrorLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1,.33,.24)));ErrorLabel->SetAutoWrapText(true);ErrorLabel->SetFont(F);
 RuleList=SNew(SVerticalBox);BuildCards();RefreshSummary();
 auto Actions=SNew(SHorizontalBox);
 Actions->AddSlot().AutoWidth().Padding(0,0,8,0)[Button(TEXT("START LOCAL MATCH"),[this]{StartSelectedMatch();})];
 Actions->AddSlot().AutoWidth().Padding(0,0,8,0)[Button(TEXT("Restore four-rule preset"),[this]{ConfigureForMatch(FModifierConfiguration::FourRulePreset());})];
 Actions->AddSlot().AutoWidth()[Button(TEXT("+ Custom rule"),[this]{FModifierDefinition D;D.Identifier=FString::Printf(TEXT("Custom%d"),Candidate.Definitions.Num());D.DisplayName=TEXT("Custom damage / meter");Candidate.Definitions.Add(D);EnabledIds.Add(D.Identifier);FModifierInterval I;I.Identifier=D.Identifier;Candidate.Schedule.Add(I);BuildCards();RefreshSummary();})];
 auto Network=SNew(SHorizontalBox);
 if(OnNetwork){Network->AddSlot().AutoWidth()[Button(TEXT("HOST LAN"),[this]{StartNetworkMatch(true,Address);})];Network->AddSlot().FillWidth(1).Padding(8,0)[SNew(SEditableTextBox).Text(FText::FromString(Address)).HintText(FText::FromString(TEXT("Host IP:port"))).OnTextChanged_Lambda([this](const FText& T){Address=T.ToString();})];Network->AddSlot().AutoWidth()[Button(TEXT("JOIN LAN"),[this]{StartNetworkMatch(false,Address);})];}
 if(OnReplay)Network->AddSlot().AutoWidth().Padding(8,0)[Button(TEXT("WATCH LAST REPLAY"),[this]{OnReplay();})];
 return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.006,.012,.025,.985)).Padding(FMargin(28,18))
 [SNew(SVerticalBox)
 +SVerticalBox::Slot().AutoHeight()[Text(TEXT("JADE CIRCUIT  /  MATCH MODIFIERS"),26,FLinearColor(.35,.9,1))]
 +SVerticalBox::Slot().AutoHeight().Padding(0,5,0,10)[Text(TEXT("Compose the rules. Preview their schedules. Play the result."),14)]
 +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[RuleList.ToSharedRef()]+SScrollBox::Slot()[SummaryLabel->TakeWidget()]]
 +SVerticalBox::Slot().AutoHeight().Padding(0,6)[ErrorLabel->TakeWidget()]
 +SVerticalBox::Slot().AutoHeight()[Actions]
 +SVerticalBox::Slot().AutoHeight().Padding(0,6)[Network]
 +SVerticalBox::Slot().AutoHeight()[Text(TEXT("P1: arrows move/jump · A strike · A+S alternate · F+arrow dash · A+G knockback · D ends Restriction\nP2: J/L move · K jump · U strike · U+I alternate · O dash · U+H knockback · Y ends Restriction\nTab setup · P pause · R rematch · V save replay · B watch replay · Esc exits PIE · Shift+F1 releases mouse"),12)]
 +SVerticalBox::Slot().AutoHeight().Padding(0,5)[Text(TEXT("Schedule frame 0 is the first playable step. Pause holds the clock; hitstop and superfreeze do not. Online peers must use identical rules and schedules."),11)]];
}
bool UModifierSetupWidget::ValidateSelection(FModifierConfiguration& C)
{
 C=GetSelection();FString Reason;if(!C.Validate(Reason)){ShowError(Reason);return false;}
 auto* GI=Cast<UNightSkyGameInstance>(GetGameInstance());if(!GI){ShowError(TEXT("No active game instance."));return false;}
 if(AllowAuthoring && GetWorld()->GetNetMode()==NM_Standalone)GI->AvailableModifiers=Candidate.Definitions;
 if(!GI->ValidateModifierContent(C,Reason)){ShowError(Reason);return false;}ShowError(TEXT(""));return true;
}
bool UModifierSetupWidget::StartSelectedMatch()
{
 auto* GI=Cast<UNightSkyGameInstance>(GetGameInstance());auto* B=GetWorld()?GetWorld()->GetGameState<ANightSkyGameState>():nullptr;if(!GI||!B)return false;
 if(GetWorld()->GetNetMode()!=NM_Standalone){ShowError(TEXT("Return to local setup before changing online rules."));return false;}
 B->SetPaused(true);FModifierConfiguration C;if(!ValidateSelection(C))return false;
 GI->BattleData.Modifiers=C;GI->IsReplay=false;GI->IsTraining=true;B->MatchInit();GI->IsTraining=false;
 if(!B->MatchModifiers.Accepted){ShowError(B->GetModifierRejection());return false;}
 if(OnAccepted)OnAccepted(C);
 B->SetPaused(false);RemoveFromParent();if(auto* PC=GetOwningPlayer()){PC->SetInputMode(FInputModeGameOnly());PC->bShowMouseCursor=false;}return true;
}

bool UModifierSetupWidget::StartNetworkMatch(bool Host, const FString& HostAddress)
{
 FModifierConfiguration C;
 if(!OnNetwork || !ValidateSelection(C))return false;
 if(!Host && (HostAddress.IsEmpty() || HostAddress.Contains(TEXT("?")) || HostAddress.Contains(TEXT("/")) || HostAddress.Contains(TEXT(" "))))
 {ShowError(TEXT("Enter a host IP or hostname, optionally followed by :port."));return false;}
 OnNetwork(C,Host,HostAddress);return true;
}

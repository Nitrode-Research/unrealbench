#include "ModifierArena.h"
#include "NightSkyEngine/UI/ModifierSetupWidget.h"
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyModifierWidget.h"
#include "NightSkyEngine/Data/BattleExtensionData.h"
#include "NightSkyEngine/Network/NetworkPawn.h"
#include "NightSkyEngine/Miscellaneous/ReplayInfo.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Components/InputComponent.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/CoreStyle.h"
namespace
{
 const FName ArenaMap(TEXT("/Game/ModifierFixture/ModifierArena"));
 UModifierArenaInstance* Instance(const UObject* C){return Cast<UModifierArenaInstance>(UGameplayStatics::GetGameInstance(C));}
 ANightSkyGameState* Battle(const UObject* C){return C&&C->GetWorld()?C->GetWorld()->GetGameState<ANightSkyGameState>():nullptr;}
}
void UModifierArenaInstance::Init()
{
 Super::Init();
 Profile=Cast<UModifierArenaProfile>(UGameplayStatics::LoadGameFromSlot(TEXT("Modifier125_Profile"),0));
 if(!Profile){Profile=NewObject<UModifierArenaProfile>(this);Profile->Authoring=FModifierConfiguration::FourRulePreset();Profile->Selected=Profile->Authoring;}
 AvailableModifiers=Profile->Authoring.Definitions;
 BattleVersion=TEXT("ModifierArena125-1");Prepare();
 if(GEngine)
 {
  GEngine->OnNetworkFailure().AddWeakLambda(this,[this](UWorld*,UNetDriver*,ENetworkFailure::Type,const FString& Reason){Notice=TEXT("Network connection failed: ")+Reason+TEXT(". Tab returns to local setup.");});
  GEngine->OnTravelFailure().AddWeakLambda(this,[this](UWorld*,ETravelFailure::Type,const FString& Reason){Notice=TEXT("Travel failed: ")+Reason+TEXT(". Tab returns to local setup.");});
 }
}
void UModifierArenaInstance::Prepare()
{
 BattleData.PlayerListP1={GetMutableDefault<UModifierArenaCharacter>()};BattleData.PlayerListP2=BattleData.PlayerListP1;
 BattleData.ColorIndicesP1={1};BattleData.ColorIndicesP2={2};BattleData.BattleFormat=EBattleFormat::Rounds;
 BattleData.RoundCount=2;BattleData.StartRoundTimer=99;BattleData.TimeUntilRoundStart=0;
 BattleData.Stage=GetMutableDefault<UModifierArenaStage>();BattleData.Random.Reseed(125125);
 BattleData.Modifiers=Profile->Selected;BattleData.bIsValid=true;IsTraining=true;IsReplay=false;IsCPUBattle=false;
 FighterRunner=Online?Multiplayer:LocalPlay;ModifierPeerAccepted=false;ModifierPeerConfiguration.Reset();ModifierSetupError.Reset();
}
void UModifierArenaInstance::Persist(){if(!UGameplayStatics::SaveGameToSlot(Profile,TEXT("Modifier125_Profile"),0))Notice=TEXT("Could not save authored rules to disk.");}
void UModifierArenaInstance::SaveArenaReplay()
{
 if(IsReplay||!GetRecordedReplay()||GetRecordedReplay()->LengthInFrames<1){Notice=TEXT("Play a match before saving a replay.");return;}
 const FString Slot=TEXT("MOD125_")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S_"))+FGuid::NewGuid().ToString(EGuidFormats::Digits);
 auto* Snapshot=DuplicateObject<UReplaySaveInfo>(GetRecordedReplay(),this);
 if(Online)
 {
  const auto* S=Battle(this);
  Snapshot->LengthInFrames=FMath::Clamp(S?S->GetConfirmedInputFrame()+1:0,0,Snapshot->LengthInFrames);
  Snapshot->InputsP1.SetNum(Snapshot->LengthInFrames);Snapshot->InputsP2.SetNum(Snapshot->LengthInFrames);
  if(Snapshot->LengthInFrames==0){Notice=TEXT("Waiting for confirmed online inputs before saving.");return;}
 }
 if(UGameplayStatics::SaveGameToSlot(Snapshot,Slot,0)){Profile->LastReplay=Slot;Persist();Notice=TEXT("Replay saved. B watches the saved match.");}else Notice=TEXT("Replay save failed; recording is still in memory.");
}
void UModifierArenaInstance::WatchArenaReplay()
{
 if(Online){Notice=TEXT("Return to local setup (Tab) before watching a replay.");return;}
 auto* Tape=Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(Profile->LastReplay,0));
 FString Why;
 if(!Tape||Tape->Version!=BattleVersion||Tape->BattleData.Stage!=GetMutableDefault<UModifierArenaStage>()||Tape->LengthInFrames<1||Tape->InputsP1.Num()!=Tape->LengthInFrames||Tape->InputsP2.Num()!=Tape->LengthInFrames){Notice=TEXT("No compatible arena replay saved. V saves the current match.");return;}
 if(!ValidateModifierContent(Tape->BattleData.Modifiers,Why)){Notice=TEXT("Replay rejected: ")+Why;return;}
 PlayReplayFromBP(Profile->LastReplay);if(!IsReplay){Notice=ModifierSetupError;return;}
 ReplayTravel=true;StartOnTravel=true;Notice=TEXT("REPLAY · P pauses · R returns to live play");UGameplayStatics::OpenLevel(this,ArenaMap);
}
void UModifierArenaInstance::StartNetwork(const FModifierConfiguration& C,bool Host,const FString& Address)
{
 if(!Host&&(Address.IsEmpty()||Address.Contains(TEXT("?"))||Address.Contains(TEXT("/"))||Address.Contains(TEXT(" ")))){Notice=TEXT("Enter a host IP or hostname, optionally followed by :port.");return;}
 if(auto* Second=UGameplayStatics::GetPlayerController(this,1))UGameplayStatics::RemovePlayer(Second,true);
 Profile->Selected=C;Persist();Online=true;StartOnTravel=true;PlayerIndex=Host?0:1;Prepare();
 Notice=Host?TEXT("Hosting on port 7777. Waiting for an identical peer configuration."):TEXT("Connecting; waiting for exact modifier agreement.");
 if(Host)UGameplayStatics::OpenLevel(this,ArenaMap,true,TEXT("listen"));
 else if(auto* PC=GetFirstLocalPlayerController())PC->ClientTravel(Address,TRAVEL_Absolute);
}
void UModifierArenaInstance::ReturnLocal(){Online=false;ReplayTravel=false;StartOnTravel=false;PlayerIndex=0;Prepare();Notice=TEXT("Local authoring restored.");UGameplayStatics::OpenLevel(this,ArenaMap);}
AModifierArenaFighter::AModifierArenaFighter(){MaxHealth=1000;MaxMeter=1000;MeterPercentOnHit=100;MeterPercentOnReceiveHit=100;}
UModifierArenaCharacter::UModifierArenaCharacter(){PlayerClass=AModifierArenaFighter::StaticClass();}
UModifierArenaStage::UModifierArenaStage(){StageName=TEXT("Jade Circuit");StageFriendlyName=FText::FromName(StageName);StageURL=ArenaMap.ToString();}
UModifierArenaRound::UModifierArenaRound(){Name=FGameplayTag::RequestGameplayTag(TEXT("BattleExtension.RoundInit"));}
void UModifierArenaRound::Exec_Implementation(){if(Parent)for(int32 I=0;I<2;++I)Parent->BattleState.Meter[I]=Parent->BattleState.MaxMeter[I];}
AModifierArenaBattle::AModifierArenaBattle()
{
 BattleExtensionData=CreateDefaultSubobject<UBattleExtensionData>(TEXT("ArenaExtensions"));
 BattleExtensionData->ExtensionArray={UModifierArenaRound::StaticClass()};
}
void AModifierArenaBattle::BeginPlay()
{
 Super::BeginPlay();GetWorld()->SpawnActor<AModifierArenaPresentation>();
 if(auto* GI=Instance(this))
 {
  if(GetNetMode()==NM_Standalone&&!UGameplayStatics::GetPlayerController(this,1))UGameplayStatics::CreatePlayer(this,1,true);
  SetPaused(!GI->StartOnTravel&&!GI->IsReplay);GI->IsTraining=false;
 }
}
AModifierArenaGameMode::AModifierArenaGameMode(){GameStateClass=AModifierArenaBattle::StaticClass();PlayerControllerClass=AModifierArenaController::StaticClass();DefaultPawnClass=ANetworkPawn::StaticClass();}
void AModifierArenaGameMode::InitGame(const FString& Map,const FString& Options,FString& Error)
{
 Super::InitGame(Map,Options,Error);if(auto* GI=Instance(this)){if(!GI->ReplayTravel)GI->Prepare();GI->ReplayTravel=false;if(!GI->Online)DefaultPawnClass=nullptr;}
}
AModifierArenaController::AModifierArenaController(){PrimaryActorTick.bTickEvenWhenPaused=true;}
void AModifierArenaController::SetupInputComponent()
{
 Super::SetupInputComponent();if(GetLocalPlayer()&&GetLocalPlayer()->GetControllerId()!=0)return;
 InputComponent->BindKey(EKeys::Tab,IE_Pressed,this,&AModifierArenaController::ToggleSetup);
 InputComponent->BindKey(EKeys::Enter,IE_Pressed,this,&AModifierArenaController::StartSelectedMatch);
 InputComponent->BindKey(EKeys::P,IE_Pressed,this,&AModifierArenaController::TogglePause);
 InputComponent->BindKey(EKeys::R,IE_Pressed,this,&AModifierArenaController::RestartMatch);
 InputComponent->BindKey(EKeys::V,IE_Pressed,this,&AModifierArenaController::SaveMatchReplay);
 InputComponent->BindKey(EKeys::B,IE_Pressed,this,&AModifierArenaController::WatchMatchReplay);
 const TPair<FKey,int32> P1[]={{EKeys::Left,INP_Left},{EKeys::Right,INP_Right},{EKeys::Up,INP_Up},{EKeys::Down,INP_Down},{EKeys::A,INP_A},{EKeys::S,INP_B},{EKeys::D,INP_D},{EKeys::F,INP_E},{EKeys::G,INP_F}};
 const TPair<FKey,int32> P2[]={{EKeys::J,INP_Left},{EKeys::L,INP_Right},{EKeys::K,INP_Up},{EKeys::U,INP_A},{EKeys::I,INP_B},{EKeys::Y,INP_D},{EKeys::O,INP_E},{EKeys::H,INP_F}};
 auto Bind=[&](const auto& Keys,bool Second){for(const auto& Key:Keys)for(bool Held:{true,false}){FInputKeyBinding B(FInputChord(Key.Key),Held?IE_Pressed:IE_Released);B.KeyDelegate.GetDelegateForManualSet().BindUObject(this,&AModifierArenaController::InputBit,Key.Value,Held,Second);InputComponent->KeyBindings.Add(MoveTemp(B));}};Bind(P1,false);Bind(P2,true);
 for(auto& Key:InputComponent->KeyBindings)Key.bExecuteWhenPaused=true;
}
void AModifierArenaController::InputBit(int32 Mask,bool Held,bool Second){auto& Word=Second?SecondWord:Inputs;if(Held)Word|=Mask;else Word&=~Mask;}
void AModifierArenaController::SetInputWord(int32 Word,bool Second){(Second?SecondWord:Inputs)=Word;}
void AModifierArenaController::BeginPlay()
{
 Super::BeginPlay();InitializeLocalUI();
}
void AModifierArenaController::InitializeLocalUI()
{
 if(HUD||!IsLocalController()||!GetLocalPlayer()||GetLocalPlayer()->GetControllerId()!=0)return;
 HUD=CreateWidget<UModifierArenaHUD>(this);HUD->AddToViewport(25);HUD->SetVisibility(ESlateVisibility::HitTestInvisible);
 // Initial menu opens from the controller tick once the battle exists, even while paused.
}
void AModifierArenaController::ToggleSetup()
{
 auto* GI=Instance(this);auto* State=Battle(this);if(!GI||!State)return;
 if(GI->Online||GI->IsReplay){GI->ReturnLocal();return;}
 if(Setup&&Setup->IsInViewport()){Setup->RemoveFromParent();State->SetPaused(false);SetInputMode(FInputModeGameOnly());bShowMouseCursor=false;return;}
 State->SetPaused(true);Inputs=SecondWord=0;Setup=CreateWidget<UModifierSetupWidget>(this);Setup->AllowAuthoring=true;
 Setup->ConfigureForMatch(GI->Profile->Authoring);
 for(const auto& D:GI->Profile->Authoring.Definitions)Setup->SetRuleEnabled(D.Identifier,GI->Profile->Selected.Definitions.ContainsByPredicate([&](const auto& Selected){return Selected.Identifier.Equals(D.Identifier,ESearchCase::CaseSensitive);}));
 Setup->OnAccepted=[this,GI](const FModifierConfiguration& C){GI->Profile->Selected=C;GI->Profile->Authoring=Setup->GetAuthoredConfiguration();GI->Persist();GI->Notice=TEXT("Live match. P pauses · Tab edits rules · V saves a replay.");Inputs=SecondWord=0;};
 Setup->OnNetwork=[this,GI](const FModifierConfiguration& C,bool Host,const FString& Address){GI->Profile->Authoring=Setup->GetAuthoredConfiguration();GI->StartNetwork(C,Host,Address);};
 Setup->OnReplay=[GI]{GI->WatchArenaReplay();};Setup->AddToViewport(100);SetInputMode(FInputModeGameAndUI());bShowMouseCursor=true;
}
void AModifierArenaController::HostSelectedMatch(){if(Setup)Setup->StartNetworkMatch(true,TEXT(""));}
void AModifierArenaController::JoinSelectedMatch(const FString& Address){if(Setup)Setup->StartNetworkMatch(false,Address);}
void AModifierArenaController::StartSelectedMatch(){if(Setup&&Setup->IsInViewport())Setup->StartSelectedMatch();}
void AModifierArenaController::TogglePause(){if(auto* GI=Instance(this);GI&&!GI->Online)if(auto* S=Battle(this))S->SetPaused(!S->bPauseGame);}
void AModifierArenaController::RestartMatch()
{
 auto* GI=Instance(this);if(!GI)return;if(GI->IsReplay){GI->ReturnLocal();return;}if(Setup&&Setup->IsInViewport()){StartSelectedMatch();return;}
 if(GI->Online){Rematch();GI->Notice=TEXT("Rematch requested. Both players must press R.");}else {PairRematch=true;if(auto* S=Battle(this))S->SetPaused(false);}
}
void AModifierArenaController::PostRematch(){Super::PostRematch();PairRematch=false;Inputs&=~INP_Rematch;SecondWord&=~INP_Rematch;}
void AModifierArenaController::SaveMatchReplay(){if(auto* GI=Instance(this))GI->SaveArenaReplay();}
void AModifierArenaController::WatchMatchReplay(){if(auto* GI=Instance(this))GI->WatchArenaReplay();}
void AModifierArenaController::Tick(float Delta)
{
 Super::Tick(Delta);InitializeLocalUI();auto* GI=Instance(this);auto* S=Battle(this);if(!GI||!S||!IsLocalController())return;
 if(HUD&&!Setup&&!GI->StartOnTravel&&!GI->Online&&!GI->IsReplay&&S->Players.Num()==2)ToggleSetup();
 if(GI->Online)
 {
  if(!AgreementSent)for(TActorIterator<ANetworkPawn> It(GetWorld());It;++It)
  {
   if(GI->PlayerIndex==0&&It->GetController()&&!It->IsLocallyControlled()){It->ClientModifierAgreement(GI->BattleData.Modifiers);AgreementSent=true;break;}
   if(GI->PlayerIndex==1&&It->IsLocallyControlled()){It->ServerModifierAgreement(GI->BattleData.Modifiers);AgreementSent=true;break;}
  }
  if(!NetworkStarted&&GI->ModifierPeerAccepted){GI->IsTraining=true;S->MatchInit();GI->IsTraining=false;S->SetPaused(false);NetworkStarted=S->FighterRunner!=nullptr;GI->Notice=TEXT("ONLINE · Rules agreed · P1 controls your fighter · R requests rematch · Tab leaves");}
  return;
 }
 if(GetLocalPlayer()&&GetLocalPlayer()->GetControllerId()!=0)return;
 if(auto* Second=Cast<ANightSkyPlayerController>(UGameplayStatics::GetPlayerController(this,1)))
 {Second->Inputs=SecondWord;if(PairRematch){Inputs|=INP_Rematch;Second->Inputs|=INP_Rematch;}else Inputs&=~INP_Rematch;}
}
TSharedRef<SWidget> UModifierArenaHUD::RebuildWidget()
{
 auto Line=[](TFunction<FString()> Fn,int32 Size,FLinearColor Color){return SNew(STextBlock).Text_Lambda([Fn]{return FText::FromString(Fn());}).Font(FCoreStyle::GetDefaultFontStyle("Bold",Size)).ColorAndOpacity(Color).AutoWrapText(true);};
 auto Top=SNew(SHorizontalBox);
 for(int32 Team=0;Team<2;++Team)
 {
  const FLinearColor Color=Team==0?FLinearColor(.2,.8,1):FLinearColor(1,.3,.65);auto V=SNew(SVerticalBox);
  V->AddSlot().AutoHeight()[Line([this,Team]{auto* S=Battle(this);auto* P=S?S->GetMainPlayer(Team==0):nullptr;return P?FString::Printf(TEXT("PLAYER %d    %d / %d HP    ROUNDS %d"),Team+1,P->CurrentHealth,P->MaxHealth,Team?S->BattleState.P2RoundsWon:S->BattleState.P1RoundsWon):TEXT("Preparing fighters...");},18,Color)];
  V->AddSlot().AutoHeight().Padding(0,5)[SNew(SBox).HeightOverride(15)[SNew(SProgressBar).FillColorAndOpacity(Color).Percent_Lambda([this,Team]{auto* S=Battle(this);auto* P=S?S->GetMainPlayer(Team==0):nullptr;return P?float(P->CurrentHealth)/FMath::Max(1,P->MaxHealth):0.f;})]];
  V->AddSlot().AutoHeight()[Line([this,Team]{auto* S=Battle(this);return S?FString::Printf(TEXT("METER %d / %d"),S->BattleState.Meter[Team],S->BattleState.MaxMeter[Team]):TEXT("");},14,Color)];
  V->AddSlot().AutoHeight().Padding(0,5)[SNew(SBox).HeightOverride(7)[SNew(SProgressBar).FillColorAndOpacity(FLinearColor(.75,.5,1)).Percent_Lambda([this,Team]{auto* S=Battle(this);return S?float(S->BattleState.Meter[Team])/FMath::Max(1,S->BattleState.MaxMeter[Team]):0.f;})]];
  Top->AddSlot().FillWidth(1).Padding(12,4)[V];
 }
 auto Bottom=SNew(SVerticalBox);
 Bottom->AddSlot().AutoHeight()[Line([this]{auto* GI=Instance(this);auto* S=Battle(this);if(!GI||!S)return FString();FString Mode=GI->Online?TEXT("ONLINE"):GI->IsReplay?TEXT("REPLAY"):TEXT("LOCAL");if(S->bPauseGame)Mode+=TEXT(" / PAUSED");if(GI->IsReplay&&!GI->HasReplayFrame(S->LocalFrame))Mode+=TEXT(" / COMPLETE");return FString::Printf(TEXT("%s    ROUND %d · FRAME %d · TIMER %d    %s"),*Mode,S->BattleState.RoundCount,S->GetPlayableRoundFrame(),FMath::Max(0,S->BattleState.RoundTimer/60),S->GetCurrentRoundResult()==3?TEXT("DRAW"):S->GetCurrentRoundResult()==1?TEXT("PLAYER 1 WINS"):S->GetCurrentRoundResult()==2?TEXT("PLAYER 2 WINS"):TEXT(""));},18,FLinearColor(.6,.9,1))];
 Bottom->AddSlot().AutoHeight()[Line([this]{auto* GI=Instance(this);auto* S=Battle(this);if(!GI)return FString();if(!GI->ModifierSetupError.IsEmpty())return GI->ModifierSetupError;if(S&&!S->GetModifierRejection().IsEmpty())return S->GetModifierRejection();return GI->Notice;},14,FLinearColor(1,.8,.35))];
 Bottom->AddSlot().AutoHeight().Padding(0,5)[Line([]{return FString(TEXT("P1: arrows · A strike · A+S alternate · F dash · A+G knockback · D end restriction\nP2: J/L/K · U strike · U+I alternate · O dash · U+H knockback · Y end restriction\nTab setup / leave online · P pause · R rematch · V save · B replay · Esc exits PIE"));},12,FLinearColor(.82,.88,.96))];
 return SNew(SOverlay)
 +SOverlay::Slot().VAlign(VAlign_Top).Padding(18)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.008,.016,.03,.92))[Top]]
 +SOverlay::Slot().VAlign(VAlign_Bottom).Padding(18)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.008,.016,.03,.94)).Padding(12)[Bottom]];
}

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
AModifierArenaPresentation::AModifierArenaPresentation()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    PrimaryActorTick.bTickEvenWhenPaused = true;
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

void AModifierArenaPresentation::InitializeFighters(ANightSkyGameState* State)
{
    for (int32 Index = 0; Index < 2; ++Index)
    {
        auto* Body = NewObject<USkeletalMeshComponent>(this);
        Body->SetupAttachment(RootComponent);
        Body->SetSkeletalMeshAsset(FighterMesh);
        Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        Body->SetComponentTickEnabled(false);
        Body->SetCastShadow(true);
        Body->RegisterComponent();
        if (Index >= 1)
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
        Label->SetWorldSize(12);
        Label->SetTextRenderColor(Index < 1 ? FColor(80,215,255) : FColor(255,135,205));
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

void AModifierArenaPresentation::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* State = Battle(this);
    if (!State || State->Players.Num()!=2) return;
    if (Bodies.IsEmpty()) InitializeFighters(State);
    const auto Transform = State->BattleSceneTransform;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        auto* Fighter = State->Players[Index];
        for (auto* Cube : TInlineComponentArray<UStaticMeshComponent*>(Fighter))
        {
            Cube->SetVisibility(false);
            // The battle camera fits actor bounds. Expand the hidden display proxy to
            // contain the armored mesh and its role label, without changing combat boxes.
            Cube->SetRelativeScale3D(FVector(1.f, 1.f, 4.2f));
            Cube->SetRelativeLocation(FVector(0, 0, 210));
        }
        const bool Visible = Fighter->IsOnScreen();
        auto* Body = Bodies[Index].Get();
        Body->SetVisibility(Visible);
        Labels[Index]->SetVisibility(false);
        if (!Visible) continue;
        const bool Main = Fighter->IsMainPlayer();
        // Depth separation is visual only; real contacts still use the unmodified fixed-point origins.
        const float Depth = Main ? 0.f : 72.f + 62.f * Fighter->TeamIndex;
        const FVector Position(float(Fighter->PosX)/COORD_SCALE, Depth, float(Fighter->PosY)/COORD_SCALE);
        Body->SetWorldLocation(Transform.TransformPosition(Position));
        Body->SetWorldRotation(Transform.GetRotation() * FRotator(0,Fighter->Direction==DIR_Right ? 0.f:180.f,0).Quaternion());
        const float Bulk = 1.f;
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
        else if (FMath::Abs(Fighter->SpeedX)>0 && Fighter->PosY==0) Animation=Walk;
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

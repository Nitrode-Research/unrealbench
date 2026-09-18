#include "Gameplay/LinkInteractionActor.h"
#include "Gameplay/LinkCharacter.h"
#include "Gameplay/LinkPlayerController.h"
#include "Gameplay/LinkSession.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ALinkInteractionActor::ALinkInteractionActor()
{
    PrimaryActorTick.bCanEverTick=true;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("Root")); SetRootComponent(SceneRoot);
    Selection=CreateDefaultSubobject<UBoxComponent>(TEXT("Selection")); Selection->SetupAttachment(SceneRoot);
    Selection->SetBoxExtent(FVector(110,100,130)); Selection->SetRelativeLocation(FVector(0,0,100));
    Selection->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Selection->SetCollisionResponseToAllChannels(ECR_Ignore);
    Selection->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block); Selection->SetCanEverAffectNavigation(false);
    Area=CreateDefaultSubobject<USphereComponent>(TEXT("ArrivalArea")); Area->SetupAttachment(SceneRoot);
    Area->SetSphereRadius(170); Area->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Area->SetCollisionResponseToAllChannels(ECR_Ignore); Area->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    Area->SetGenerateOverlapEvents(true); Area->SetCanEverAffectNavigation(false);
    WaypointOffsets={FVector(-100,0,0),FVector(100,0,0)};
}
void ALinkInteractionActor::BeginPlay()
{
    Super::BeginPlay();
    if(!WaypointOffsets.IsEmpty())
    {
        for(const auto& Offset:WaypointOffsets){CameraTarget+=GetActorTransform().TransformPosition(Offset);}
        CameraTarget=GetActorTransform().InverseTransformPosition(CameraTarget/WaypointOffsets.Num()+FVector(0,0,100));bCameraTargetValid=true;
    }
    Area->SetSphereRadius(ArrivalRadius);
    Session=GetGameInstance()->GetSubsystem<ULinkSession>();
    if (PersistentId.IsEmpty()) { PersistentId=GetName(); }
    Context.Global=&Session->GetGlobals();
    Context.Interaction=&Session->GetInteractionFacts(PersistentId,InitialItem);
    FLinkStoryEngine Engine(Session->GetDatabase());
    Engine.Set(LinkFacts::LT,LinkFacts::LT,Context); Engine.Set(LinkFacts::RT,LinkFacts::RT,Context);
    Engine.Set(LinkFacts::InitialEvent,EventId,Context);
    UpdateFacts();
    UpdateItemVisual();
}
void ALinkInteractionActor::UpdateItemVisual()
{
    const int32 VisualFact=VisualItemId?VisualItemId:InitialItem;
    if(VisualFact && !ItemVisualTag.IsNone() && Context.Interaction)
    {
        const int32 Value=Context.Interaction->FindRef(VisualFact);
        if(Value!=LastVisualValue)
        {
            LastVisualValue=Value;
            for(TActorIterator<AActor> It(GetWorld());It;++It)
            {
                if(It->ActorHasTag(ItemVisualTag)){It->SetActorHiddenInGame(Value<=0);It->SetActorEnableCollision(Value>0);}
            }
        }
    }
}
void ALinkInteractionActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* Controller=Cast<ALinkPlayerController>(GetWorld()->GetFirstPlayerController());
    if (!Controller) { return; }
    for (int32 Player=0;Player<2;++Player)
    {
        Participants.SetReady(Player,Area->IsOverlappingActor(Controller->GetCharacterAt(Player)));
    }
    UpdateFacts();
    UpdateItemVisual();
}
bool ALinkInteractionActor::Focus(int32 Player,ALinkCharacter* Character)
{
    if (!Character) { return false; }
    TArray<FVector> Points;
    for (const auto& Offset : WaypointOffsets) { Points.Add(GetActorTransform().TransformPosition(Offset)); }
    const bool Result=Participants.Focus(Player,Character->GetNavAgentLocation(),Points);
    UpdateFacts(); return Result;
}
void ALinkInteractionActor::Leave(int32 Player) { Participants.Leave(Player); UpdateFacts(); }
FVector ALinkInteractionActor::WaypointFor(int32 Player) const
{
    const int32 Index=Participants.Waypoint(Player);
    return WaypointOffsets.IsValidIndex(Index) ? GetActorTransform().TransformPosition(WaypointOffsets[Index]) : GetActorLocation();
}
void ALinkInteractionActor::UpdateFacts()
{
    if (!Session) { return; }
    auto* Controller=Cast<ALinkPlayerController>(GetWorld()->GetFirstPlayerController());
    FLinkStoryEngine Engine(Session->GetDatabase());
    for (int32 Player=0;Player<2;++Player)
    {
        const bool Elsewhere=Controller && Controller->GetInteractionFor(Player) && Controller->GetInteractionFor(Player)!=this;
        Engine.Set(Player==0?LinkFacts::IsLTPresent:LinkFacts::IsRTPresent,Participants.Presence(Player,Elsewhere),Context);
    }
    Engine.Set(LinkFacts::Initiator,Participants.GetInitiator(),Context);
    Engine.Set(LinkFacts::Listener,Participants.GetListener(),Context);
}

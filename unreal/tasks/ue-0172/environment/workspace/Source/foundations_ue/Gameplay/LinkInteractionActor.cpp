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
    // Restore the documented B gameplay contract.
    Super::BeginPlay();
}
void ALinkInteractionActor::UpdateItemVisual()
{
    // Restore the documented B gameplay contract.
    
}
void ALinkInteractionActor::Tick(float DeltaSeconds)
{
    // Restore the documented B gameplay contract.
    
}
bool ALinkInteractionActor::Focus(int32 Player,ALinkCharacter* Character)
{
    // Restore the documented B gameplay contract.
    return {};
}
void ALinkInteractionActor::Leave(int32 Player) {
    // Restore the documented B gameplay contract.
    
}
FVector ALinkInteractionActor::WaypointFor(int32 Player) const
{
    // Restore the documented B gameplay contract.
    return {};
}
void ALinkInteractionActor::UpdateFacts()
{
    // Restore the documented B gameplay contract.
    
}

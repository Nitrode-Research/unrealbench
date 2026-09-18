#include "Gameplay/LinkSourceItemDisplay.h"
#include "Gameplay/LinkSession.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"

ALinkSourceItemDisplay::ALinkSourceItemDisplay()
{
    PrimaryActorTick.bCanEverTick=true;
    GetStaticMeshComponent()->SetCollisionProfileName(TEXT("NoCollision"));
    GetStaticMeshComponent()->SetCanEverAffectNavigation(false);
}
void ALinkSourceItemDisplay::BeginPlay() { Super::BeginPlay(); RefreshVisibility(); }
void ALinkSourceItemDisplay::Tick(float DeltaSeconds) { Super::Tick(DeltaSeconds); RefreshVisibility(); }
void ALinkSourceItemDisplay::RefreshVisibility()
{
    if(InteractionId.IsEmpty()||ItemFact==0||!GetGameInstance()){return;}
    const auto& Facts=GetGameInstance()->GetSubsystem<ULinkSession>()->GetInteractionFacts(InteractionId,InitialItem);
    const bool bShouldHide=Facts.FindRef(ItemFact)!=1;
    if(IsHidden()!=bShouldHide){SetActorHiddenInGame(bShouldHide);}
}

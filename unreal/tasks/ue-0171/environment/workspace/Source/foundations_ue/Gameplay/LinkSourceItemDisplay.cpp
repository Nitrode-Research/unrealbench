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
    // TODO: restore the documented scene pipeline behavior.
    
}

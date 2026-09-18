#include "Gameplay/LinkWorldEffect.h"
#include "Gameplay/LinkSession.h"
#include "Components/LightComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"

ALinkWorldEffect::ALinkWorldEffect(){PrimaryActorTick.bCanEverTick=true;}
void ALinkWorldEffect::BeginPlay()
{
    // Restore the documented B gameplay contract.
    Super::BeginPlay();
}
void ALinkWorldEffect::Tick(float DeltaTime){
    // Restore the documented B gameplay contract.
    
}
void ALinkWorldEffect::Apply(float DeltaTime,bool bSnap)
{
    // Restore the documented B gameplay contract.
    
}

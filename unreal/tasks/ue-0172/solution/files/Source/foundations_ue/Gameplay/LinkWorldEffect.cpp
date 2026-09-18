#include "Gameplay/LinkWorldEffect.h"
#include "Gameplay/LinkSession.h"
#include "Components/LightComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"

ALinkWorldEffect::ALinkWorldEffect(){PrimaryActorTick.bCanEverTick=true;}
void ALinkWorldEffect::BeginPlay()
{
    Super::BeginPlay();
    if(!MovingTag.IsNone())
    {
        for(TActorIterator<AActor> It(GetWorld());It;++It){if(It->ActorHasTag(MovingTag)){Movables.Add(*It,It->GetActorTransform());}}
    }
    Apply(0,true);
}
void ALinkWorldEffect::Tick(float DeltaTime){Super::Tick(DeltaTime);Apply(DeltaTime,false);}
void ALinkWorldEffect::Apply(float DeltaTime,bool bSnap)
{
    auto* Session=GetGameInstance()->GetSubsystem<ULinkSession>();
    FLinkStoryContext Context;Context.Global=&Session->GetGlobals();Context.Interaction=Session->FindInteractionFacts(InteractionId);
    FLinkStoryEngine Engine(Session->GetDatabase());bActivated=Engine.Get(InputFact,Context)==1;
    if(OutputFact){Engine.Set(OutputFact,bActivated?1:0,Context);}
    Blend=bSnap?(bActivated?1.f:0.f):FMath::Lerp(Blend,bActivated?1.f:0.f,1.f-FMath::Exp(-8.f*FMath::Max(0.f,DeltaTime)));
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        if(!TrueTag.IsNone()&&It->ActorHasTag(TrueTag)){It->SetActorHiddenInGame(!bActivated);It->SetActorEnableCollision(bActivated);}
        if(!FalseTag.IsNone()&&It->ActorHasTag(FalseTag)){It->SetActorHiddenInGame(bActivated);It->SetActorEnableCollision(!bActivated);}
        if(!LightTag.IsNone()&&It->ActorHasTag(LightTag))
        {
            if(auto* Light=It->FindComponentByClass<ULightComponent>()){Light->SetIntensity(LightIntensity*Blend);}
        }
    }
    for(const auto& Pair:Movables)
    {
        if(auto* Actor=Pair.Key.Get())
        {
            FTransform Transform=Pair.Value;Transform.AddToTranslation(ActiveOffset*Blend);
            Transform.SetRotation(FQuat(FVector::UpVector,FMath::DegreesToRadians(ActiveYaw*Blend))*Pair.Value.GetRotation());
            Actor->SetActorTransform(Transform);
        }
    }
}

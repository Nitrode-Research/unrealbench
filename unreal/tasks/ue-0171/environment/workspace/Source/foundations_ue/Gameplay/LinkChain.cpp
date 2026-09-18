#include "Gameplay/LinkChain.h"
#include "Gameplay/LinkCharacter.h"
#include "CableComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ConfigCacheIni.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"

ALinkChain::ALinkChain()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    Cable = CreateDefaultSubobject<UCableComponent>(TEXT("Chain"));
    SetRootComponent(Cable);
    Cable->CableLength = 820;
    Cable->NumSegments = 82;
    Cable->CableWidth = 5;
    Cable->NumSides = 6;
    Cable->SolverIterations = 12;
    Cable->SubstepTime = 0.005f;
    Cable->bUseSubstepping = true;
    Cable->bEnableCollision = true;
    Cable->CollisionFriction = 0.5f;
    Cable->bAttachStart = Cable->bAttachEnd = true;
    Cable->bSkipCableUpdateWhenNotVisible = false;
    Cable->bSkipCableUpdateWhenNotOwnerRecentlyRendered = false;
    Cable->bResetAfterTeleport = true;
    Cable->bTeleportAfterReattach = true;
    Cable->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Cable->SetCollisionResponseToAllChannels(ECR_Ignore);
    Cable->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Cable->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    Cable->SetCanEverAffectNavigation(false);
    SeededTube=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("AuthoredChainTube"));
    SeededTube->SetupAttachment(Cable); SeededTube->SetAbsolute(true,true,true);
    SeededTube->SetCollisionEnabled(ECollisionEnabled::NoCollision); SeededTube->SetCanEverAffectNavigation(false);
}

void ALinkChain::Initialize(ALinkCharacter* Left, ALinkCharacter* Right)
{
    // Restore the documented A gameplay contract.
    
}

void ALinkChain::GetPoints(TArray<FVector>& OutPoints) const {
    // Restore the documented A gameplay contract.
    
}
float ALinkChain::GetRestLength() const {
    // Restore the documented A gameplay contract.
    return {};
}

bool ALinkChain::InitializeAuthoredCurve(const TArray<FVector>& Centres)
{
    // TODO: restore the documented scene pipeline behavior.
    return false;
}

void ALinkChain::Tick(float DeltaSeconds)
{
    // TODO: restore the documented scene pipeline behavior.
    Super::Tick(DeltaSeconds);
}

void ALinkChain::UpdateSeededTube(bool bCreate)
{
    // TODO: restore the documented scene pipeline behavior.
    
}

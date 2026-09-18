#include "Gameplay/LinkCharacter.h"
#include "Gameplay/LinkAudio.h"
#include "Gameplay/LinkAnimInstance.h"
#include "Engine/World.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "Navigation/PathFollowingComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "NavigationPath.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace
{
FVector ReferencePosition(const FReferenceSkeleton& Skeleton, int32 Index)
{
    FTransform Pose = Skeleton.GetRefBonePose()[Index];
    for (int32 Parent = Skeleton.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
    {
        Pose = Pose * Skeleton.GetRefBonePose()[Parent];
    }
    return Pose.GetLocation();
}
float FacingCorrection(const USkeletalMesh* Mesh)
{
    const auto& Skeleton = Mesh->GetRefSkeleton();
    int32 Foot = Skeleton.FindBoneIndex(TEXT("Foot.L")), Toes = Skeleton.FindBoneIndex(TEXT("Toes.L"));
    if (Foot == INDEX_NONE) { Foot = Skeleton.FindBoneIndex(TEXT("Foot_L")); }
    if (Toes == INDEX_NONE) { Toes = Skeleton.FindBoneIndex(TEXT("Toes_L")); }
    if (Foot == INDEX_NONE || Toes == INDEX_NONE) { return 90.f; }
    const FVector Forward = ReferencePosition(Skeleton,Toes) - ReferencePosition(Skeleton,Foot);
    return -FMath::RadiansToDegrees(FMath::Atan2(Forward.Y,Forward.X));
}
}

ALinkCharacter::ALinkCharacter()
{
    GetCapsuleComponent()->InitCapsuleSize(50.f, 100.f);
    // Pinned Unity collision matrix disables Player (layer 6) against Player.
    // Following owns pair spacing; hard capsule blocking can trap a partner against scenery.
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    GetCharacterMovement()->MaxWalkSpeed = 500.f;
    GetCharacterMovement()->MaxAcceleration = 3000.f;
    GetCharacterMovement()->BrakingDecelerationWalking = 3000.f;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.f, 400.f, 0.f);
    GetCharacterMovement()->GetNavAgentPropertiesRef().AgentRadius = 50.f;
    GetCharacterMovement()->GetNavAgentPropertiesRef().AgentHeight = 200.f;
    GetCharacterMovement()->GetNavAgentPropertiesRef().AgentStepHeight = 20.f;
    GetCharacterMovement()->MaxStepHeight = 20.f;
    GetCharacterMovement()->SetWalkableFloorAngle(30.f);
    GetCharacterMovement()->GetNavMovementProperties()->bUseAccelerationForPaths = true;
    bUseControllerRotationYaw = false;
    AIControllerClass = AAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(GetCapsuleComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Cylinder"));
    Body->SetStaticMesh(Shape.Object);
    Body->SetRelativeScale3D(FVector(0.55, 0.55, 1.7));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetCanEverAffectNavigation(false);
}

void ALinkCharacter::SetIdentity(int32 InIdentity)
{
    // Restore the documented A gameplay contract.
    
}

FLinkAnimationMotionInput ALinkCharacter::GetAnimationMotionInput() const
{
    // Restore the documented A gameplay contract.
    return {};
}
void ALinkCharacter::OnSourceAnimationStep(float NormalizedSpeed)
{
    // Restore the documented A gameplay contract.
    
}

bool ALinkCharacter::MoveTo(const FVector& Target, float StopDistance, float Speed, float Acceleration)
{
    // Restore the documented A gameplay contract.
    return {};
}

void ALinkCharacter::StopNavigation()
{
    // Restore the documented A gameplay contract.
    
}
void ALinkCharacter::SettleForCompletion()
{
    // Restore the documented A gameplay contract.
    
}

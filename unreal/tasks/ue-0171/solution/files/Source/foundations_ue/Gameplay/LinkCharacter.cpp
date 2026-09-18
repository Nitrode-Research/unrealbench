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
    Identity = InIdentity;
    const FString Tag = Identity == 0 ? TEXT("LT") : TEXT("RT");
    const FString MeshPath = FString::Printf(TEXT("/Game/Characters/%s/SK_%s.SK_%s"),*Tag,*Tag,*Tag);
    USkeletalMesh* Model = LoadObject<USkeletalMesh>(nullptr,*MeshPath);
    if (Model)
    {
        GetMesh()->SetSkeletalMeshAsset(Model);
        GetMesh()->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
        GetMesh()->SetRelativeRotation(FRotator(0,FacingCorrection(Model),0));
        GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        GetMesh()->SetCanEverAffectNavigation(false);
        GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        GetMesh()->SetAnimInstanceClass(ULinkAnimInstance::StaticClass());
        Body->SetVisibility(false);
        return;
    }
    const TCHAR* MaterialPath = Identity == 0 ? TEXT("/Game/Materials/M_LT.M_LT") : TEXT("/Game/Materials/M_RT.M_RT");
    if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
    {
        Body->SetMaterial(0, Material);
    }
}

FLinkAnimationMotionInput ALinkCharacter::GetAnimationMotionInput() const
{
    FLinkAnimationMotionInput Result;
    Result.ActualSpeed=GetVelocity().Size2D();Result.StopDistance=FMath::Max(0.f,LastStopDistance);
    const auto* AI=Cast<AAIController>(GetController());
    const auto* Following=AI?AI->GetPathFollowingComponent():nullptr;
    if(Following&&Following->GetStatus()==EPathFollowingStatus::Moving)
    {
        const auto Path=Following->GetPath();
        if(Path.IsValid())
        {
            FVector Previous=GetNavAgentLocation();
            const auto& Points=Path->GetPathPoints();
            for(int32 Index=Following->GetCurrentPathIndex()+1;Index<Points.Num();++Index)
            {
                Result.RemainingDistance+=FVector::Distance(Previous,Points[Index].Location);Previous=Points[Index].Location;
            }
        }
        Result.DesiredSpeed=GetCharacterMovement()->GetLastUpdateRequestedVelocity().Size2D();
        // Acceleration-driven RequestPathMove supplies a direction rather than
        // RequestDirectMove's velocity. Its requested magnitude is MaxWalkSpeed.
        if(Result.DesiredSpeed<=UE_SMALL_NUMBER){Result.DesiredSpeed=GetCharacterMovement()->MaxWalkSpeed;}
    }
    return Result;
}
void ALinkCharacter::OnSourceAnimationStep(float NormalizedSpeed)
{
    ++AnimationStepCount;
    if(auto* Audio=GetWorld()->GetSubsystem<ULinkAudio>()){Audio->Footstep(Identity,AnimationStepCount);}
}

bool ALinkCharacter::MoveTo(const FVector& Target, float StopDistance, float Speed, float Acceleration)
{
    AAIController* AI = Cast<AAIController>(GetController());
    if (!AI || Target.ContainsNaN()) { return false; }
    GetCharacterMovement()->MaxWalkSpeed = Speed;
    GetCharacterMovement()->MaxAcceleration = Acceleration;
    GetCharacterMovement()->BrakingDecelerationWalking = Acceleration;
    if (Speed <= UE_SMALL_NUMBER) { StopNavigation(); return true; }
    if (bHasMoveGoal && Target.Equals(LastMoveGoal, 1.f) && FMath::IsNearlyEqual(StopDistance, LastStopDistance, 1.f)
        && AI->GetMoveStatus() == EPathFollowingStatus::Moving) { return true; }
    LastMoveGoal = Target;
    LastStopDistance = StopDistance;
    bHasMoveGoal = true;
    // Reject partial routes: a successful command must reach navigable ground.
    return AI->MoveToLocation(Target, StopDistance, false, true, true, false, nullptr, false) != EPathFollowingRequestResult::Failed;
}

void ALinkCharacter::StopNavigation()
{
    if (auto* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); }
    bHasMoveGoal = false;
}
void ALinkCharacter::SettleForCompletion()
{
    MotionMode=ELinkMotionMode::Idle;StopNavigation();GetCharacterMovement()->StopMovementImmediately();
    if(auto* Instance=Cast<ULinkAnimInstance>(GetMesh()->GetAnimInstance()))
    {
        Instance->SettleToIdle();
        GetMesh()->TickAnimation(1.f/30.f,false);GetMesh()->RefreshBoneTransforms();
    }
}

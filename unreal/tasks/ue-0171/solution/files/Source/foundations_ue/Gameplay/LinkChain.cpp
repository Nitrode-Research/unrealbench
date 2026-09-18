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
    if (!Left || !Right) { return; }
    LeftCrew=Left; RightCrew=Right;
    float Length = Cable->CableLength;
    GConfig->GetFloat(TEXT("Foundations.Chain"), TEXT("Length"), Length, GGameIni);
    if (FMath::IsFinite(Length) && Length > 0) { Cable->CableLength = Length; }
    const FName Cuff(TEXT("ChainCuff"));
    if (Left->GetMesh()->DoesSocketExist(Cuff) && Right->GetMesh()->DoesSocketExist(Cuff))
    {
        Cable->SetAbsolute(false,false,true);
        AttachToComponent(Left->GetMesh(),FAttachmentTransformRules::SnapToTargetNotIncludingScale,Cuff);
        SetActorRelativeLocation(FVector::ZeroVector);
        SetActorScale3D(FVector::OneVector);
        Cable->SetAttachEndToComponent(Right->GetMesh(),Cuff);
        Cable->EndLocation=FVector::ZeroVector;
        Cable->AddTickPrerequisiteComponent(Left->GetMesh());
        Cable->AddTickPrerequisiteComponent(Right->GetMesh());
    }
    else
    {
        AttachToComponent(Left->GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        SetActorRelativeLocation(FVector(0, 0, AttachmentOffset));
        Cable->SetAttachEndToComponent(Right->GetCapsuleComponent());
        Cable->EndLocation = FVector(0, 0, AttachmentOffset);
    }
    Cable->AddTickPrerequisiteComponent(Left->GetCharacterMovement());
    Cable->AddTickPrerequisiteComponent(Right->GetCharacterMovement());
    // Initialize particles from the actual endpoints, including after save restoration.
    Cable->ReregisterComponent();
}

void ALinkChain::GetPoints(TArray<FVector>& OutPoints) const { if(bSeeded){OutPoints=Seeded.Points;}else{Cable->GetCableParticleLocations(OutPoints);} }
float ALinkChain::GetRestLength() const { return Cable->CableLength; }

bool ALinkChain::InitializeAuthoredCurve(const TArray<FVector>& Centres)
{
    if (!LeftCrew || !RightCrew) { return false; }
    const FName Cuff(TEXT("ChainCuff"));
    if (!Seeded.Initialize(LeftCrew->GetMesh()->GetSocketLocation(Cuff),RightCrew->GetMesh()->GetSocketLocation(Cuff),Centres)) { return false; }
    bSeeded=true; SeededRemainder=0;
    Cable->SetVisibility(false); Cable->SetComponentTickEnabled(false);
    SeededTube->SetWorldTransform(FTransform::Identity);
    UpdateSeededTube(true); return true;
}

void ALinkChain::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bSeeded || !LeftCrew || !RightCrew) { return; }
    SeededRemainder+=FMath::Min(DeltaSeconds,.1f);
    constexpr float Step=.005f;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AuthoredChain),false,this);
    Params.AddIgnoredActor(LeftCrew); Params.AddIgnoredActor(RightCrew);
    while (SeededRemainder>=Step)
    {
        SeededRemainder-=Step;
        Seeded.Integrate(Step,FVector(0,0,GetWorld()->GetGravityZ()),LeftCrew->GetMesh()->GetSocketLocation(TEXT("ChainCuff")),RightCrew->GetMesh()->GetSocketLocation(TEXT("ChainCuff")));
        Seeded.Constrain();
        for (int32 I=1;I<Seeded.Points.Num()-1;++I)
        {
            FHitResult Hit;
            if (GetWorld()->SweepSingleByChannel(Hit,Seeded.Previous[I],Seeded.Points[I],FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeSphere(2.5f),Params))
            {
                const FVector Location=Hit.bStartPenetrating ? Seeded.Points[I]+Hit.Normal*(Hit.PenetrationDepth+.1f) : Hit.Location+Hit.Normal*.1f;
                const FVector Velocity=Seeded.Points[I]-Seeded.Previous[I];
                Seeded.Points[I]=Location;
                Seeded.Previous[I]=Location-FVector::VectorPlaneProject(Velocity,Hit.Normal)*.5;
            }
        }
    }
    UpdateSeededTube(false);
}

void ALinkChain::UpdateSeededTube(bool bCreate)
{
    constexpr int32 Sides=6;
    TArray<FVector> Vertices,Normals; TArray<FVector2D> UVs; TArray<int32> Triangles;
    TArray<FProcMeshTangent> Tangents;
    FVector PrevSide=FVector::RightVector;
    for (int32 I=0;I<Seeded.Points.Num();++I)
    {
        const FVector Direction=(Seeded.Points[FMath::Min(I+1,Seeded.Points.Num()-1)]-Seeded.Points[FMath::Max(I-1,0)]).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
        FVector Side=FVector::VectorPlaneProject(PrevSide,Direction).GetSafeNormal();
        if (Side.IsNearlyZero()) { Side=FVector::CrossProduct(Direction,FVector::UpVector).GetSafeNormal(SMALL_NUMBER,FVector::RightVector); }
        PrevSide=Side; const FVector Up=FVector::CrossProduct(Direction,Side).GetSafeNormal();
        for (int32 J=0;J<Sides;++J)
        {
            const double Angle=2*PI*J/Sides; const FVector Normal=Side*FMath::Cos(Angle)+Up*FMath::Sin(Angle);
            Vertices.Add(Seeded.Points[I]+Normal*2.5); Normals.Add(Normal); UVs.Add(FVector2D(double(I)/(Seeded.Points.Num()-1),double(J)/Sides));
            Tangents.Add(FProcMeshTangent(Direction,false));
            if (I>0 && bCreate)
            {
                const int32 A=(I-1)*Sides+J,B=(I-1)*Sides+(J+1)%Sides,C=I*Sides+J,D=I*Sides+(J+1)%Sides;
                Triangles.Append({A,C,B,B,C,D});
            }
        }
    }
    if (bCreate) { SeededTube->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UVs,TArray<FLinearColor>(),Tangents,false); }
    else { SeededTube->UpdateMeshSection_LinearColor(0,Vertices,Normals,UVs,TArray<FLinearColor>(),Tangents); }
}

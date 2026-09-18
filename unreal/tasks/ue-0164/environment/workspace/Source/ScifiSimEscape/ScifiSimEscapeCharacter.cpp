// Copyright Epic Games, Inc. All Rights Reserved.

#include "ScifiSimEscapeCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ScifiSimEscape.h"
#include "InteractionSystem/InteractorComponent.h"
#include "Climbing/ClimbComponent.h"
#include "Stealth/StealthComponent.h"

AScifiSimEscapeCharacter::AScifiSimEscapeCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	
	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Create the Camera Component	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// Create interactor component
	InteractorComponent = CreateDefaultSubobject<UInteractorComponent>(TEXT("Interactor Component"));

	// Create climb component
	ClimbComponent = CreateDefaultSubobject<UClimbComponent>(TEXT("Climb Component"));

	// Create stealth component
	StealthComponent = CreateDefaultSubobject<UStealthComponent>(TEXT("Stealth Component"));
	
	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;
	
	// If crouch capability is disabled, attempting to crouch will fail silently at runtime
	// and print this warning to the log:
	// LogCharacter: BP_FirstPersonCharacter_C_0 is trying to crouch, but crouching is disabled
	// on this character! (check CharacterMovement NavAgentSettings)
	// Setting bCanCrouch to true here enables crouching for this character.
	GetCharacterMovement()->NavAgentProps.bCanCrouch = true;	
}

void AScifiSimEscapeCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeCharacter::BeginPlay()
{
	// Restore the published gameplay contract.
	Super::BeginPlay();
}

void AScifiSimEscapeCharacter::Tick(float DeltaSeconds)
{
	// Restore the published gameplay contract.
	Super::Tick(DeltaSeconds);
}

void AScifiSimEscapeCharacter::Landed(const FHitResult& Hit)
{
	// Restore the published gameplay contract.
	Super::Landed(Hit);
}

void AScifiSimEscapeCharacter::UpdateCameraLocation(const float DeltaTime)
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeCharacter::MoveInput(const FInputActionValue& Value)
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeCharacter::LookInput(const FInputActionValue& Value)
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeCharacter::DoAim(float Yaw, float Pitch)
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeCharacter::DoMove(float Right, float Forward)
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeCharacter::DoJumpStart()
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeCharacter::DoJumpEnd()
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeCharacter::DoInteract()
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeCharacter::DoHack()
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeCharacter::OnCrouchStart()
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeCharacter::OnCrouchEnd()
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeCharacter::TryCrouchOnLanding()
{
	// Restore the published gameplay contract.
}
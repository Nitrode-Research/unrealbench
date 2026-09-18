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
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AScifiSimEscapeCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AScifiSimEscapeCharacter::DoJumpEnd);

		// Crouching
		EnhancedInputComponent->BindAction(CrouchAction,ETriggerEvent::Started,this,&AScifiSimEscapeCharacter::OnCrouchStart);
		EnhancedInputComponent->BindAction(CrouchAction,ETriggerEvent::Completed,this,&AScifiSimEscapeCharacter::OnCrouchEnd);

		// Interacting
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AScifiSimEscapeCharacter::DoInteract);

		// Hacking
		EnhancedInputComponent->BindAction(HackAction, ETriggerEvent::Started, this, &AScifiSimEscapeCharacter::DoHack);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AScifiSimEscapeCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AScifiSimEscapeCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AScifiSimEscapeCharacter::LookInput);
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AScifiSimEscapeCharacter::BeginPlay()
{
	Super::BeginPlay();

	DefaultCameraRelativeLocation = FirstPersonCameraComponent->GetRelativeLocation();
	TargetCameraRelativeLocation = DefaultCameraRelativeLocation;
}

void AScifiSimEscapeCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateCameraLocation(DeltaSeconds);
	//TryCrouchOnLanding();	
}

void AScifiSimEscapeCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	TryCrouchOnLanding();
}

void AScifiSimEscapeCharacter::UpdateCameraLocation(const float DeltaTime)
{
	if (FirstPersonCameraComponent == nullptr)
	{
		UE_LOG(LogTemp,Warning,TEXT("AScifiSimEscapeCharacter: UpdateCameraLocation: FirstPersonCameraComponent is invalid for some reason. Cannot update camera location"));
		return;
	}
	
	const FVector CurrentLocation = FirstPersonCameraComponent->GetRelativeLocation();

	const FVector NewLocation = FMath::VInterpTo(
		CurrentLocation,
		TargetCameraRelativeLocation,
		DeltaTime,
		CrouchCameraInterpSpeed
	);

	FirstPersonCameraComponent->SetRelativeLocation(NewLocation);
}

void AScifiSimEscapeCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);

}

void AScifiSimEscapeCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);

}

void AScifiSimEscapeCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AScifiSimEscapeCharacter::DoMove(float Right, float Forward)
{
	// On a ladder forward is up, and nothing else moves the character.
	if (ClimbComponent != nullptr && ClimbComponent->IsClimbing())
	{
		ClimbComponent->AddClimbInput(Forward);
		return;
	}

	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AScifiSimEscapeCharacter::DoJumpStart()
{
	// On a ladder, jump lets go.
	if (ClimbComponent != nullptr && ClimbComponent->IsClimbing())
	{
		ClimbComponent->StopClimbing();
		return;
	}

	// pass Jump to the character
	Jump();
}

void AScifiSimEscapeCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}

void AScifiSimEscapeCharacter::DoInteract()
{
	if (InteractorComponent != nullptr)
	{
		InteractorComponent->Interact();
	}
}

void AScifiSimEscapeCharacter::DoHack()
{
	if (InteractorComponent != nullptr)
	{
		InteractorComponent->Hack();
	}
}

void AScifiSimEscapeCharacter::OnCrouchStart()
{
	bCrouchHeld = true;
	
	// Disable crouching while falling, player should not be abe to crouch while falling/jumping
	if (GetCharacterMovement()->IsFalling())
	{
		return;
	}

	// Nor on a ladder.
	if (ClimbComponent != nullptr && ClimbComponent->IsClimbing())
	{
		return;
	}
	
	Crouch();

	TargetCameraRelativeLocation = DefaultCameraRelativeLocation;
	TargetCameraRelativeLocation.X -= CrouchCameraOffset;
	
	UE_LOG(LogTemp,Warning,TEXT("Crouch Start"));
}

void AScifiSimEscapeCharacter::OnCrouchEnd()
{
	bCrouchHeld = false;
	
	UnCrouch();

	TargetCameraRelativeLocation = DefaultCameraRelativeLocation;

	UE_LOG(LogTemp,Warning,TEXT("Crouch end"));
}

void AScifiSimEscapeCharacter::TryCrouchOnLanding()
{
	if (!bCrouchHeld || bIsCrouched)
	{
		return;
	}

	Crouch();

	TargetCameraRelativeLocation = DefaultCameraRelativeLocation;
	TargetCameraRelativeLocation.X -= CrouchCameraOffset;
}
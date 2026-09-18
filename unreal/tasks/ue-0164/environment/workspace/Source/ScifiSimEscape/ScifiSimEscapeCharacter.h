// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "ScifiSimEscapeCharacter.generated.h"

class UInteractorComponent;
class UClimbComponent;
class UStealthComponent;
class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A basic first person character
 */
UCLASS(abstract)
class AScifiSimEscapeCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MouseLookAction;

	/** Crouch Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* CrouchAction;

	/** Interact Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* InteractAction;

	/** Hack Input Action: works the handheld on whatever the Interactor is focused on */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* HackAction;

public:
	AScifiSimEscapeCharacter();

protected:

	/** Called from Input Actions for movement input */
	void MoveInput(const FInputActionValue& Value);

	/** Called from Input Actions for looking input */
	void LookInput(const FInputActionValue& Value);

	/** Handles aim inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoAim(float Yaw, float Pitch);

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles jump start inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump end inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	/** Handles crouch start input */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void OnCrouchStart();

	/** Handles crouch end input */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void OnCrouchEnd();

	/** Handles interact inputs from either controls or UI interfaces. Presses interact on whatever the Interactor is focused on */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoInteract();

	/** Handles hack inputs from either controls or UI interfaces. Presses hack on whatever the Interactor is focused on */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoHack();

protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	virtual void Landed(const FHitResult& Hit) override;

public:

	/** Returns the first person mesh **/
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	/** Returns first person camera component **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	/** Returns the climb component, which holds the character on a ladder while it climbs. */
	UClimbComponent* GetClimbComponent() const { return ClimbComponent; }

	/** Returns the stealth component, which says whether a guard with line of sight would see the character. */
	UStealthComponent* GetStealthComponent() const { return StealthComponent; }

private:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input", meta = (AllowPrivateAccess = true))
	TObjectPtr<UInteractorComponent> InteractorComponent;

	/** Ladder climbing. While it is climbing, forward input goes to it and jump lets go. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UClimbComponent> ClimbComponent;

	/** Stealth: the lights zone the character stands in, whether it is crouched or slow, and whether it is in a hiding spot. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStealthComponent> StealthComponent;
	
#pragma region Crouch

	void UpdateCameraLocation(const float DeltaTime);
	/** Allow crouch to activate automatically upon landing if the player held the crouch button while jumping. **/
	void TryCrouchOnLanding();
	
	UPROPERTY(EditDefaultsOnly, Category = "Crouch")
	float CrouchCameraOffset = 40.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Crouch")
	float CrouchCameraInterpSpeed = 8.0f;

	FVector DefaultCameraRelativeLocation;
	FVector TargetCameraRelativeLocation;

	UPROPERTY(BlueprintReadOnly,Category = "Crouch", meta = (AllowPrivateAccess = "true"))
	bool bCrouchHeld = false;

#pragma endregion Crouch
	
};


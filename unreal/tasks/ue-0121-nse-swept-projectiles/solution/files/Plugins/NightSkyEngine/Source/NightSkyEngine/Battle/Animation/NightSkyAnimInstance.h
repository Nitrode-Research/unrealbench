// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "NightSkyAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class NIGHTSKYENGINE_API UNightSkyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

protected:
	virtual void Montage_Advance(float DeltaSeconds) override;
	
public:
	UNightSkyAnimInstance();
	UPROPERTY() TObjectPtr<UAnimSequenceBase> PlaytestCastAnimation;
	UPROPERTY() TObjectPtr<UAnimSequenceBase> RequestedAnimation;
	bool bPlaytestCastMontage = false;

	UPROPERTY(BlueprintReadWrite)
	UAnimMontage* RootMontage;
	
	UPROPERTY(BlueprintReadWrite)
	float CurrentAnimTime;

	UFUNCTION(BlueprintCallable, meta = (BlueprintThreadSafe))
	void CreateRootMontage(UAnimSequenceBase* Asset, const FAlphaBlendArgs& BlendIn, const FAlphaBlendArgs& BlendOut);
};

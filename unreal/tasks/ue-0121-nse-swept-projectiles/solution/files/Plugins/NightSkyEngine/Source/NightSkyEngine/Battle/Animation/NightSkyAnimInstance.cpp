// Fill out your copyright notice in the Description page of Project Settings.


#include "NightSkyAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "UObject/ConstructorHelpers.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Battle/Playtest/SweptProjectilePlaytest.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NightSkyAnimInstance)

UNightSkyAnimInstance::UNightSkyAnimInstance()
{
	static ConstructorHelpers::FObjectFinder<UAnimSequence> CastAsset(TEXT("/Game/Playtest0121/AS_PlasmaCast.AS_PlasmaCast"));
	PlaytestCastAnimation = CastAsset.Object;
}

void UNightSkyAnimInstance::Montage_Advance(float DeltaSeconds)
{
	float CastTime = 0;
	const bool bCasting = PlaytestCastAnimation && NSEPlaytest::CastPoseTime(Cast<APlayerObject>(GetOwningActor()), CastTime);
	if (RequestedAnimation && bCasting != bPlaytestCastMontage)
	{
		FAlphaBlendArgs Blend;
		Blend.BlendTime = .045f;
		CreateRootMontage(RequestedAnimation, Blend, Blend);
	}
	if (!RootMontage)
	{
		Super::Montage_Advance(DeltaSeconds);
		return;
	}
	
	Montage_SetPosition(RootMontage, bCasting ? CastTime : CurrentAnimTime);
}

void UNightSkyAnimInstance::CreateRootMontage(UAnimSequenceBase* Asset, const FAlphaBlendArgs& BlendIn, const FAlphaBlendArgs& BlendOut)
{
	if (!Asset) return;
	RequestedAnimation = Asset;
	float CastTime = 0;
	bPlaytestCastMontage = PlaytestCastAnimation && NSEPlaytest::CastPoseTime(Cast<APlayerObject>(GetOwningActor()), CastTime);
	if (bPlaytestCastMontage) Asset = PlaytestCastAnimation;
	RootMontage = PlaySlotAnimationAsDynamicMontage_WithBlendArgs(Asset, "Root", BlendIn, BlendOut, 1, 1, -1,
	                                                              CurrentAnimTime);
	Montage_SetNextSection("Default", "Default", RootMontage);
}

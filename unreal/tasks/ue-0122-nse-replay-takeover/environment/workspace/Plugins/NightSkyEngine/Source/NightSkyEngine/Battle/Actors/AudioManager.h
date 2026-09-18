// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AudioManager.generated.h"

constexpr int CommonAudioChannelCount = 32;
constexpr int CharaAudioChannelCount = 32;
constexpr int CharaVoiceChannelCount = 6;

class USoundBase;

// Optional native observer for common one-shot audio presentation. The engine
// notifies it wherever it actually starts or stops a common audio channel, so
// catch-up audio is observable without a sound device. This interface owns no
// replay policy: deciding whether to present belongs to the caller.
class NIGHTSKYENGINE_API ICommonAudioObserver
{
public:
	virtual ~ICommonAudioObserver() = default;
	virtual void Started(int32 Channel, USoundBase* Sound, float StartTime) = 0;
	virtual void Stopped(int32 Channel) = 0;
};

UCLASS()
class NIGHTSKYENGINE_API AAudioManager : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AAudioManager();

	UPROPERTY()
	UAudioComponent* CommonAudioPlayers[CommonAudioChannelCount];
	UPROPERTY()
	UAudioComponent* CharaAudioPlayers[CharaAudioChannelCount];
	UPROPERTY()
	UAudioComponent* CharaVoicePlayers[CharaVoiceChannelCount];
	UPROPERTY()
	UAudioComponent* AnnouncerVoicePlayer;
	UPROPERTY()
	UAudioComponent* MusicPlayer;

	TSharedPtr<ICommonAudioObserver> CommonAudioObserver;

	// Common-channel presentation seam. Callers decide whether to present at
	// all; these report what was presented to the optional observer.
	void PlayCommon(int32 Channel, USoundBase* Sound, float StartTime = 0.0f);
	void StopCommon(int32 Channel);
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	void PauseAllAudio();
};

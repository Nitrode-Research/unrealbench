#include "Gameplay/LinkAudio.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"
#include "Misc/ConfigCacheIni.h"

void ULinkAudio::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    StepSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/S_Footstep.S_Footstep"));
    ChainSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/S_Chain.S_Chain"));
    CommandSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/S_Command.S_Command"));
    PickupSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/S_Pickup.S_Pickup"));
    BedSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/S_YardBed.S_YardBed"));
    GConfig->GetFloat(TEXT("Foundations.Audio"),TEXT("MasterVolume"),MasterVolume,GGameIni);
    GConfig->GetFloat(TEXT("Foundations.Audio"),TEXT("AmbienceVolume"),AmbienceVolume,GGameIni);
    GConfig->GetFloat(TEXT("Foundations.Audio"),TEXT("FootstepVolume"),FootstepVolume,GGameIni);
    MasterVolume=FMath::Clamp(MasterVolume,0.f,1.f);AmbienceVolume=FMath::Clamp(AmbienceVolume,0.f,1.f);FootstepVolume=FMath::Clamp(FootstepVolume,0.f,1.f);
}
bool ULinkAudio::HasRequiredAssets() const{return StepSound&&ChainSound&&CommandSound&&PickupSound&&BedSound;}
void ULinkAudio::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    if(BedSound){Bed=UGameplayStatics::SpawnSound2D(this,BedSound,MasterVolume*AmbienceVolume,1,0,nullptr,false,false);if(Bed){Bed->SetUISound(false);}}
}
void ULinkAudio::Deinitialize(){if(Bed){Bed->Stop();Bed->DestroyComponent();}Bed=nullptr;Super::Deinitialize();}
void ULinkAudio::Cue(USoundBase* Sound,float Volume,float Pitch)
{
    if(Sound&&!bMuted){UGameplayStatics::PlaySound2D(this,Sound,MasterVolume*Volume,Pitch);}
}
void ULinkAudio::Command(){Cue(CommandSound,0.25f);}
void ULinkAudio::Pickup(){Cue(PickupSound,0.35f);}
void ULinkAudio::Footstep(int32 Identity,int32 Count)
{
    Cue(StepSound,FootstepVolume,Identity==0?1.07f:0.92f);
    if(Count%3==0){Cue(ChainSound,0.16f,Count%2?1.08f:0.94f);}
}
void ULinkAudio::ToggleMute(){bMuted=!bMuted;if(Bed){Bed->SetVolumeMultiplier(bMuted?0:MasterVolume*AmbienceVolume);}}

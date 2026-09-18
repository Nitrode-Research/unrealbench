#include "Gameplay/LinkSceneLayout.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

ALinkSceneLayout::ALinkSceneLayout() { SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("SceneLayout"))); }
ALinkSceneLayout* ALinkSceneLayout::Find(const UWorld* World)
{
    if (!World) { return nullptr; }
    TActorIterator<ALinkSceneLayout> It(World);
    return It ? *It : nullptr;
}
bool ALinkSceneLayout::Load()
{
    // TODO: restore the documented scene pipeline behavior.
    return false;
}
bool ALinkSceneLayout::GetPlayerStart(int32 Index,FTransform& Out)
{
    // TODO: restore the documented scene pipeline behavior.
    return false;
}
const TArray<FVector>& ALinkSceneLayout::GetChainCentres() {
    // TODO: restore the documented scene pipeline behavior.
    return ChainCentres;
}

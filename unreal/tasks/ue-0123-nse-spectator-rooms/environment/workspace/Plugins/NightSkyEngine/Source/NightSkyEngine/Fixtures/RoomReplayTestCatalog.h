#pragma once
#include "CoreMinimal.h"
#include "NightSkyEngine/Network/SpectatorRoom.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

// Test-owned metadata records only public export bytes and publicly listed IDs.
// It never opens or guesses the implementation's replay storage files.
namespace RoomReplayTestCatalog
{
inline FString Path(const FString &Match)
{
    return FPaths::ProjectSavedDir() / TEXT("Automation/PublicRoomExports") /
           (FMD5::HashAnsiString(*Match) + TEXT(".json"));
}
inline bool Load(const FString &Match, FString &ReplayId, TArray<uint8> &Bytes)
{
    FString Json, Encoded;
    TSharedPtr<FJsonObject> Value;
    return FFileHelper::LoadFileToString(Json, *Path(Match)) &&
           FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Value) && Value.IsValid() &&
           Value->TryGetStringField(TEXT("replay_id"), ReplayId) && !ReplayId.IsEmpty() &&
           Value->TryGetStringField(TEXT("bytes"), Encoded) && FBase64::Decode(Encoded, Bytes);
}
inline bool Record(USpectatorRoom *Room, const TArray<uint8> &Bytes,
                   const TArray<FString> &Before, FString *OutIdentity = nullptr)
{
    FRoomMatch Match;
    if (!USpectatorRoom::ReadReplay(Bytes, Match))
        return false;
    const auto Listed = Room->SavedReplays();
    FString ReplayId;
    for (const auto &Id : Listed)
        if (!Before.Contains(Id))
        {
            ReplayId = Id;
            break;
        }
    if (ReplayId.IsEmpty())
    {
        TArray<uint8> PriorBytes;
        if (!Load(Match.Id, ReplayId, PriorBytes) || !Listed.Contains(ReplayId))
            return false;
    }
    auto Value = MakeShared<FJsonObject>();
    Value->SetStringField(TEXT("replay_id"), ReplayId);
    Value->SetStringField(TEXT("bytes"), FBase64::Encode(Bytes));
    FString Json;
    FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Json));
    const FString Filename = Path(Match.Id);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    const FString Temporary = Filename + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!FFileHelper::SaveStringToFile(Json, *Temporary) ||
        !IFileManager::Get().Move(*Filename, *Temporary, true, true))
        return false;
    if (OutIdentity)
        *OutIdentity = ReplayId;
    return true;
}
}

#include "SpectatorRoom.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "NightSkyEngine/Data/PrimaryStageData.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformFileManager.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include "Misc/FileHelper.h"
#include "Misc/Compression.h"
#include "Engine/NetConnection.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeExit.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "UObject/Package.h"
#include "UObject/LinkerInstancingContext.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "RoomPanel.h"
#include "NightSkyEngine/Battle/Actors/AudioManager.h"
#include "Components/AudioComponent.h"
#include "NiagaraComponent.h"
#include "NightSkyEngine/Battle/Actors/ParticleManager.h"
#include "include/ggponet.h"
#include "include/connection_manager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NightSkyEngine/Miscellaneous/ReplayInfo.h"
#include "NightSkyEngine/Miscellaneous/NightSkySettingsInfo.h"
#include "NightSkyEngine/Data/SoundData.h"
#include "HAL/FileManager.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"

namespace
{
bool LoadPrivateRoomWorld(URoomPlaybackGameInstance *Game, const FString &StageURL, FString &Error)
{
    auto &Context = *Game->GetWorldContext();
    FURL URL(nullptr, *StageURL, TRAVEL_Absolute);
    const FString SourcePackage = URL.Map;
    static int32 NextPrivateInstance = 100000;
    FString PrivatePackage;
    do
    {
        Context.PIEInstance = NextPrivateInstance++;
        PrivatePackage = FString::Printf(TEXT("%s/NSE_Room_%d_%s"),
                                         *FPackageName::GetLongPackagePath(SourcePackage),
                                         Context.PIEInstance, *FPackageName::GetShortName(SourcePackage));
    } while (FindPackage(nullptr, *PrivatePackage));

    // The Game loader only makes a private copy when the canonical world is
    // already initialized. Allocate a separate package even on the first load,
    // leaving the canonical stage available for ordinary live/seamless travel.
    const FName PackageName(*PrivatePackage);
    // This loaded destination is not active in a context yet. Keep it inactive
    // through LoadMap's old-world cleanup; LoadMap adopts it and sets the Game
    // world type before initialization. Its temporary root still protects it.
    UWorld::WorldTypePreLoadMap.FindOrAdd(PackageName) = EWorldType::Inactive;
    UPackage *Package = CreatePackage(*PrivatePackage);
    FLinkerInstancingContext Instancing;
    Instancing.AddPackageMapping(FName(*SourcePackage), PackageName);
    UPackage *LoadedPackage = Package ? LoadPackage(Package, *SourcePackage, LOAD_None, nullptr, &Instancing) : nullptr;
    UWorld::WorldTypePreLoadMap.Remove(PackageName);
    UWorld *PrivateWorld = LoadedPackage ? UWorld::FindWorldInPackage(LoadedPackage) : nullptr;
    if (!PrivateWorld || LoadedPackage != Package || PrivateWorld->GetOutermost() != Package)
    {
        Error = TEXT("could not load an isolated stage package: ") + SourcePackage;
        return false;
    }
    Package->SetPIEInstanceID(Context.PIEInstance);
    for (ULevelStreaming *Streaming : PrivateWorld->GetStreamingLevels())
        Streaming->RenameForPIE(Context.PIEInstance);

    // LoadMap collects garbage while tearing down InitializeStandalone's empty
    // world. Keep the uninitialized destination alive until LoadMap owns it.
    PrivateWorld->AddToRoot();
    // LoadMap strips the engine's UEDPIE prefix. This private alias deliberately
    // uses a different name, with package imports remapped during LoadPackage.
    URL.Map = PrivatePackage;
    UWorld *PreviousWorld = GWorld;
    const bool Loaded = GEngine->LoadMap(Context, URL, nullptr, Error);
    GWorld = PreviousWorld;
    if (!Loaded || Context.World() != PrivateWorld || PrivateWorld->GetOutermost() != Package ||
        PrivateWorld->WorldType != Context.WorldType)
    {
        if (Loaded)
            Error = TEXT("stage loader did not activate its private world: ") + SourcePackage;
        // The caller tears down this context on failure. Keep that teardown
        // restricted to the world allocated here, never a foreign live world.
        Context.SetCurrentWorld(PrivateWorld);
        PrivateWorld->RemoveFromRoot();
        return false;
    }
    // Successful Game LoadMap expects the world to remain rooted; its normal
    // DestroyWorld/LoadMap teardown releases that root.
    return true;
}

FString HashBytes(const TArray<uint8> &Bytes)
{
    uint8 Digest[20];
    FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Digest);
    return BytesToHex(Digest, 20);
}

FString MakeCredentialDigest(const FString &Id, const FString &Secret)
{
    FTCHARToUTF8 Text(*(Id + TEXT(":") + Secret));
    return HashBytes(TArray<uint8>(reinterpret_cast<const uint8 *>(Text.Get()), Text.Length()));
}

void StopBattleAudio(ANightSkyGameState *Battle)
{
    if (!Battle || !Battle->AudioManager)
        return;
    auto Stop = [](UAudioComponent *Audio)
    {
        if (Audio)
        {
            Audio->Stop();
            Audio->SetSound(nullptr);
        }
    };
    for (auto *Audio : Battle->AudioManager->CommonAudioPlayers)
        Stop(Audio);
    for (auto *Audio : Battle->AudioManager->CharaAudioPlayers)
        Stop(Audio);
    for (auto *Audio : Battle->AudioManager->CharaVoicePlayers)
        Stop(Audio);
    Stop(Battle->AudioManager->AnnouncerVoicePlayer);
    Stop(Battle->AudioManager->MusicPlayer);
}

FString DescribeBattleState(ANightSkyGameState *Battle)
{
    FString Text = FString::Printf(TEXT("%d:%d:%d:%d:%d"),
                                   Battle->BattleState.FrameNumber,
                                   Battle->BattleState.RoundTimer,
                                   Battle->BattleState.Meter[0],
                                   Battle->BattleState.Meter[1],
                                   int32(Battle->BattleState.BattlePhase));
    Text += FString::Printf(TEXT("|rng:%u"), Battle->BattleState.RandomManager.GetSeed());
    for (auto *Player : Battle->Players)
        Text += FString::Printf(TEXT("|p%d:%d:%d:%d:%d:%d:%d:%d:%s:%s"),
                                Player->ObjNumber,
                                Player->PosX,
                                Player->PosY,
                                Player->SpeedX,
                                Player->SpeedY,
                                Player->CurrentHealth,
                                Player->Hitstop,
                                Player->Inputs,
                                *Player->GetCurrentStateName(StateMachine_Primary).ToString(),
                                *Player->CelName.ToString());
    for (auto *Object : Battle->Objects)
        if (Object->IsActive)
            Text += FString::Printf(TEXT("|o%d:%d:%d:%d:%d:%d:%s:%s"),
                                    Object->ObjNumber,
                                    Object->PosX,
                                    Object->PosY,
                                    Object->SpeedX,
                                    Object->SpeedY,
                                    Object->Hitstop,
                                    *Object->ObjectStateName.ToString(),
                                    *Object->CelName.ToString());
    return Text;
}

FString HashConfirmedFrame(const FRoomFrame &Frame)
{
    FBufferArchive Bytes;
    int64 Tick = Frame.ConfirmedTick;
    int32 A = Frame.Input1, B = Frame.Input2;
    Bytes << Tick << A << B;
    Bytes.Append(Frame.State);
    return HashBytes(Bytes);
}

FString CreateRoomIdentity()
{
    return FGuid::NewGuid().ToString(EGuidFormats::Digits);
}

FString GetRoomStorePath(const FString &Slot)
{
    return FPaths::ProjectSavedDir() / TEXT("SpectatorRooms") /
           (FPaths::MakeValidFileName(Slot) + TEXT(".room"));
}

bool WriteRoomFileAtomically(const FString &Path, const TArray<uint8> &Bytes)
{
    auto &Files = FPlatformFileManager::Get().GetPlatformFile();
    Files.CreateDirectoryTree(*FPaths::GetPath(Path));
    TUniquePtr<IFileHandle> Handle(Files.OpenWrite(*(Path + TEXT(".tmp"))));
    if (!Handle || !Handle->Write(Bytes.GetData(), Bytes.Num()) || !Handle->Flush(true))
        return false;
    Handle.Reset();
#if PLATFORM_WINDOWS
    // FWindowsPlatformFile::MoveFile uses MoveFileW, which cannot replace an
    // existing journal. Replace in one filesystem operation after flushing the
    // new bytes; deleting the acknowledged journal first creates a crash gap.
    const FString Destination = Files.ConvertToAbsolutePathForExternalAppForWrite(*Path);
    const FString Temporary = Files.ConvertToAbsolutePathForExternalAppForWrite(*(Path + TEXT(".tmp")));
    return !!::MoveFileExW(*Temporary, *Destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    return Files.MoveFile(*Path, *(Path + TEXT(".tmp")));
#endif
}

// A single recoverable payload extends the exact preceding main journal. The
// anchor is a separate immutable record, written only after payload durability.
struct FRoomConfirmation
{
    int32 Version = 1;
    FString Previous, Room, Match, Outcome;
    int32 FrameNumber = 0;
    bool HasFrame = false, Recovery = false;
    FRoomFrame Frame;

    void Serialize(FArchive &Ar)
    {
        Ar << Version << Previous << Room << Match << FrameNumber << HasFrame << Recovery << Outcome;
        if (HasFrame)
            Ar << Frame.Input1 << Frame.Input2 << Frame.State;
    }
};

TArray<uint8> CheckRoomBytes(const TArray<uint8> &Body)
{
    FTCHARToUTF8 Prefix(*HashBytes(Body));
    TArray<uint8> Result(reinterpret_cast<const uint8 *>(Prefix.Get()), Prefix.Length());
    Result.Append(Body);
    return Result;
}

bool UncheckRoomBytes(const TArray<uint8> &Envelope, TArray<uint8> &Body)
{
    if (Envelope.Num() < 41)
        return false;
    Body = TArray<uint8>(Envelope.GetData() + 40, Envelope.Num() - 40);
    FTCHARToUTF8 Prefix(*HashBytes(Body));
    return FMemory::Memcmp(Prefix.Get(), Envelope.GetData(), 40) == 0;
}

FString RoomEnvelopeDigest(const TArray<uint8> &Bytes)
{
    FString Digest;
    for (int32 I = 0; I < 40 && I < Bytes.Num(); ++I)
        Digest.AppendChar(TCHAR(Bytes[I]));
    return Digest;
}

TArray<uint8> MakeRoomAnchor(const TArray<uint8> &Payload, int64 Tick)
{
    FBufferArchive Body;
    int32 Version = 1;
    FString PayloadHash = HashBytes(Payload);
    Body << Version << PayloadHash << Tick;
    return CheckRoomBytes(Body);
}

// Missing or stale anchors belong to an unpublished payload. A malformed anchor
// fails closed instead of silently replacing a possibly published timestamp.
bool ReadRoomAnchor(const FString &Path, const TArray<uint8> &Payload, int64 &Tick)
{
    Tick = -1;
    if (!FPaths::FileExists(Path))
        return true;
    TArray<uint8> Bytes, Body;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path) || !UncheckRoomBytes(Bytes, Body))
        return false;
    FMemoryReader Reader(Body);
    int32 Version = 0;
    FString PayloadHash;
    int64 SavedTick = -1;
    Reader << Version << PayloadHash << SavedTick;
    if (Reader.IsError() || Reader.Tell() != Reader.TotalSize() || Version != 1 || SavedTick < 0)
        return false;
    if (PayloadHash == HashBytes(Payload))
        Tick = SavedTick;
    return true;
}

bool ApplyRoomConfirmation(URoomStore *Store, FRoomConfirmation Confirmation, int64 Tick)
{
    if (!Store || Confirmation.Version != 1 || Confirmation.Room != Store->Id || Store->Matches.IsEmpty())
        return false;
    auto &Match = Store->Matches.Last();
    if (Match.Id != Confirmation.Match || Match.OutcomeTick >= 0 || Match.Frames.IsEmpty() ||
        Confirmation.FrameNumber != Match.Frames.Num() - (Confirmation.HasFrame ? 0 : 1) ||
        (Confirmation.Recovery != !Confirmation.HasFrame) ||
        (Confirmation.Recovery && Confirmation.Outcome != TEXT("interrupted:authority")) ||
        (Confirmation.HasFrame && !Confirmation.Outcome.IsEmpty() &&
         Confirmation.Outcome != TEXT("completed:p1") && Confirmation.Outcome != TEXT("completed:p2") &&
         Confirmation.Outcome != TEXT("completed:draw")))
        return false;
    if (Confirmation.HasFrame)
    {
        FString Semantic;
        FMemoryReader Reader(Confirmation.Frame.State);
        Reader << Semantic;
        if (Reader.IsError() || Reader.Tell() != Reader.TotalSize() || Semantic.IsEmpty())
            return false;
        Confirmation.Frame.ConfirmedTick = Tick;
        Confirmation.Frame.Digest = HashConfirmedFrame(Confirmation.Frame);
        Match.Frames.Add(Confirmation.Frame);
    }
    Store->Recovering |= Confirmation.Recovery;
    if (!Confirmation.Outcome.IsEmpty())
    {
        Match.Outcome = Confirmation.Outcome;
        Match.OutcomeTick = Tick;
        Store->Paused = false;
        Store->Offers.Empty();
        for (auto &Member : Store->Members)
            Member.Ready = false;
        int32 Finished = 0;
        for (int32 I = Store->Matches.Num() - 1; I >= 0; --I)
            if (Store->Matches[I].OutcomeTick >= 0 && ++Finished > 2)
                Store->Matches.RemoveAt(I);
    }
    ++Store->Revision;
    return true;
}

void RemoveRoomConfirmation(const FString &Path)
{
    auto &Files = FPlatformFileManager::Get().GetPlatformFile();
    // Main already contains the committed record. Leftovers are harmless because
    // the predecessor and payload hashes prevent stale replay or anchor reuse.
    Files.DeleteFile(*(Path + TEXT(".pending")));
    Files.DeleteFile(*(Path + TEXT(".anchor")));
}

TArray<uint8> EncodeRoomStore(URoomStore *Store)
{
    TArray<uint8> Bytes;
    UGameplayStatics::SaveGameToMemory(Store, Bytes);
    const FString Digest = HashBytes(Bytes);
    FTCHARToUTF8 Prefix(*Digest);
    TArray<uint8> Result(reinterpret_cast<const uint8 *>(Prefix.Get()), Prefix.Length());
    Result.Append(Bytes);
    return Result;
}

URoomStore *DecodeRoomStore(const TArray<uint8> &Envelope)
{
    if (Envelope.Num() < 41)
        return nullptr;
    TArray<uint8> Body(Envelope.GetData() + 40, Envelope.Num() - 40);
    const FString Digest = HashBytes(Body);
    FTCHARToUTF8 Prefix(*Digest);
    if (FMemory::Memcmp(Prefix.Get(), Envelope.GetData(), 40) != 0)
        return nullptr;
    return Cast<URoomStore>(UGameplayStatics::LoadGameFromMemory(Body));
}
// Internal consumers only need the compact journal for validation, saving and
// ordinary replay playback. Expanding every public input prefix is unnecessary.
bool ReadCompactReplay(const TArray<uint8> &Bytes, FRoomMatch &Match)
{
    auto *Replay = DecodeRoomStore(Bytes);
    if (!Replay || Replay->Matches.Num() != 1 || Replay->Matches[0].Id.IsEmpty() ||
        Replay->Matches[0].OutcomeTick < 0 || Replay->Matches[0].Frames.IsEmpty())
        return false;
    for (const auto &Frame : Replay->Matches[0].Frames)
    {
        if (HashConfirmedFrame(Frame) != Frame.Digest)
            return false;
        FString Semantic;
        FMemoryReader Reader(Frame.State);
        Reader << Semantic;
        if (Reader.IsError() || Reader.Tell() != Reader.TotalSize() || Semantic.IsEmpty())
            return false;
    }
    const auto &Initial = Replay->Matches[0].Frames[0];
    if (Initial.Input1 != 0 || Initial.Input2 != 0)
        return false;
    Match = Replay->Matches[0];
    return true;
}

bool RestoreMatchStartup(UNightSkyGameInstance *Game, const FRoomMatch &Match)
{
    UNightSkySettingsInfo *Settings = nullptr;
    if (!Match.StartupSettings.IsEmpty())
    {
        Settings = Cast<UNightSkySettingsInfo>(UGameplayStatics::LoadGameFromMemory(Match.StartupSettings));
        if (!Settings)
            return false;
    }
    Game->SettingsInfo = Settings;
    Game->AnnouncerData = Match.AnnouncerData;
    Game->MusicData = Match.MusicData;
    return true;
}

} // namespace

bool USpectatorRoom::Create(const FString &Slot, const FString &Organizer, const FString &Secret, int32 Delay)
{
    if (Store || Slot.IsEmpty() || Slot != FPaths::MakeValidFileName(Slot) || Delay < 0 || Delay > 600 || Organizer.IsEmpty() || Secret.IsEmpty())
        return false;
    if (FPaths::FileExists(GetRoomStorePath(Slot)))
        return false;
    Store = NewObject<URoomStore>(this);
    SaveSlot = Slot;
    if (auto *GameInstance = Cast<UNightSkyGameInstance>(GetGameInstance()))
    {
        Store->Selection = GameInstance->BattleData;
        Store->Content = InspectContent(Store->Selection, GameInstance->BattleVersion, GameInstance->AnnouncerData, GameInstance->MusicData);
    }
    Store->Id = CreateRoomIdentity();
    Store->Organizer = Organizer;
    Store->Delay = Delay;
    Store->Epoch = FDateTime::UtcNow().GetTicks();
    MonotonicEpoch = FPlatformTime::Seconds();
    BaseTick = 0;
    FRoomMember Host;
    Host.Id = Organizer;
    Host.Membership = CreateRoomIdentity();
    Host.Credential = MakeCredentialDigest(Organizer, Secret);
    Store->Members.Add(Host);
    if (Save())
        return true;
    Store = nullptr;
    return false;
}

bool USpectatorRoom::Recover(const FString &Slot)
{
    DrainDepartures();
    if (!DeferredDepartures.IsEmpty() || !FlushConfirmation() || Slot.IsEmpty() || Slot != FPaths::MakeValidFileName(Slot))
        return false;
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *GetRoomStorePath(Slot)))
        return false;
    URoomStore *Loaded = DecodeRoomStore(Bytes);
    if (!Loaded)
        return false;
    const FString Path = GetRoomStorePath(Slot);
    TArray<uint8> Payload;
    if (FPaths::FileExists(Path + TEXT(".pending")))
    {
        TArray<uint8> Body;
        if (!FFileHelper::LoadFileToArray(Payload, *(Path + TEXT(".pending"))) || !UncheckRoomBytes(Payload, Body))
            return false;
        FRoomConfirmation Confirmation;
        FMemoryReader Reader(Body);
        Confirmation.Serialize(Reader);
        if (Reader.IsError() || Reader.Tell() != Reader.TotalSize() || Confirmation.Version != 1)
            return false;
        if (Confirmation.Previous == RoomEnvelopeDigest(Bytes))
        {
            int64 Tick = -1;
            if (!ReadRoomAnchor(Path + TEXT(".anchor"), Payload, Tick))
                return false;
            const bool NeedsAnchor = Tick < 0;
            if (NeedsAnchor)
                Tick = Clock ? Clock() : FMath::Max<int64>(
                    0, (FDateTime::UtcNow().GetTicks() - Loaded->Epoch) * 60 / ETimespan::TicksPerSecond);
            if (!ApplyRoomConfirmation(Loaded, Confirmation, Tick))
                return false;
            // Only a never-published payload needs an anchor on recovery. Existing
            // anchors are preserved byte for byte, including after compaction failure.
            if (NeedsAnchor && !WriteRoomFileAtomically(Path + TEXT(".anchor"), MakeRoomAnchor(Payload, Tick)))
                return false;
            Bytes = EncodeRoomStore(Loaded);
            if (!WriteRoomFileAtomically(Path, Bytes))
                return false;
        }
        RemoveRoomConfirmation(Path);
    }
    const auto PreviousStore = Store;
    const auto PreviousDurable = LastDurable;
    const FString PreviousSlot = SaveSlot;
    const auto PreviousBattle = Battle;
    const auto PreviousInputs = PendingInputs;
    const double PreviousEpoch = MonotonicEpoch;
    const int64 PreviousBase = BaseTick;
    Store = Loaded;
    Store->Recovering = true;
    LastDurable = Bytes;
    SaveSlot = Slot;
    MonotonicEpoch = FPlatformTime::Seconds();
    BaseTick = FMath::Max<int64>(
        0, (FDateTime::UtcNow().GetTicks() - Store->Epoch) * 60 / ETimespan::TicksPerSecond);
    PendingInputs.Empty();
    Battle.Reset();
    // Recovery of an active match is itself a recoverable outcome commitment.
    // A post-payload storage stall keeps the loaded room pending; it cannot undo
    // that commitment or expose it before its immutable anchor is durable.
    if (IsActive())
    {
        if (CommitConfirmation(nullptr, TEXT("interrupted:authority")))
            return true;
    }
    else
    {
        Retain();
        if (Save())
            return true;
    }
    Store = PreviousStore;
    LastDurable = PreviousDurable;
    SaveSlot = PreviousSlot;
    Battle = PreviousBattle;
    PendingInputs = PreviousInputs;
    MonotonicEpoch = PreviousEpoch;
    BaseTick = PreviousBase;
    return false;
}

bool USpectatorRoom::FlushConfirmation()
{
    const FString Path = GetRoomStorePath(SaveSlot);
    if (ConfirmingStore)
    {
        if (!WriteRoomFileAtomically(Path + TEXT(".anchor"), ConfirmationAnchor))
            return false;
        Store = ConfirmingStore;
        ConfirmingStore = nullptr;
        ConfirmationAnchor.Empty();
        // This is now the logical durable state, recoverable from main plus WAL.
        // A full-main write is compaction, never a reason to roll this pair back.
        LastDurable = EncodeRoomStore(Store);
        ConfirmationNeedsCompaction = true;
    }
    if (ConfirmationNeedsCompaction)
    {
        if (!WriteRoomFileAtomically(Path, LastDurable))
            return false;
        ConfirmationNeedsCompaction = false;
        RemoveRoomConfirmation(Path);
    }
    return true;
}

bool USpectatorRoom::CommitConfirmation(const FRoomFrame *Frame, const FString &Outcome)
{
    if (!FlushConfirmation() || !IsActive() || LastDurable.IsEmpty())
        return false;
    FRoomConfirmation Confirmation;
    Confirmation.Previous = RoomEnvelopeDigest(LastDurable);
    Confirmation.Room = Store->Id;
    Confirmation.Match = Store->Matches.Last().Id;
    Confirmation.HasFrame = Frame != nullptr;
    Confirmation.Recovery = !Frame;
    Confirmation.FrameNumber = Store->Matches.Last().Frames.Num() - (Frame ? 0 : 1);
    Confirmation.Outcome = Outcome;
    if (Frame)
        Confirmation.Frame = *Frame;
    FBufferArchive Body;
    Confirmation.Serialize(Body);
    const TArray<uint8> Payload = CheckRoomBytes(Body);
    if (!WriteRoomFileAtomically(GetRoomStorePath(SaveSlot) + TEXT(".pending"), Payload))
        return false;
    // From here the pair/outcome is recoverable even if no anchor was published.
    // Sample once after payload durability; slow anchor/main writes cannot move
    // this event earlier, and retries must not restart an existing live anchor.
    const int64 Tick = RoomTick();
    const bool Applied = ApplyRoomConfirmation(Store, Confirmation, Tick);
    check(Applied); // Constructed from this active store and its captured native frame.
    ConfirmationAnchor = MakeRoomAnchor(Payload, Tick);
    ConfirmingStore = Store;
    if (!FlushConfirmation() && ConfirmingStore)
        Store = DecodeRoomStore(LastDurable); // Serve only the preceding anchored prefix.
    return true; // Payload committed; a pending anchor freezes rather than rolls back native state.
}

bool USpectatorRoom::Save()
{
    if (!Store || ConfirmingStore || ConfirmationNeedsCompaction)
        return false;
    if (ApplyingCommand)
        return true;
    ++Store->Revision;
    const TArray<uint8> Bytes = EncodeRoomStore(Store);
    if (WriteRoomFileAtomically(GetRoomStorePath(SaveSlot), Bytes))
    {
        LastDurable = Bytes;
        return true;
    }
    if (!LastDurable.IsEmpty())
        Store = DecodeRoomStore(LastDurable);
    return false;
}

int64 USpectatorRoom::RoomTick() const
{
    if (Clock)
        return Clock();
    return Store ? BaseTick + int64(FMath::Max(0.0, FPlatformTime::Seconds() - MonotonicEpoch) * 60.0) : 0;
}

FRoomMember *USpectatorRoom::Member(const FString &Identity)
{
    return Store ? Store->Members.FindByPredicate(
                       [&](const FRoomMember &M)
                       {
                           return M.Id == Identity;
                       })
                 : nullptr;
}

FRoomMatch *USpectatorRoom::FindMatch(const FString &Identity)
{
    return Store ? Store->Matches.FindByPredicate(
                       [&](const FRoomMatch &M)
                       {
                           return M.Id == Identity;
                       })
                 : nullptr;
}

bool USpectatorRoom::IsActive() const
{
    return Store && Store->Matches.Num() && Store->Matches.Last().OutcomeTick < 0;
}

bool USpectatorRoom::Authenticate(const FString &Identity,
                                  const FString &Secret,
                                  const TMap<FString, FString> &Content)
{
    DrainDepartures();
    if (!DeferredDepartures.IsEmpty() || !FlushConfirmation() || !Store || Identity.IsEmpty() || Secret.IsEmpty())
        return false;
    auto *RequestingMember = Member(Identity);
    if (RequestingMember && RequestingMember->Credential != MakeCredentialDigest(Identity, Secret))
        return false;
    if (!RequestingMember)
    {
        FRoomMember New;
        New.Id = Identity;
        New.Membership = CreateRoomIdentity();
        New.Credential = MakeCredentialDigest(Identity, Secret);
        if (Store->Matches.Num())
            New.SelectedMatch = Store->Matches.Last().Id;
        Store->Members.Add(New);
        RequestingMember = &Store->Members.Last();
    }
    if (!RequestingMember->Present)
        RequestingMember->Membership = CreateRoomIdentity();
    RequestingMember->Present = true;
    RequestingMember->Ready = false;
    RequestingMember->Content = Content;
    RequestingMember->Diagnostic.Empty();
    ++RequestingMember->Acknowledgement;
    return Save();
}

int32 USpectatorRoom::Edge(const FRoomMatch &Match) const
{
    int32 Result = -1;
    for (int32 I = 0; I < Match.Frames.Num(); ++I)
    {
        if (RoomTick() - Match.Frames[I].ConfirmedTick < Store->Delay)
            break;
        Result = I;
    }
    return Result;
}

bool USpectatorRoom::ContentValid(FRoomMember &RequestingMember, const TMap<FString, FString> &Required)
{
    for (const auto &Pair : Required)
    {
        const FString *Local = RequestingMember.Content.Find(Pair.Key);
        if (!Local || Local->IsEmpty() || Pair.Value.IsEmpty() || *Local != Pair.Value)
        {
            RequestingMember.Diagnostic = TEXT("content:") + Pair.Key;
            return false;
        }
    }
    RequestingMember.Diagnostic.Empty();
    return true;
}

TMap<FString, FString> USpectatorRoom::InspectRequiredContent(const TMap<FString, FString> &Required,
                                                              const FString &GameplayRevision)
{
    TMap<FString, FString> Result;
    for (const auto &Entry : Required)
    {
        if (Entry.Key == TEXT("gameplay"))
        {
            Result.Add(Entry.Key, GameplayRevision);
            continue;
        }
        FString Path = Entry.Key;
        if (Path.StartsWith(TEXT("package:")))
            Path.RightChopInline(8);
        else if (Path.StartsWith(TEXT("map:")))
            Path.RightChopInline(4);
        Path = FPackageName::ObjectPathToPackageName(Path);
        FString Filename;
        TArray<uint8> Bytes;
        const bool Valid = FPackageName::IsValidLongPackageName(Path) &&
                           FPackageName::DoesPackageExist(Path, &Filename) &&
                           FFileHelper::LoadFileToArray(Bytes, *Filename);
        Result.Add(Entry.Key, Valid ? HashBytes(Bytes) : TEXT(""));
    }
    return Result;
}

TMap<FString, FString> USpectatorRoom::InspectContent(const FBattleData &Config,
                                                      const FString &GameplayRevision)
{
    return InspectContent(Config, GameplayRevision, nullptr, nullptr);
}

TMap<FString, FString> USpectatorRoom::InspectContent(const FBattleData &Config,
                                                      const FString &GameplayRevision, USoundData *AnnouncerData, USoundData *MusicData)
{
    TMap<FString, FString> Out;
    Out.Add(TEXT("gameplay"), GameplayRevision);
    auto &Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TSet<FString> Visited;
    TFunction<void(const FString &, const FString &)> AddPackage;
    AddPackage = [&](const FString &Package, const FString &Key)
    {
        if (Package.StartsWith(TEXT("/Script/")))
            return; // Native code is covered by the gameplay revision.
        FString Filename;
        TArray<uint8> Bytes;
        if (!FPackageName::IsValidLongPackageName(Package) ||
            !FPackageName::DoesPackageExist(Package, &Filename) ||
            !FFileHelper::LoadFileToArray(Bytes, *Filename))
        {
            Out.Add(Key, TEXT(""));
            return;
        }
        Out.Add(Key, HashBytes(Bytes));
        if (Visited.Contains(Package))
            return;
        Visited.Add(Package);
        // Include on-disk hard and soft package references, even when the asset is not loaded.
        Registry.ScanFilesSynchronous({Filename}, true);
        TArray<FName> Dependencies;
        Registry.GetDependencies(FName(*Package), Dependencies);
        for (const FName Dependency : Dependencies)
        {
            const FString Name = Dependency.ToString();
            AddPackage(Name, TEXT("package:") + Name);
        }
    };
    auto Add = [&](const FString &ObjectPath)
    {
        AddPackage(FPackageName::ObjectPathToPackageName(ObjectPath), ObjectPath);
    };
    if (AnnouncerData) Add(AnnouncerData->GetPathName());
    if (MusicData) Add(MusicData->GetPathName());
    for (auto Chara : Config.PlayerListP1)
        Add(Chara.ToSoftObjectPath().ToString());
    for (auto Chara : Config.PlayerListP2)
        Add(Chara.ToSoftObjectPath().ToString());
    Add(Config.Stage ? Config.Stage->GetPathName() : TEXT("missing-stage"));
    // StageURL is a travel string, so it is not an asset-registry dependency.
    if (Config.Stage)
    {
        FString Map = Config.Stage->StageURL;
        int32 Option = INDEX_NONE;
        if (Map.FindChar(TEXT('?'), Option))
            Map.LeftInline(Option);
        AddPackage(FPackageName::ObjectPathToPackageName(Map), TEXT("map:") + Map);
    }
    return Out;
}

void USpectatorRoom::Retain()
{
    int32 Finished = 0;
    for (int32 I = Store->Matches.Num() - 1; I >= 0; --I)
        if (Store->Matches[I].OutcomeTick >= 0 && ++Finished > 2)
            Store->Matches.RemoveAt(I);
}

void USpectatorRoom::Finish(const FString &Outcome)
{
    if (!CommittingFrame && !FlushConfirmation())
        return;
    if (!IsActive())
        return;
    if (CommittingFrame)
    {
        PendingOutcome = Outcome;
        return;
    }
    Store->Matches.Last().Outcome = Outcome;
    Store->Matches.Last().OutcomeTick =
        (Outcome.StartsWith(TEXT("completed")) && Store->Matches.Last().Frames.Num())
            ? Store->Matches.Last().Frames.Last().ConfirmedTick
            : RoomTick();
    Store->Paused = false;
    PendingInputs.Empty();
    Store->Offers.Empty();
    for (auto &M : Store->Members)
        M.Ready = false;
    Retain();
    Save();
}

void USpectatorRoom::Depart(const FString &Identity)
{
    auto *M = Member(Identity);
    if (!M || !M->Present)
        return;
    if (!DeferredDepartures.Contains(Identity))
    {
        DeferredDepartures.Add(Identity, RoomTick());
        DeferredDepartureMemberships.Add(Identity, M->Membership);
    }
    DrainDepartures();
}

void USpectatorRoom::DrainDepartures()
{
    if (DeferredDepartures.IsEmpty() || !FlushConfirmation())
        return;
    auto Departures = MoveTemp(DeferredDepartures);
    auto Memberships = MoveTemp(DeferredDepartureMemberships);
    DeferredDepartures.Empty();
    DeferredDepartureMemberships.Empty();
    TArray<FString> Identities;
    Departures.GetKeys(Identities);
    Identities.Sort([&](const FString &A, const FString &B)
    {
        return Departures[A] == Departures[B] ? A < B : Departures[A] < Departures[B];
    });
    for (int32 I = 0; I < Identities.Num(); ++I)
    {
        const auto &Identity = Identities[I];
        auto *Member = this->Member(Identity);
        if (!Member || Member->Membership != Memberships.FindRef(Identity) || DepartAt(Identity, Departures[Identity]))
            continue;
        // A later departure must never overtake an earlier failed save and
        // become the first interruption. Preserve every unprocessed event.
        for (; I < Identities.Num(); ++I)
        {
            DeferredDepartures.Add(Identities[I], Departures[Identities[I]]);
            DeferredDepartureMemberships.Add(Identities[I], Memberships[Identities[I]]);
        }
        break;
    }
}

bool USpectatorRoom::DepartAt(const FString &Identity, int64 AcceptedTick)
{
    auto *M = Member(Identity);
    if (!M || !M->Present)
        return true;
    const bool OuterTransaction = ApplyingCommand;
    ApplyingCommand = true;
    if (M->Mode == TEXT("live"))
        if (auto *Selected = FindMatch(M->SelectedMatch))
            M->Cursor = Edge(*Selected);
    if (M->Seat >= 0)
    {
        const bool Interrupted = IsActive();
        Finish(TEXT("interrupted:disconnect"));
        if (Interrupted)
            Store->Matches.Last().OutcomeTick = AcceptedTick;
        M = Member(Identity);
        M->Seat = -1;
        M->Assignment = CreateRoomIdentity();
        M->Ready = false;
    }
    M->Present = false;
    Store->Queue.Remove(Identity);
    Store->Offers.Remove(Identity);
    if (!OuterTransaction)
        ++M->Acknowledgement;
    ApplyingCommand = OuterTransaction;
    const bool Saved = Save();
    if (Saved && !OuterTransaction)
        ContentRetryMatches.Remove(Identity);
    return Saved;
}

FRoomDelivery USpectatorRoom::Observe(const FString &Identity)
{
    FRoomDelivery Delivery;
    auto *RequestingMember = Member(Identity);
    if (!RequestingMember || !RequestingMember->Present)
    {
        Delivery.Status = TEXT("unauthenticated");
        return Delivery;
    }
    Delivery.Room = Store->Id;
    Delivery.Role = Identity == Store->Organizer
                        ? TEXT("organizer")
                        : (RequestingMember->Seat >= 0 ? TEXT("fighter") : TEXT("spectator"));
    Delivery.Membership = RequestingMember->Membership;
    Delivery.Assignment = RequestingMember->Assignment;
    // The stored recovery marker records the interrupted room until a new match.
    // Pending recovery ends once its durable publication/compaction has finished.
    Delivery.Recovering = (ConfirmingStore && ConfirmingStore->Recovering) ||
                          (Store->Recovering && ConfirmationNeedsCompaction);
    Delivery.SelectedStage = Store->Selection.Stage ? Store->Selection.Stage->GetPathName() : TEXT("");
    for (auto Asset : Store->Selection.PlayerListP1)
        Delivery.SelectedCharacters.Add(Asset.ToSoftObjectPath().ToString());
    for (auto Asset : Store->Selection.PlayerListP2)
        Delivery.SelectedCharacters.Add(Asset.ToSoftObjectPath().ToString());
    if (const auto *Offer = Store->Offers.Find(Identity))
    {
        Delivery.Offer = Offer->Id;
        Delivery.OfferedSeat = Offer->Seat;
    }
    Delivery.Locked = Store->Locked;
    Delivery.Paused = Store->Paused || ConfirmingStore || ConfirmationNeedsCompaction || !DeferredDepartures.IsEmpty();
    Delivery.Acknowledgement = RequestingMember->Acknowledgement;
    for (const auto &Retained : Store->Matches)
        Delivery.RetainedMatches.Add(Retained.Id);
    // Roster is public membership only. Credentials, other cursors and content stay authority-local.
    for (const auto &Source : Store->Members)
    {
        FRoomMember Public;
        Public.Id = Source.Id;
        Public.Seat = Source.Seat;
        Public.Assignment = Source.Assignment;
        Public.Present = Source.Present;
        Public.Ready = Source.Ready;
        Delivery.Roster.Add(Public.Id + TEXT("|") + FString::FromInt(Public.Seat) + TEXT("|") +
                            Public.Assignment + TEXT("|") +
                            (Public.Present ? TEXT("present") : TEXT("absent")));
    }
    const bool Playing = RequestingMember->Seat >= 0 && IsActive();
    const bool Fighter = RequestingMember->Seat >= 0 && Store->Matches.Num() > 0 &&
                         Store->Matches.Last().Participants.Contains(Identity);
    Delivery.FighterSeat = Playing ? RequestingMember->Seat : -1;
    FRoomMatch *Match = Fighter ? &Store->Matches.Last() : FindMatch(RequestingMember->SelectedMatch);
    Delivery.Match = RequestingMember->SelectedMatch;
    Delivery.Mode = RequestingMember->Mode;
    Delivery.RequiredContent =
        RequestingMember->Seat >= 0 ? Store->Content : (Match ? Match->Content : Store->Content);
    // Keep a rejected selection's refresh requirements through automatic and
    // explicit reauthentication, without changing the acknowledged cursor.
    if (auto *Requested = FindMatch(ContentRetryMatches.FindRef(Identity));
        Requested && Edge(*Requested) >= 0)
        for (const auto &Required : Requested->Content)
            Delivery.RequiredContent.Add(Required.Key, Required.Value);
    if (!Match)
    {
        Delivery.Status = TEXT("history unavailable");
        return Delivery;
    }
    Delivery.Match = Match->Id;
    FRoomMember Inspected = *RequestingMember;
    if (!ContentValid(Inspected, Match->Content))
    {
        Delivery.Status = Inspected.Diagnostic;
        return Delivery;
    }
    Delivery.Edge = Fighter ? Match->Frames.Num() - 1 : Edge(*Match);
    Delivery.Frame = Fighter || RequestingMember->Mode == TEXT("live")
                         ? Delivery.Edge : RequestingMember->Cursor;
    if (Delivery.Frame < 0 || Delivery.Frame > Delivery.Edge)
    {
        Delivery.Status = TEXT("buffering");
        return Delivery;
    }
    auto *Setup = NewObject<URoomStore>();
    FRoomMatch SetupMatch;
    SetupMatch.Id = Match->Id;
    SetupMatch.Configuration = Match->Configuration;
    SetupMatch.StartupSettings = Match->StartupSettings;
    SetupMatch.AnnouncerData = Match->AnnouncerData;
    SetupMatch.MusicData = Match->MusicData;
    SetupMatch.Training = Match->Training;
    SetupMatch.BattleClass = Match->BattleClass;
    SetupMatch.Content = Match->Content;
    if (Match->Frames.Num())
        SetupMatch.Frames.Add(Match->Frames[0]);
    Setup->Matches.Add(SetupMatch);
    Delivery.Setup = EncodeRoomStore(Setup);
    Delivery.Gameplay = Match->Frames[Delivery.Frame];
    FString Semantic;
    FMemoryReader Stored(Delivery.Gameplay.State);
    Stored << Semantic;
    if (Stored.IsError() || HashConfirmedFrame(Delivery.Gameplay) != Delivery.Gameplay.Digest)
    {
        Delivery.Gameplay = FRoomFrame();
        Delivery.Status = TEXT("integrity error");
        return Delivery;
    }
    TArray<int32> InputsA, InputsB;
    for (int32 I = 0; I <= Delivery.Frame; ++I)
    {
        if (HashConfirmedFrame(Match->Frames[I]) != Match->Frames[I].Digest)
        {
            Delivery.Gameplay = FRoomFrame();
            Delivery.Setup.Empty();
            Delivery.Status = TEXT("integrity error");
            return Delivery;
        }
        if (I == 0)
            continue;
        InputsA.Add(Match->Frames[I].Input1);
        InputsB.Add(Match->Frames[I].Input2);
    }
    FBufferArchive Journal;
    FString MatchId = Match->Id;
    Journal << MatchId << InputsA << InputsB << Semantic;
    Delivery.Gameplay.State = Journal;
    Delivery.Gameplay.Digest = HashConfirmedFrame(Delivery.Gameplay);
    Delivery.Status = TEXT("ok");
    if (Match->OutcomeTick >= 0 && (Fighter || RoomTick() - Match->OutcomeTick >= Store->Delay))
        Delivery.Outcome = Match->Outcome;
    return Delivery;
}

void USpectatorRoom::PlaybackTick(const FString &Identity)
{
    DrainDepartures();
    if (!DeferredDepartures.IsEmpty() || !FlushConfirmation())
        return;
    auto *RequestingMember = Member(Identity);
    if (!RequestingMember || !RequestingMember->Present || RequestingMember->Mode != TEXT("playing"))
        return;
    auto *Match = FindMatch(RequestingMember->SelectedMatch);
    if (!Match)
        return;
    const int32 Bound = Edge(*Match);
    if (RequestingMember->Cursor >= Bound)
        return;
    ++RequestingMember->Cursor;
    ++RequestingMember->Acknowledgement;
    Save();
}

FRoomDelivery USpectatorRoom::Command(const FString &Identity, const FRoomCommand &Request)
{
    DrainDepartures();
    if (Request.Operation != TEXT("observe") && (!DeferredDepartures.IsEmpty() || !FlushConfirmation()))
        return RejectCommand(Identity, Request, TEXT("storage pending"));
    auto *RequestingMember = Member(Identity);
    if (!RequestingMember || !RequestingMember->Present)
    {
        FRoomDelivery Delivery;
        Delivery.Status = TEXT("unauthenticated");
        Delivery.RequestNonce = Request.Nonce;
        return Delivery;
    }
    if (Request.Operation == TEXT("observe"))
    {
        auto Delivery = Observe(Identity);
        Delivery.RequestNonce = Request.Nonce;
        return Delivery;
    }
    if (!Request.Membership.IsEmpty() && Request.Membership != RequestingMember->Membership)
        return RejectCommand(Identity, Request, TEXT("stale membership"));
    if (Request.Nonce.IsEmpty())
        return RejectCommand(Identity, Request, TEXT("nonce required"));
    if (RequestingMember->Consumed.Contains(Request.Nonce))
        return RejectCommand(Identity, Request, TEXT("duplicate"));
    // Keep membership, nonces and the mutation in one durable transaction.
    const TArray<uint8> Before = EncodeRoomStore(Store);
    const auto PreviousInputs = PendingInputs;
    const float PreviousAccumulator = Accumulator;
    ApplyingCommand = true;
    const auto Response = ApplyCommand(Identity, Request, RequestingMember);
    ApplyingCommand = false;
    if (Response.IsSet() && Response.GetValue().Status != TEXT("exported"))
    {
        Store = DecodeRoomStore(Before);
        PendingInputs = PreviousInputs;
        Accumulator = PreviousAccumulator;
        return Response.GetValue();
    }
    RequestingMember = Member(Identity);
    RequestingMember->Consumed.Add(Request.Nonce);
    ++RequestingMember->Acknowledgement;
    if (!Save())
    {
        Store = DecodeRoomStore(Before);
        PendingInputs = PreviousInputs;
        Accumulator = PreviousAccumulator;
        return RejectCommand(Identity, Request, TEXT("storage failure"));
    }
    if (Request.Operation == TEXT("select-match") || Request.Operation == TEXT("leave"))
        ContentRetryMatches.Remove(Identity);
    auto Delivery = Response.IsSet() ? Response.GetValue() : Observe(Identity);
    Delivery.Status = Response.IsSet() ? TEXT("exported") : TEXT("accepted");
    Delivery.RequestNonce = Request.Nonce;
    Delivery.Acknowledgement = Member(Identity)->Acknowledgement;
    if (Request.Operation == TEXT("start"))
        if (auto *GameInstance = Cast<UNightSkyGameInstance>(GetGameInstance()))
        {
            GameInstance->BattleData = Store->Matches.Last().Configuration;
            // Room seats always belong to the two authenticated human fighters.
            GameInstance->IsCPUBattle = false;
            Battle.Reset();
            GameInstance->TravelToVSInfo();
        }
    return Delivery;
}

FString USpectatorRoom::InitializeMatch(FRoomMatch &Match)
{
    auto *Source = Cast<UNightSkyGameInstance>(GetGameInstance());
    if (!Source || !Match.Configuration.Stage)
        return TEXT("content:battle configuration");
    auto *InitialGame = NewObject<URoomPlaybackGameInstance>(GEngine);
    InitialGame->AddToRoot();
    InitialGame->InitializeStandalone();
    ON_SCOPE_EXIT
    {
        InitialGame->IsReplay = true;
        auto *World = InitialGame->GetWorld();
        if (World)
        {
            World->EndPlay(EEndPlayReason::Quit);
            World->DestroyWorld(false);
        }
        InitialGame->Shutdown();
        if (World)
            GEngine->DestroyWorldContext(World);
        InitialGame->RemoveFromRoot();
    };
    InitialGame->BattleData = Match.Configuration;
    InitialGame->IsTraining = Match.Training;
    InitialGame->IsCPUBattle = false;
    InitialGame->BattleVersion = Source->BattleVersion;
    if (!RestoreMatchStartup(InitialGame, Match))
        return TEXT("content:startup settings");
    InitialGame->FighterRunner = LocalPlay;
    InitialGame->GetSubsystem<USpectatorRoom>()->EnableClient();
    FString Error;
    const bool Loaded = LoadPrivateRoomWorld(InitialGame, Match.Configuration.Stage->StageURL, Error);
    if (!Loaded)
        return TEXT("content:stage ") + Error;
    ANightSkyGameState *InitialBattle = nullptr;
    if (TActorIterator<ANightSkyGameState> It(InitialGame->GetWorld()); It)
    {
        InitialBattle = *It;
    }
    if (!InitialBattle || InitialBattle->Players.Num() < 2)
        return TEXT("content:battle initialization");
    Match.BattleClass = InitialBattle->GetClass();
    Match.StartTick = RoomTick();
    FString Semantic = DescribeBattleState(InitialBattle);
    FBufferArchive Bytes;
    Bytes << Semantic;
    FRoomFrame Initial;
    Initial.ConfirmedTick = Match.StartTick;
    Initial.State = Bytes;
    Initial.Digest = HashConfirmedFrame(Initial);
    Match.Frames.Add(Initial);
    return TEXT("ok");
}

void USpectatorRoom::BindBattle(ANightSkyGameState *InBattle)
{
    if (!FlushConfirmation())
        return;
    if (!InBattle || Battle.Get() == InBattle)
        return;
    Battle = InBattle;
    if (IsActive() && !Store->Matches.Last().Frames.IsEmpty())
    {
        const auto &Match = Store->Matches.Last();
        FString Initial;
        FMemoryReader Reader(Match.Frames[0].State);
        Reader << Initial;
        if (Reader.IsError() || Match.BattleClass != InBattle->GetClass() ||
            DescribeBattleState(InBattle) != Initial)
            Finish(TEXT("interrupted:initialization"));
    }
}

bool USpectatorRoom::Capture(int32 Input1, int32 Input2)
{
    if (!Battle.IsValid() || !IsActive())
        return false;
    // Store one compact semantic record per confirmed pair. Delivery constructs only the released prefix.
    FString Semantic = DescribeBattleState(Battle.Get());
    FBufferArchive Ar;
    Ar << Semantic;
    FRoomFrame Frame;
    Frame.Input1 = Input1;
    Frame.Input2 = Input2;
    Frame.State = Ar;
    const bool Committed = CommitConfirmation(&Frame, PendingOutcome);
    if (Committed)
        PendingOutcome.Empty();
    return Committed;
}

void USpectatorRoom::BattleTick(float Delta)
{
    DrainDepartures();
    if (!DeferredDepartures.IsEmpty() || !FlushConfirmation())
    {
        Accumulator = 0;
        return;
    }
    if (!IsActive() || !Battle.IsValid() || Store->Paused)
    {
        Accumulator = 0;
        return;
    }
    Accumulator = FMath::Min(Accumulator + Delta, OneFrame);
    if (Accumulator < OneFrame || PendingInputs.Num() != 2)
        return;
    const int32 A = PendingInputs[0], B = PendingInputs[1];
    PendingInputs.Empty();
    Accumulator = 0;
    FRollbackData PreviousState;
    int32 PreviousChecksum = 0;
    Battle->SaveGameState(PreviousState, &PreviousChecksum);
    CommittingFrame = true;
    Battle->UpdateGameState(A, B, false);
    // End the retained timeline on the pair that decides the result. Native
    // victory timers and animations may continue in ordinary non-room play.
    // Keep this pair's native state unchanged so every replay reproduces it.
    if (!Store->Matches.Last().Training && PendingOutcome.IsEmpty())
    {
        switch (Battle->GetDecidedWinSide())
        {
        case WIN_P1: Finish(TEXT("completed:p1")); break;
        case WIN_P2: Finish(TEXT("completed:p2")); break;
        case WIN_Draw: Finish(TEXT("completed:draw")); break;
        default: break;
        }
    }
    CommittingFrame = false;
    if (!Capture(A, B))
    {
        StopBattleAudio(Battle.Get());
        Battle->LoadGameState(PreviousState);
        Battle->FinishResimulation();
        PendingOutcome.Empty();
        Store->Paused = true;
        return;
    }
    if (!PendingOutcome.IsEmpty())
    {
        const FString Outcome = PendingOutcome;
        PendingOutcome.Empty();
        Finish(Outcome);
    }
}

bool USpectatorRoom::DecodeConfirmedInputs(const FRoomFrame &Frame,
                                           FString &MatchId,
                                           TArray<int32> &InputsA,
                                           TArray<int32> &InputsB,
                                           FString &Expected)
{
    if (Frame.State.IsEmpty() || HashConfirmedFrame(Frame) != Frame.Digest)
        return false;
    FMemoryReader Reader(Frame.State);
    Reader << MatchId << InputsA << InputsB << Expected;
    return !Reader.IsError() && Reader.Tell() == Reader.TotalSize() && InputsA.Num() == InputsB.Num() &&
           !MatchId.IsEmpty() && !Expected.IsEmpty() &&
           (InputsA.IsEmpty() ? Frame.Input1 == 0 && Frame.Input2 == 0
                              : InputsA.Last() == Frame.Input1 && InputsB.Last() == Frame.Input2);
}

bool USpectatorRoom::PlayFrame(ANightSkyGameState *InBattle, const FRoomFrame &Frame)
{
    return PlayFrameInternal(InBattle, Frame, false);
}

bool USpectatorRoom::PlayFrameInternal(ANightSkyGameState *InBattle, const FRoomFrame &Frame, bool Quiet, FString *PresentationError)
{
    if (PresentationError) PresentationError->Empty();
    FString MatchId, Expected;
    TArray<int32> InputsA, InputsB;
    if (!InBattle || !DecodeConfirmedInputs(Frame, MatchId, InputsA, InputsB, Expected))
        return false;

    struct FLocalPlayback
    {
        FRollbackData Initial;
        FTransform InitialCameraTransform, InitialSequenceCameraTransform;
        float InitialCameraFOV = 0.f;
        bool HasInitialCamera = false, HasInitialSequenceCamera = false;
        FString Match, LastDigest;
        TArray<int32> KnownInputsA, KnownInputsB;
        int32 Cursor = 0;
        bool Failed = false;
    };

    static TMap<TWeakObjectPtr<ANightSkyGameState>, FLocalPlayback> Local;
    for (auto It = Local.CreateIterator(); It; ++It)
        if (!It.Key().IsValid())
            It.RemoveCurrent();
    auto *Found = Local.Find(InBattle);
    if (!Found)
    {
        FLocalPlayback Playback;
        int32 Checksum = 0;
        InBattle->SaveGameState(Playback.Initial, &Checksum);
        if (IsValid(InBattle->CameraActor))
        {
            Playback.InitialCameraTransform = InBattle->CameraActor->GetActorTransform();
            Playback.InitialCameraFOV = InBattle->CameraActor->GetCameraComponent()->FieldOfView;
            Playback.HasInitialCamera = true;
        }
        if (IsValid(InBattle->SequenceCameraActor))
        {
            Playback.InitialSequenceCameraTransform = InBattle->SequenceCameraActor->GetActorTransform();
            Playback.HasInitialSequenceCamera = true;
        }
        Playback.Match = MatchId;
        if (InBattle->ParticleManager && !InBattle->ParticleManager->CaptureReplayStart(InBattle))
        {
            if (PresentationError) *PresentationError = TEXT("content:startup effects capture");
            return false;
        }
        Local.Add(InBattle, MoveTemp(Playback));
        Found = Local.Find(InBattle);
    }
    if (Found->Failed || Found->Match != MatchId)
        return false;
    for (int32 I = 0; I < FMath::Min(Found->KnownInputsA.Num(), InputsA.Num()); ++I)
        if (Found->KnownInputsA[I] != InputsA[I] || Found->KnownInputsB[I] != InputsB[I])
        {
            Found->Failed = true;
            return false;
        }
    if (Found->Cursor == InputsA.Num() && Found->LastDigest == Frame.Digest)
    {
        Found->Failed = DescribeBattleState(InBattle) != Expected;
        return !Found->Failed;
    }
    FRollbackData Previous;
    int32 PreviousChecksum = 0;
    InBattle->SaveGameState(Previous, &PreviousChecksum);
    const bool Sequential = !Quiet && InputsA.Num() == Found->Cursor + 1;
    const bool RestoreStart = !Sequential && !Found->LastDigest.IsEmpty();
    if (RestoreStart && InBattle->ParticleManager && !InBattle->ParticleManager->PrepareReplayStart())
    {
        if (PresentationError) *PresentationError = TEXT("content:startup effects restore");
        return false;
    }
    if (!Sequential)
    {
        StopBattleAudio(InBattle);
        if (RestoreStart)
        {
            if (InBattle->ParticleManager)
                InBattle->ParticleManager->ClearReplayParticles(InBattle);
            InBattle->LoadGameState(Found->Initial);
            // The rollback snapshot restores gameplay, not native camera actors.
            // Restore initialized presentation before catch-up; a zero destination
            // has no simulation step to refresh the camera before its capture.
            if (Found->HasInitialCamera && IsValid(InBattle->CameraActor))
            {
                InBattle->CameraActor->SetActorTransform(Found->InitialCameraTransform);
                InBattle->CameraActor->GetCameraComponent()->SetFieldOfView(Found->InitialCameraFOV);
            }
            if (Found->HasInitialSequenceCamera && IsValid(InBattle->SequenceCameraActor))
                InBattle->SequenceCameraActor->SetActorTransform(Found->InitialSequenceCameraTransform);
            if (InBattle->ParticleManager)
                InBattle->ParticleManager->InstallReplayStart();
        }
        // A first presentation already owns the normal initialized effect set.
        // Later seeks reconstruct that same startup before replaying any inputs.
        Found->Cursor = 0;
    }
    for (int32 I = Found->Cursor; I < InputsA.Num(); ++I)
    {
        InBattle->UpdateGameState(InputsA[I], InputsB[I], !Sequential);
        if (!Sequential)
            for (int32 Index = 0; Index < InBattle->BattleState.ActiveObjectCount; ++Index)
                InBattle->SortedObjects[Index]->UpdatePresentationTransforms();
    }
    Found->Cursor = InputsA.Num();
    Found->LastDigest = Frame.Digest;
    const FString Actual = DescribeBattleState(InBattle);
    Found->Failed = Actual != Expected;
    if (Found->Failed)
        InBattle->LoadGameState(Previous);
    else if (InputsA.Num() > Found->KnownInputsA.Num())
    {
        Found->KnownInputsA = InputsA;
        Found->KnownInputsB = InputsB;
    }
    if (!Sequential)
    {
        InBattle->FinishResimulation();
        if (InBattle->ParticleManager) InBattle->ParticleManager->PauseParticles();
        // Catch-up suppresses rendering work on skipped frames. Publish the final
        // native transforms and animation once, without advancing gameplay again.
        for (int32 Index = 0; Index < InBattle->BattleState.ActiveObjectCount; ++Index)
            InBattle->SortedObjects[Index]->UpdateVisualsNoRollback();
    }
    return !Found->Failed;
}

struct FRoomPrediction : public ConnectionManager
{
    TWeakObjectPtr<URoomConnection> Owner;
    TWeakObjectPtr<ANightSkyGameState> Battle;
    FString Match, Assignment;
    GGPOSession *Session = nullptr;
    GGPOPlayerHandle LocalHandle = 0;
    int32 Seat = 0, Rollbacks = 0;
    int32 RenderedFrame = INDEX_NONE, RenderedRollbacks = INDEX_NONE;
    TWeakObjectPtr<ANightSkyGameState> RenderedBattle;
    TWeakObjectPtr<ASceneCapture2D> RenderedCapture;
    FString RenderedMatch;
    bool Running = false, Failed = false, Paused = false;
    TArray<TArray<uint8>> Incoming;
    TMap<int32, int32> Inputs;
    TMap<int32, FString> States, Expected;

    ~FRoomPrediction()
    {
        if (Session)
            GGPONet::ggpo_close_session(Session);
    }

    int SendTo(const char *Buffer, int Len, int, int) override
    {
        if (Owner.IsValid() && Len > 0 && Len <= 4096)
            Owner->SendPredictionPacket(
                Match, Assignment, TArray<uint8>(reinterpret_cast<const uint8 *>(Buffer), Len));
        return Len;
    }

    int RecvFrom(char *Buffer, int Len, int, int *Connection) override
    {
        if (Incoming.IsEmpty())
            return -1;
        auto Packet = MoveTemp(Incoming[0]);
        Incoming.RemoveAt(0);
        if (Packet.Num() > Len)
            return -1;
        FMemory::Memcpy(Buffer, Packet.GetData(), Packet.Num());
        *Connection = 1 - Seat;
        return Packet.Num();
    }

    bool Advance(bool Resimulate)
    {
        int32 Pair[2] = {0, 0}, Flags = 0;
        if (GGPONet::ggpo_synchronize_input(Session, Pair, sizeof(Pair), &Flags) != GGPO_OK)
            return false;
        Battle->UpdateGameState(Pair[0], Pair[1], Resimulate);
        States.Add(Battle->BattleState.FrameNumber, DescribeBattleState(Battle.Get()));
        return GGPONet::ggpo_advance_frame(Session) == GGPO_OK;
    }

    bool Initialize(URoomConnection *InOwner, ANightSkyGameState *InBattle, const FRoomDelivery &Delivery)
    {
        Owner = InOwner;
        Battle = InBattle;
        Match = Delivery.Match;
        Assignment = Delivery.Assignment;
        Seat = Delivery.FighterSeat;
        GGPOSessionCallbacks C = {};
        C.begin_game = [](const char *)
        {
            return true;
        };
        C.save_game_state = [this](unsigned char **Buffer, int *Len, int *Checksum, int)
        {
            FRollbackData State;
            Battle->SaveGameState(State, Checksum);
            FBufferArchive Bytes;
            State.Serialize(Bytes);
            *Len = Bytes.Num();
            *Buffer = new unsigned char[*Len];
            FMemory::Memcpy(*Buffer, Bytes.GetData(), *Len);
            return true;
        };
        C.load_game_state = [this](unsigned char *Buffer, int Len)
        {
            TArray<uint8> Bytes(Buffer, Len);
            FMemoryReader Reader(Bytes);
            FRollbackData State;
            State.Serialize(Reader);
            if (Reader.IsError())
                return false;
            StopBattleAudio(Battle.Get());
            Battle->LoadGameState(State);
            ++Rollbacks;
            return true;
        };
        C.free_buffer = [](void *Buffer)
        {
            delete[] static_cast<unsigned char *>(Buffer);
        };
        C.log_game_state = [](const char *, unsigned char *, int)
        {
            return true;
        };
        C.advance_frame = [this](int)
        {
            return Advance(true);
        };
        C.on_event = [this](GGPOEvent *Event)
        {
            if (Event->code == GGPO_EVENTCODE_RUNNING)
                Running = true;
            if (Event->code == GGPO_EVENTCODE_DISCONNECTED_FROM_PEER)
            {
                Running = false;
                Failed = true;
            }
            return true;
        };
        if (GGPONet::ggpo_start_session(&Session, &C, this, "room", 2, sizeof(int32)) != GGPO_OK)
            return false;
        for (int32 I = 0; I < 2; ++I)
        {
            GGPOPlayer Player = {};
            Player.type = I == Seat ? GGPO_PLAYERTYPE_LOCAL : GGPO_PLAYERTYPE_REMOTE;
            Player.player_num = I + 1;
            Player.connection_id = I;
            GGPOPlayerHandle Handle = 0;
            if (GGPONet::ggpo_add_player(Session, &Player, &Handle) != GGPO_OK)
                return false;
            if (I == Seat)
            {
                LocalHandle = Handle;
                GGPONet::ggpo_set_frame_delay(Session, Handle, 0);
            }
        }
        GGPONet::ggpo_set_disconnect_timeout(Session, 45000);
        GGPONet::ggpo_try_synchronize_local(Session);
        States.Add(0, DescribeBattleState(Battle.Get()));
        return true;
    }

    FString Tick()
    {
        if (!Session || !Battle.IsValid())
            return TEXT("prediction unavailable");
        GGPONet::ggpo_idle(Session, 0);
        Battle->FinishResimulation();
        if (Failed)
            return TEXT("prediction disconnected");
        if (Running && !Paused)
        {
            const int32 Next = Battle->BattleState.FrameNumber + 1;
            if (auto *Input = Inputs.Find(Next))
            {
                int32 Bits = *Input;
                if (GGPONet::ggpo_add_local_input(Session, LocalHandle, &Bits, sizeof(Bits)) == GGPO_OK)
                {
                    if (!Advance(false))
                        return TEXT("prediction waiting");
                    Inputs.Remove(Next);
                }
            }
        }
        const int32 Confirmed = GGPONet::ggpo_get_last_confirmed_frame(Session) + 1;
        for (auto It = Expected.CreateIterator(); It; ++It)
            if (It.Key() <= Confirmed)
            {
                const FString *Actual = States.Find(It.Key());
                if (Actual && *Actual != It.Value())
                {
                    Failed = true;
                    return TEXT("integrity error");
                }
                if (Actual)
                    It.RemoveCurrent();
            }
        return Running ? TEXT("ok") : TEXT("prediction synchronizing");
    }
};

bool USpectatorRoom::BeginPrediction(URoomConnection *Connection, const FRoomDelivery &Delivery)
{
    if (Delivery.FighterSeat < 0 || Delivery.FighterSeat > 1 || Delivery.Gameplay.State.IsEmpty())
        return false;
    if (!Prediction || Prediction->Match != Delivery.Match || Prediction->Assignment != Delivery.Assignment)
    {
        // A new seat starts from the complete initialized frame, never a borrowed
        // rollback buffer. Fighter input cannot begin before the initial delivery.
        auto *Setup = DecodeRoomStore(Delivery.Setup);
        if (!Setup || Setup->Matches.Num() != 1 || Setup->Matches[0].Frames.Num() != 1)
            return false;
        FRoomDelivery Initial = Delivery;
        Initial.Frame = 0;
        Initial.Gameplay = Setup->Matches[0].Frames[0];
        FString Semantic;
        FMemoryReader Saved(Initial.Gameplay.State);
        Saved << Semantic;
        FString Id = Delivery.Match;
        TArray<int32> Empty;
        FBufferArchive Journal;
        Journal << Id << Empty << Empty << Semantic;
        Initial.Gameplay.State = Journal;
        Initial.Gameplay.Digest = HashConfirmedFrame(Initial.Gameplay);
        if (PresentDelivery(Initial) != TEXT("ok"))
            return false;
        Prediction = MakeShared<FRoomPrediction>();
        if (!Prediction->Initialize(Connection, PlaybackBattle.Get(), Initial))
        {
            Prediction.Reset();
            return false;
        }
    }
    FString Id, Semantic;
    TArray<int32> A, B;
    FMemoryReader Reader(Delivery.Gameplay.State);
    Reader << Id << A << B << Semantic;
    if (Reader.IsError() || Id != Delivery.Match)
        return false;
    Prediction->Owner = Connection;
    const auto &Local = Delivery.FighterSeat == 0 ? A : B;
    for (int32 I = 0; I < Local.Num(); ++I)
        if (I + 1 > PlaybackBattle->BattleState.FrameNumber)
            Prediction->Inputs.FindOrAdd(I + 1) = Local[I];
    for (const auto &Pending : PendingPredictionInputs)
        if (Pending.Key > PlaybackBattle->BattleState.FrameNumber)
            Prediction->Inputs.FindOrAdd(Pending.Key) = Pending.Value;
    PendingPredictionInputs.Empty();
    Prediction->Expected.Add(Delivery.Frame, Semantic);
    return true;
}

void USpectatorRoom::SubmitPrediction(const FString &Match, int32 Frame, int32 Input)
{
    if (PendingPredictionMatch != Match)
    {
        PendingPredictionInputs.Empty();
        PendingPredictionMatch = Match;
    }
    if (Prediction && Prediction->Match != Match)
        Prediction.Reset();
    if (!Prediction)
    {
        PendingPredictionInputs.FindOrAdd(Frame) = Input;
        return;
    }
    if (Prediction && PlaybackBattle.IsValid() && Frame > PlaybackBattle->BattleState.FrameNumber)
        Prediction->Inputs.FindOrAdd(Frame) = Input & ~(INP_Rematch | INP_ResetTraining);
}

void USpectatorRoom::ReceivePrediction(const TArray<uint8> &Bytes)
{
    if (Prediction && Bytes.Num() > 0 && Bytes.Num() <= 4096 && Prediction->Incoming.Num() < 256)
        Prediction->Incoming.Add(Bytes);
}

FString USpectatorRoom::TickPrediction(bool CombatPaused)
{
    if (!Prediction)
        return TEXT("");
    Prediction->Paused = CombatPaused;
    const FString Status = Prediction->Tick();
    if (PlaybackCapture.IsValid() && PlaybackBattle.IsValid() && PlaybackBattle->CameraActor)
    {
        const int32 Frame = PlaybackBattle->BattleState.FrameNumber;
        // Waiting for a peer does not change the rendered simulation. Keep polling
        // transport without submitting the same scene again on every engine tick.
        if (Prediction->RenderedFrame != Frame || Prediction->RenderedRollbacks != Prediction->Rollbacks ||
            Prediction->RenderedBattle != PlaybackBattle || Prediction->RenderedCapture != PlaybackCapture ||
            Prediction->RenderedMatch != PlaybackMatch)
        {
            PlaybackCapture->SetActorTransform(PlaybackBattle->CameraActor->GetActorTransform());
            PlaybackCapture->GetCaptureComponent2D()->CaptureScene();
            Prediction->RenderedFrame = Frame;
            Prediction->RenderedRollbacks = Prediction->Rollbacks;
            Prediction->RenderedBattle = PlaybackBattle;
            Prediction->RenderedCapture = PlaybackCapture;
            Prediction->RenderedMatch = PlaybackMatch;
        }
    }
    return Status;
}

int32 USpectatorRoom::PredictionFrame() const
{
    return Prediction && PlaybackBattle.IsValid() ? PlaybackBattle->BattleState.FrameNumber : -1;
}

int32 USpectatorRoom::PredictionRollbacks() const
{
    return Prediction ? Prediction->Rollbacks : 0;
}

int32 USpectatorRoom::PredictionConfirmedFrame() const
{
    return Prediction && Prediction->Session ? GGPONet::ggpo_get_last_confirmed_frame(Prediction->Session) + 1
                                             : -1;
}

void URoomPlaybackGameInstance::Init()
{
    UGameInstance::Init();
}

ANightSkyGameState *USpectatorRoom::PresentationBattle() const
{
    return PlaybackBattle.Get();
}

UTextureRenderTarget2D *USpectatorRoom::PresentationTexture() const
{
    return PlaybackTexture;
}

void USpectatorRoom::ClosePresentation()
{
    Prediction.Reset();
    PlaybackBattle.Reset();
    PlaybackCapture.Reset();
    PlaybackTexture = nullptr;
    PlaybackMatch.Empty();
    PlaybackRoom.Empty();
    if (!PlaybackGame)
        return;
    PlaybackGame->IsReplay = true; // Suppress unrelated ordinary replay recording during teardown.
    auto *World = PlaybackGame->GetWorld();
    if (World)
    {
        World->EndPlay(EEndPlayReason::Quit);
        World->DestroyWorld(false);
    }
    PlaybackGame->Shutdown();
    if (World)
        GEngine->DestroyWorldContext(World);
    PlaybackGame = nullptr;
}

void USpectatorRoom::Deinitialize()
{
    ClosePresentation();
    Super::Deinitialize();
}

FString USpectatorRoom::PresentDelivery(const FRoomDelivery &Delivery)
{
    if (Delivery.Frame < 0 || Delivery.Gameplay.State.IsEmpty())
        return Delivery.Status;
    FString DecodedMatch, Expected;
    TArray<int32> InputsA, InputsB;
    if (!DecodeConfirmedInputs(Delivery.Gameplay, DecodedMatch, InputsA, InputsB, Expected) ||
        DecodedMatch != Delivery.Match || InputsA.Num() != Delivery.Frame)
        return TEXT("integrity error");
    Prediction.Reset();
    auto *SetupStore = DecodeRoomStore(Delivery.Setup);
    if (!SetupStore || SetupStore->Matches.Num() != 1 || SetupStore->Matches[0].Id != Delivery.Match)
        return TEXT("integrity error");
    const FRoomMatch Setup = SetupStore->Matches[0];
    if (!Setup.Configuration.Stage || !Setup.BattleClass)
        return TEXT("content:playback configuration");
    if (Setup.Frames.Num() != 1 || HashConfirmedFrame(Setup.Frames[0]) != Setup.Frames[0].Digest)
        return TEXT("integrity error");
    auto &Verified = VerifiedTimelines.FindOrAdd(Delivery.Room).FindOrAdd(Delivery.Match);
    if (Verified.Failed)
        return TEXT("integrity error");
    if (!Verified.InitialDigest.IsEmpty() && Verified.InitialDigest != Setup.Frames[0].Digest)
        Verified.Failed = true;
    for (int32 I = 0; I < FMath::Min(Verified.InputsA.Num(), InputsA.Num()); ++I)
        if (Verified.InputsA[I] != InputsA[I] || Verified.InputsB[I] != InputsB[I])
            Verified.Failed = true;
    if (Verified.Failed)
        return TEXT("integrity error");
    if (PlaybackRoom != Delivery.Room || PlaybackMatch != Delivery.Match || !PlaybackBattle.IsValid())
    {
        auto *Source = Cast<UNightSkyGameInstance>(GetGameInstance());
        if (!Source)
            return TEXT("battle game instance required");
        const auto Local = InspectContent(Setup.Configuration, Source->BattleVersion, Setup.AnnouncerData, Setup.MusicData);
        for (const auto &Required : Setup.Content)
            if (Required.Value.IsEmpty() || Local.FindRef(Required.Key) != Required.Value)
                return TEXT("content:") + Required.Key;
        ClosePresentation();
        PlaybackGame = NewObject<URoomPlaybackGameInstance>(GEngine);
        PlaybackGame->InitializeStandalone();
        PlaybackGame->BattleData = Setup.Configuration;
        if (!RestoreMatchStartup(PlaybackGame, Setup))
        {
            ClosePresentation();
            return TEXT("content:startup settings");
        }
        PlaybackGame->IsTraining = Setup.Training;
        PlaybackGame->IsCPUBattle = false;
        PlaybackGame->FighterRunner = LocalPlay;
        PlaybackGame->BattleVersion = Source->BattleVersion;
        PlaybackGame->GetSubsystem<USpectatorRoom>()->EnableClient();
        FString Error;
        const bool Loaded = LoadPrivateRoomWorld(PlaybackGame, Setup.Configuration.Stage->StageURL, Error);
        if (!Loaded)
        {
            ClosePresentation();
            return TEXT("content:stage ") + Error;
        }
        auto *World = PlaybackGame->GetWorld();
        // This world has no net driver or room owner. Its stage and actors belong only
        // to the selected retained match, so loading it cannot travel the live connection.
        if (TActorIterator<ANightSkyGameState> It(World); It)
        {
            PlaybackBattle = *It;
        }
        if (!PlaybackBattle.IsValid() || PlaybackBattle->GetClass() != Setup.BattleClass || PlaybackBattle->Players.Num() < 2)
        {
            ClosePresentation();
            return TEXT("content:battle class");
        }
        FString InitialExpected;
        FMemoryReader InitialReader(Setup.Frames[0].State);
        InitialReader << InitialExpected;
        if (InitialReader.IsError() || InitialReader.Tell() != InitialReader.TotalSize() ||
            DescribeBattleState(PlaybackBattle.Get()) != InitialExpected)
        {
            Verified.Failed = true;
            ClosePresentation();
            return TEXT("integrity error");
        }
        PlaybackBattle->SetActorTickEnabled(false);
        PlaybackMatch = Delivery.Match;
        PlaybackRoom = Delivery.Room;
        PlaybackTexture = NewObject<UTextureRenderTarget2D>(this);
        PlaybackTexture->InitAutoFormat(960, 540);
        PlaybackCapture = World->SpawnActor<ASceneCapture2D>();
        auto *Capture = PlaybackCapture->GetCaptureComponent2D();
        Capture->TextureTarget = PlaybackTexture;
        Capture->CaptureSource = SCS_FinalColorLDR;
        Capture->bCaptureEveryFrame = false;
        Capture->bCaptureOnMovement = false;
    }
    FString PresentationError;
    if (!PlayFrameInternal(PlaybackBattle.Get(), Delivery.Gameplay, Delivery.Mode == TEXT("paused"), &PresentationError))
    {
        if (!PresentationError.IsEmpty()) return PresentationError;
        Verified.Failed = true;
        return TEXT("integrity error");
    }
    Verified.InitialDigest = Setup.Frames[0].Digest;
    if (InputsA.Num() > Verified.InputsA.Num())
    {
        Verified.InputsA = InputsA;
        Verified.InputsB = InputsB;
    }
    if (PlaybackCapture.IsValid() && PlaybackBattle->CameraActor)
    {
        auto *Camera = PlaybackBattle->CameraActor;
        PlaybackCapture->SetActorTransform(Camera->GetActorTransform());
        auto *Capture = PlaybackCapture->GetCaptureComponent2D();
        Capture->FOVAngle = Camera->GetCameraComponent()->FieldOfView;
        Capture->CaptureScene();
    }
    return TEXT("ok");
}

bool USpectatorRoom::ReadReplay(const TArray<uint8> &Bytes, FRoomMatch &Match)
{
    FRoomMatch Decoded;
    if (!ReadCompactReplay(Bytes, Decoded))
        return false;
    TArray<int32> InputsA, InputsB;
    for (int32 Index = 0; Index < Decoded.Frames.Num(); ++Index)
    {
        auto &Frame = Decoded.Frames[Index];
        FString Semantic;
        FMemoryReader Reader(Frame.State);
        Reader << Semantic;
        if (Index > 0)
        {
            InputsA.Add(Frame.Input1);
            InputsB.Add(Frame.Input2);
        }
        // The journal stays compact on disk. Public frames carry the complete
        // confirmed prefix, just like Observe, so the public decoder and playback
        // accept every inspected replay frame, including initialized frame zero.
        FBufferArchive PublicState;
        PublicState << Decoded.Id << InputsA << InputsB << Semantic;
        Frame.State = PublicState;
        Frame.Digest = HashConfirmedFrame(Frame);
    }
    Match = MoveTemp(Decoded);
    return true;
}

URoomConnection::URoomConnection()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true;
}

void URoomConnection::Authenticate(const FString &Identity,
                                   const FString &Secret,
                                   const TArray<FString> &Content)
{
    PendingPredictionPackets.Empty();
    auto *Room = GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>();
    Room->LoginIdentity = Identity;
    Room->LoginSecret = Secret;
    Room->LoginContent = Content;
    ServerAuthenticate(Identity, Secret, Content);
}

void URoomConnection::BeginPlay()
{
    Super::BeginPlay();
    auto *Controller = Cast<APlayerController>(GetOwner());
    auto *Room = GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>();
    if (Controller && Controller->IsLocalController())
    {
        if (!Room->LoginIdentity.IsEmpty())
            ServerAuthenticate(Room->LoginIdentity, Room->LoginSecret, Room->LoginContent);
        if (Controller->GetLocalPlayer() && !Cast<URoomPlaybackGameInstance>(GetWorld()->GetGameInstance()))
        {
            Panel = CreateWidget<URoomPanel>(Controller, URoomPanel::StaticClass());
            Panel->Connection = this;
            Panel->AddToViewport(50);
        }
    }
}

void URoomConnection::ServerAuthenticate_Implementation(const FString &Identity,
                                                        const FString &Secret,
                                                        const TArray<FString> &Content)
{
    if (!AuthenticatedIdentity.IsEmpty() && AuthenticatedIdentity != Identity)
    {
        FRoomDelivery Delivery;
        Delivery.Status = TEXT("leave before changing identity");
        SendDelivery(Delivery);
        return;
    }
    auto *Room = GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>();
    if (Content.Num() % 2 != 0)
    {
        FRoomDelivery Delivery;
        Delivery.Status = TEXT("invalid content manifest");
        SendDelivery(Delivery);
        return;
    }
    TMap<FString, FString> Revisions;
    for (int32 I = 0; I + 1 < Content.Num(); I += 2)
        Revisions.Add(Content[I], Content[I + 1]);
    // One live connection owns an identity. Reauthentication after its connection closes is allowed.
    for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
        if (auto *Other = It->FindComponentByClass<URoomConnection>();
            Other && Other != this && Other->AuthenticatedIdentity == Identity)
        {
            FRoomDelivery Delivery;
            Delivery.Status = TEXT("identity already connected");
            SendDelivery(Delivery);
            return;
        }
    if (Room->Authenticate(Identity, Secret, Revisions))
    {
        PendingPredictionPackets.Empty();
        AuthenticatedIdentity = Identity;
        SendDelivery(Room->Observe(Identity));
    }
    else
    {
        FRoomDelivery Delivery;
        Delivery.Status = TEXT("authentication failed");
        SendDelivery(Delivery);
    }
}

void URoomConnection::Submit(const FRoomCommand &C)
{
    FRoomCommand Request = C;
    if (Request.Membership.IsEmpty())
        Request.Membership = LastDelivery.Membership;
    if (C.Operation == TEXT("input") && !Request.Nonce.IsEmpty())
        PendingPredictionCommands.Add(Request.Nonce, Request);
    ServerSubmit(Request);
}

void URoomConnection::ServerSubmit_Implementation(const FRoomCommand &C)
{
    if (AuthenticatedIdentity.IsEmpty())
    {
        FRoomDelivery Delivery;
        Delivery.Status = TEXT("unauthenticated");
        Delivery.RequestNonce = C.Nonce;
        SendDelivery(Delivery);
        return;
    }
    FRoomDelivery Result =
        GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>()->Command(AuthenticatedIdentity, C);
    // Input receipts acknowledge ownership; ordinary frame delivery carries gameplay once.
    if (C.Operation == TEXT("input"))
        Result.Gameplay = FRoomFrame();
    SendDelivery(Result);
    if (C.Operation == TEXT("leave") && Result.Status == TEXT("accepted"))
        AuthenticatedIdentity.Empty();
}

FRoomAuthorityDeliveryEvent URoomConnection::AuthorityDelivery;

void URoomConnection::SendDelivery(const FRoomDelivery &Delivery)
{
    AuthorityDelivery.Broadcast(Delivery);
    FBufferArchive Bytes;
    FRoomDelivery Copy = Delivery;
    FRoomDelivery::StaticStruct()->SerializeItem(Bytes, &Copy, nullptr);
    constexpr int32 PartSize = 16384;
    constexpr int32 MaxDecodedSize = 65536 * PartSize;
    FBufferArchive Envelope;
    const TArray<uint8> *Wire = &Bytes;
    FString Encoding;
    if (!Bytes.IsEmpty() && Bytes.Num() <= MaxDecodedSize)
    {
        int64 Bound = 0;
        const bool HasBound = FCompression::CompressMemoryBound(NAME_Zlib, Bound, int64(Bytes.Num()));
        if (HasBound && Bound > 0 && Bound <= MaxDecodedSize)
        {
            TArray<uint8> Compressed;
            Compressed.SetNumUninitialized(static_cast<int32>(Bound));
            int64 CompressedSize = Bound;
            if (FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedSize,
                                             Bytes.GetData(), Bytes.Num()))
            {
                if (CompressedSize > 0 && CompressedSize <= Compressed.Num() && CompressedSize < Bytes.Num() - 28)
                {
                    uint32 Version = 1;
                    int32 DecodedSize = Bytes.Num();
                    uint8 DecodedDigest[20];
                    FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), DecodedDigest);
                    Envelope << Version << DecodedSize;
                    Envelope.Serialize(DecodedDigest, sizeof(DecodedDigest));
                    Envelope.Serialize(Compressed.GetData(), CompressedSize);
                    Wire = &Envelope;
                    Encoding = TEXT("NSEZ1-");
                }
            }
            else
                UE_LOG(LogTemp, Warning, TEXT("Room delivery compression failed; sending complete uncompressed delivery"));
        }
        else if (!HasBound)
            UE_LOG(LogTemp, Warning, TEXT("Room delivery compression bound unavailable; sending complete uncompressed delivery"));
    }
    const FString Transfer = Encoding + CreateRoomIdentity() + TEXT(":") + HashBytes(*Wire);
    const int32 Count = FMath::DivideAndRoundUp(Wire->Num(), PartSize);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const int32 Offset = Index * PartSize, Length = FMath::Min(PartSize, Wire->Num() - Offset);
        DeliverPart(Transfer, Index, Count, TArray<uint8>(Wire->GetData() + Offset, Length));
    }
}

void URoomConnection::DeliverPart_Implementation(const FString &Transfer,
                                                 int32 Index,
                                                 int32 Count,
                                                 const TArray<uint8> &Bytes)
{
    ++ReceivedChunks;
    auto Invalid = [&]()
    {
        IntegrityFailed = true;
        LastDelivery.Status = TEXT("integrity error");
        OnDelivery.Broadcast(LastDelivery);
    };
    if (Count <= 0 || Count > 65536 || Index < 0 || Index >= Count || Bytes.IsEmpty() || Bytes.Num() > 16384)
    {
        Invalid();
        return;
    }
    if (const auto *Hashes = CompletedTransfers.Find(Transfer))
    {
        if (Hashes->Num() != Count || (*Hashes)[Index] != HashBytes(Bytes))
            Invalid();
        return;
    }
    auto &Assembly = Transfers.FindOrAdd(Transfer);
    if (Assembly.Parts.IsEmpty())
        Assembly.Parts.SetNum(Count);
    if (Assembly.Parts.Num() != Count)
    {
        Invalid();
        return;
    }
    if (Assembly.Received.Contains(Index))
    {
        if (Assembly.Parts[Index] != Bytes)
            Invalid();
        return;
    }
    Assembly.Parts[Index] = Bytes;
    Assembly.Received.Add(Index);
    if (Assembly.Received.Num() != Count)
        return;
    TArray<uint8> Complete;
    for (const auto &Part : Assembly.Parts)
        Complete.Append(Part);
    FString Identity, Digest;
    if (!Transfer.Split(TEXT(":"), &Identity, &Digest) || HashBytes(Complete) != Digest)
    {
        Invalid();
        return;
    }
    TArray<uint8> Decoded;
    const TArray<uint8> *Serialized = &Complete;
    if (Identity.StartsWith(TEXT("NSEZ")))
    {
        if (!Identity.StartsWith(TEXT("NSEZ1-")) || Complete.Num() <= 28)
        {
            Invalid();
            return;
        }
        FMemoryReader Envelope(Complete);
        uint32 Version = 0;
        int32 DecodedSize = 0;
        uint8 DecodedDigest[20];
        Envelope << Version << DecodedSize;
        Envelope.Serialize(DecodedDigest, sizeof(DecodedDigest));
        constexpr int32 MaxDecodedSize = 65536 * 16384;
        if (Envelope.IsError() || Version != 1 || DecodedSize <= 0 || DecodedSize > MaxDecodedSize)
        {
            Invalid();
            return;
        }
        Decoded.SetNumZeroed(DecodedSize);
        if (!FCompression::UncompressMemory(NAME_Zlib, Decoded.GetData(), DecodedSize,
                                             Complete.GetData() + Envelope.Tell(),
                                             Complete.Num() - Envelope.Tell()) ||
            HashBytes(Decoded) != BytesToHex(DecodedDigest, sizeof(DecodedDigest)))
        {
            Invalid();
            return;
        }
        Serialized = &Decoded;
    }
    FRoomDelivery Delivery;
    FMemoryReader Reader(*Serialized);
    FRoomDelivery::StaticStruct()->SerializeItem(Reader, &Delivery, nullptr);
    if (Reader.IsError() || Reader.Tell() != Reader.TotalSize())
    {
        Invalid();
        return;
    }
    CompletedTransfer = Transfer;
    CompletedPartHashes.Empty();
    for (const auto &Part : Assembly.Parts)
        CompletedPartHashes.Add(HashBytes(Part));
    CompletedTransfers.Add(Transfer, CompletedPartHashes);
    Transfers.Remove(Transfer);
    ++ReassembledDeliveries;
    IncomingParts.Empty();
    ReceivedParts.Empty();
    IncomingTransfer.Empty();
    Deliver_Implementation(Delivery);
}

void URoomConnection::Deliver_Implementation(const FRoomDelivery &Delivery)
{
    OnTransportDelivery.Broadcast(Delivery);
    if (const auto *Pending = PendingPredictionCommands.Find(Delivery.RequestNonce))
    {
        if (Delivery.Status == TEXT("accepted") && Delivery.FighterSeat >= 0 &&
            Pending->Match == Delivery.Match && Pending->Assignment == Delivery.Assignment)
        {
            int32 Bits = 0;
            if (LexTryParseString(Bits, *Pending->Value))
                GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>()->SubmitPrediction(
                    Pending->Match, Pending->Number, Bits);
        }
        PendingPredictionCommands.Remove(Delivery.RequestNonce);
    }
    if (IntegrityFailed)
    {
        LastDelivery.Status = TEXT("integrity error");
        OnDelivery.Broadcast(LastDelivery);
        return;
    }
    const FRoomDelivery Previous = LastDelivery;
    LastDelivery = Delivery;
    if (Delivery.Frame >= 0 && Delivery.Gameplay.State.IsEmpty() && Delivery.Match == Previous.Match)
    {
        LastDelivery.Frame = Previous.Frame;
        LastDelivery.Gameplay = Previous.Gameplay;
    }
    if (!Delivery.Room.IsEmpty() && !GetOwner()->HasAuthority())
        GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>()->EnableClient();
    // Transport audit precedes validation. The presentation delegate reports the validated local state.
    if (Delivery.Frame >= 0 && !Delivery.Gameplay.State.IsEmpty() &&
        (!GetOwner()->HasAuthority() ||
         (Cast<APlayerController>(GetOwner()) && Cast<APlayerController>(GetOwner())->GetLocalPlayer())))
    {
        auto *Room = GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>();
        const FString Status = Delivery.FighterSeat >= 0 && Room->BeginPrediction(this, Delivery)
                                   ? TEXT("ok")
                                   : Room->PresentDelivery(Delivery);
        if (Status != TEXT("ok"))
        {
            LastDelivery.Status = Status;
            IntegrityFailed = Status == TEXT("integrity error");
        }
    }
    OnDelivery.Broadcast(LastDelivery);
    if (Delivery.Status.StartsWith(TEXT("content:")) && !Delivery.RequiredContent.IsEmpty())
    {
        auto *Room = GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>();
        auto *GameInstance = Cast<UNightSkyGameInstance>(GetWorld()->GetGameInstance());
        FBufferArchive Bytes;
        auto Required = Delivery.RequiredContent;
        Bytes << Required;
        const FString Signature = HashBytes(Bytes);
        if (GameInstance && Signature != AttemptedContent && !Room->LoginIdentity.IsEmpty())
        {
            AttemptedContent = Signature;
            const auto Local =
                USpectatorRoom::InspectRequiredContent(Delivery.RequiredContent, GameInstance->BattleVersion);
            bool Ready = true;
            TArray<FString> Content;
            for (const auto &Pair : Local)
            {
                Ready &= !Pair.Value.IsEmpty() && Delivery.RequiredContent.FindRef(Pair.Key) == Pair.Value;
                Content.Add(Pair.Key);
                Content.Add(Pair.Value);
            }
            if (Ready)
                Authenticate(Room->LoginIdentity, Room->LoginSecret, Content);
        }
    }
    if (!Delivery.Replay.IsEmpty())
    {
        FRoomMatch Replay;
        if (ReadCompactReplay(Delivery.Replay, Replay))
            WriteRoomFileAtomically(FPaths::ProjectSavedDir() / TEXT("RoomReplays") /
                                        (Replay.Id + TEXT(".room")),
                                    Delivery.Replay);
    }
}

void URoomConnection::ServerPredictionPacket_Implementation(const FString &Match,
                                                            const FString &Assignment,
                                                            const TArray<uint8> &Bytes)
{
    if (Bytes.IsEmpty() || Bytes.Num() > 4096 || AuthenticatedIdentity.IsEmpty())
        return;
    auto *Room = GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>();
    const auto Sender = Room->Observe(AuthenticatedIdentity);
    if (Sender.Status != TEXT("ok") || Sender.FighterSeat < 0 || Sender.Match != Match || Sender.Assignment != Assignment)
        return;
    for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
        if (auto *Other = It->FindComponentByClass<URoomConnection>();
            Other && Other != this && !Other->AuthenticatedIdentity.IsEmpty())
        {
            const auto Peer = Room->Observe(Other->AuthenticatedIdentity);
            if (Peer.Status == TEXT("ok") && Peer.FighterSeat == 1 - Sender.FighterSeat && Peer.Match == Match)
                Other->RelayPredictionPacket(this, Sender, Peer, Bytes);
        }
}

void URoomConnection::SendPredictionPacket(const FString &Match, const FString &Assignment,
                                           const TArray<uint8> &Bytes)
{
    if (Bytes.IsEmpty() || Bytes.Num() > 4096 || LastDelivery.FighterSeat < 0 ||
        LastDelivery.FighterSeat > 1 || LastDelivery.Match != Match || LastDelivery.Assignment != Assignment ||
        LastDelivery.Room.IsEmpty() || LastDelivery.Membership.IsEmpty())
        return;
    UNetConnection *Connection = GetOwner()->GetNetConnection();
    const UNetConnection *RemoteConnection = Connection;
    if (GetOwner()->HasAuthority() ||
        (RemoteConnection && PendingPredictionPackets.IsEmpty() && RemoteConnection->IsNetReady()))
    {
        ServerPredictionPacket(Match, Assignment, Bytes);
        return;
    }
    if (!Connection)
        return;
    FPendingPredictionPacket Packet;
    Packet.ToServer = true;
    Packet.Connection = Connection;
    Packet.SenderMembership = LastDelivery.Membership;
    Packet.SenderAssignment = Assignment;
    Packet.SenderSeat = LastDelivery.FighterSeat;
    Packet.Room = LastDelivery.Room;
    Packet.Match = Match;
    Packet.Bytes = Bytes;
    Packet.ExpiresAt = FPlatformTime::Seconds() + 0.25;
    if (PendingPredictionPackets.Num() >= 32)
        PendingPredictionPackets.RemoveAt(0);
    PendingPredictionPackets.Add(MoveTemp(Packet));
}

void URoomConnection::RelayPredictionPacket(URoomConnection *Sender, const FRoomDelivery &SenderState,
                                             const FRoomDelivery &RecipientState, const TArray<uint8> &Bytes)
{
    UNetConnection *Connection = GetOwner()->GetNetConnection();
    const UNetConnection *RemoteConnection = Connection;
    // Keep local delivery and available remote sends immediate without bypassing FIFO.
    if (!RemoteConnection || (PendingPredictionPackets.IsEmpty() && RemoteConnection->IsNetReady()))
    {
        ClientPredictionPacket(SenderState.Match, Bytes);
        return;
    }
    FPendingPredictionPacket Packet;
    Packet.Connection = Connection;
    Packet.Sender = Sender;
    Packet.SenderIdentity = Sender->AuthenticatedIdentity;
    Packet.SenderMembership = SenderState.Membership;
    Packet.SenderAssignment = SenderState.Assignment;
    Packet.RecipientIdentity = AuthenticatedIdentity;
    Packet.RecipientMembership = RecipientState.Membership;
    Packet.RecipientAssignment = RecipientState.Assignment;
    Packet.Room = RecipientState.Room;
    Packet.Match = SenderState.Match;
    Packet.Bytes = Bytes;
    // Bound relay waiting, not GGPO's retry timer or the room's gameplay clock.
    Packet.ExpiresAt = FPlatformTime::Seconds() + 0.25;
    // These remain unreliable datagrams. Prefer fresh traffic on overflow;
    // GGPO owns protocol retransmission and ordering. Payload storage is <=128KiB.
    if (PendingPredictionPackets.Num() >= 32)
        PendingPredictionPackets.RemoveAt(0);
    PendingPredictionPackets.Add(MoveTemp(Packet));
}

void URoomConnection::DrainPredictionPackets()
{
    UWorld *World = GetWorld();
    if (!World || World->bIsTearingDown || !GetOwner())
    {
        PendingPredictionPackets.Empty();
        return;
    }
    const bool Authority = GetOwner()->HasAuthority();
    auto *Room = Authority ? World->GetGameInstance()->GetSubsystem<USpectatorRoom>() : nullptr;
    // Each tick inspects at most the fixed capacity. Never force readiness or
    // send a deferred packet through a replaced/disconnected transport owner.
    for (int32 Inspected = 0; Inspected < 32 && !PendingPredictionPackets.IsEmpty(); ++Inspected)
    {
        const auto &Front = PendingPredictionPackets[0];
        const UNetConnection *RemoteConnection = GetOwner()->GetNetConnection();
        if (Front.ExpiresAt <= FPlatformTime::Seconds() || !Front.Connection.IsValid() ||
            Front.Connection.Get() != RemoteConnection || Front.ToServer == Authority ||
            (Front.ToServer ? IntegrityFailed : !Front.Sender.IsValid()))
        {
            PendingPredictionPackets.RemoveAt(0);
            continue;
        }
        if (!RemoteConnection->IsNetReady())
            break;
        FPendingPredictionPacket Packet = MoveTemp(PendingPredictionPackets[0]);
        PendingPredictionPackets.RemoveAt(0);
        if (Packet.ToServer)
        {
            // A client status may be "prediction synchronizing" or combat paused.
            // Only the acknowledged seat generation and transport must still match.
            if (LastDelivery.Room == Packet.Room && LastDelivery.Membership == Packet.SenderMembership &&
                LastDelivery.Match == Packet.Match && LastDelivery.Assignment == Packet.SenderAssignment &&
                LastDelivery.FighterSeat == Packet.SenderSeat && LastDelivery.FighterSeat >= 0 &&
                LastDelivery.FighterSeat <= 1)
            {
                ServerPredictionPacket(Packet.Match, Packet.SenderAssignment, Packet.Bytes);
            }
            continue;
        }
        URoomConnection *Sender = Packet.Sender.Get();
        if (!Sender || Sender->GetWorld() != World || !Sender->GetOwner() ||
            !Sender->GetOwner()->HasAuthority() || Sender->AuthenticatedIdentity != Packet.SenderIdentity ||
            AuthenticatedIdentity != Packet.RecipientIdentity || AuthenticatedIdentity.IsEmpty())
            continue;
        const auto SenderState = Room->Observe(Packet.SenderIdentity);
        const auto RecipientState = Room->Observe(AuthenticatedIdentity);
        if (SenderState.Status != TEXT("ok") || RecipientState.Status != TEXT("ok") ||
            SenderState.FighterSeat < 0 || RecipientState.FighterSeat != 1 - SenderState.FighterSeat ||
            SenderState.Room != Packet.Room || RecipientState.Room != Packet.Room ||
            SenderState.Match != Packet.Match || RecipientState.Match != Packet.Match ||
            SenderState.Membership != Packet.SenderMembership ||
            SenderState.Assignment != Packet.SenderAssignment ||
            RecipientState.Membership != Packet.RecipientMembership ||
            RecipientState.Assignment != Packet.RecipientAssignment ||
            Packet.ExpiresAt <= FPlatformTime::Seconds())
            continue;
        ClientPredictionPacket(Packet.Match, Packet.Bytes);
    }
}

void URoomConnection::ClientPredictionPacket_Implementation(const FString &Match, const TArray<uint8> &Bytes)
{
    if (Match == LastDelivery.Match && LastDelivery.FighterSeat >= 0)
        GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>()->ReceivePrediction(Bytes);
}

void URoomConnection::EndPlay(const EEndPlayReason::Type Reason)
{
    PendingPredictionPackets.Empty();
    if (GetOwner()->HasAuthority() && !AuthenticatedIdentity.IsEmpty() &&
        Reason != EEndPlayReason::LevelTransition && !GetWorld()->IsInSeamlessTravel() &&
        !GetWorld()->bIsTearingDown && GetWorld()->NextURL.IsEmpty())
        GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>()->Depart(AuthenticatedIdentity);
    if (Panel)
    {
        Panel->RemoveFromParent();
        Panel = nullptr;
    }
    Super::EndPlay(Reason);
}

void URoomConnection::TickComponent(float Delta,
                                    ELevelTick TickType,
                                    FActorComponentTickFunction *TickFunction)
{
    Super::TickComponent(Delta, TickType, TickFunction);
    if (!IntegrityFailed && (!GetOwner()->HasAuthority() ||
        (Cast<APlayerController>(GetOwner()) && Cast<APlayerController>(GetOwner())->GetLocalPlayer())))
    {
        const FString Status = GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>()->TickPrediction(
            LastDelivery.Paused);
        if (!Status.IsEmpty() && Status != TEXT("ok") && LastDelivery.Status != Status)
        {
            LastDelivery.Status = Status;
            IntegrityFailed = Status == TEXT("integrity error");
            OnDelivery.Broadcast(LastDelivery);
        }
    }
    if (!GetOwner()->HasAuthority())
    {
        DrainPredictionPackets();
        return;
    }
    if (AuthenticatedIdentity.IsEmpty())
        return;
    PlaybackAccumulator += Delta;
    auto *Room = GetWorld()->GetGameInstance()->GetSubsystem<USpectatorRoom>();
    while (PlaybackAccumulator >= OneFrame)
    {
        Room->PlaybackTick(AuthenticatedIdentity);
        PlaybackAccumulator -= OneFrame;
    }
    // Service prediction before a bulk snapshot can consume this tick's send budget.
    DrainPredictionPackets();
    // Coalesce unsolicited snapshots under backpressure. Playback continues and
    // the unchanged signature causes the latest complete history to retry later.
    const UNetConnection *RemoteConnection = GetOwner()->GetNetConnection();
    if (RemoteConnection && !RemoteConnection->IsNetReady())
        return;
    const FRoomDelivery Delivery = Room->Observe(AuthenticatedIdentity);
    FRoomDelivery Visible = Delivery;
    Visible.Acknowledgement = 0;
    FBufferArchive Notice;
    FRoomDelivery::StaticStruct()->SerializeItem(Notice, &Visible, nullptr);
    const FString Signature = HashBytes(Notice);
    if (Signature != LastSentSignature)
    {
        LastSentSignature = Signature;
        SendDelivery(Delivery);
    }
}

TArray<FString> USpectatorRoom::SavedReplays() const
{
    TArray<FString> Names;
    IFileManager::Get().FindFiles(
        Names, *(FPaths::ProjectSavedDir() / TEXT("RoomReplays/*.room")), true, false);
    for (auto &Name : Names)
        Name = FPaths::GetBaseFilename(Name);
    return Names;
}

FString USpectatorRoom::PlaySavedReplay(const FString &ReplayId)
{
    DrainDepartures();
    if (!DeferredDepartures.IsEmpty() || !FlushConfirmation())
        return TEXT("storage pending");
    if (ReplayId != FPaths::MakeValidFileName(ReplayId))
        return TEXT("invalid replay identity");
    TArray<uint8> Bytes;
    FRoomMatch Match;
    if (!FFileHelper::LoadFileToArray(
            Bytes, *(FPaths::ProjectSavedDir() / TEXT("RoomReplays") / (ReplayId + TEXT(".room")))))
        return TEXT("history unavailable");
    if (!ReadCompactReplay(Bytes, Match) || Match.Id != ReplayId || !Match.Configuration.Stage)
        return TEXT("integrity error");
    auto *GameInstance = Cast<UNightSkyGameInstance>(GetGameInstance());
    if (!GameInstance)
        return TEXT("battle game instance required");
    const auto Content = InspectContent(Match.Configuration, GameInstance->BattleVersion, Match.AnnouncerData, Match.MusicData);
    for (const auto &Pair : Match.Content)
    {
        const auto *Local = Content.Find(Pair.Key);
        if (!Local || *Local != Pair.Value || Pair.Value.IsEmpty())
            return TEXT("content:") + Pair.Key;
    }
    if (!RestoreMatchStartup(GameInstance, Match))
        return TEXT("content:startup settings");
    GameInstance->CurrentReplay = NewObject<UReplaySaveInfo>(GameInstance);
    GameInstance->CurrentReplay->BattleData = Match.Configuration;
    GameInstance->CurrentReplay->Version = GameInstance->BattleVersion;
    GameInstance->CurrentReplay->bIsTraining = Match.Training;
    GameInstance->IsTraining = Match.Training;
    // A caller may have visited CPU battle since this two-human match was recorded.
    GameInstance->IsCPUBattle = false;
    for (int32 I = 1; I < Match.Frames.Num(); ++I)
    {
        GameInstance->CurrentReplay->InputsP1.Add(Match.Frames[I].Input1);
        GameInstance->CurrentReplay->InputsP2.Add(Match.Frames[I].Input2);
    }
    GameInstance->CurrentReplay->LengthInFrames = Match.Frames.Num() - 1;
    GameInstance->BattleData = Match.Configuration;
    GameInstance->IsReplay = true;
    GameInstance->FighterRunner = Multiplayer;
    ClientRoom = false;
    Store = nullptr;
    UGameplayStatics::OpenLevel(GameInstance, FName(*Match.Configuration.Stage->StageURL));
    return TEXT("playing");
}

FRoomDelivery USpectatorRoom::RejectCommand(const FString &Identity,
                                            const FRoomCommand &Request,
                                            const FString &Reason)
{
    auto Delivery = Observe(Identity);
    Delivery.Status = Reason;
    Delivery.RequestNonce = Request.Nonce;
    return Delivery;
}

TOptional<FRoomDelivery> USpectatorRoom::ApplyCommand(const FString &Identity,
                                                      const FRoomCommand &Request,
                                                      FRoomMember *&RequestingMember)
{
    const bool IsOrganizer = Identity == Store->Organizer;
    const bool HasActiveMatch = IsActive();
    if (Request.Operation == TEXT("leave"))
    {
        Depart(Identity);
        RequestingMember = Member(Identity);
    }
    else if (Request.Operation == TEXT("queue"))
    {
        if (RequestingMember->Seat >= 0)
            return RejectCommand(Identity, Request, TEXT("already fighter"));
        Store->Queue.AddUnique(Identity);
    }
    else if (Request.Operation == TEXT("withdraw"))
    {
        Store->Queue.Remove(Identity);
        Store->Offers.Remove(Identity);
    }
    else if (Request.Operation == TEXT("offer"))
    {
        return ApplySeatOffer(Identity, Request, RequestingMember, IsOrganizer, HasActiveMatch);
    }
    else if (Request.Operation == TEXT("accept") || Request.Operation == TEXT("decline"))
    {
        return ApplySeatResponse(Identity, Request, RequestingMember, IsOrganizer, HasActiveMatch);
    }
    else if (Request.Operation == TEXT("lock") || Request.Operation == TEXT("unlock"))
    {
        if (!IsOrganizer || HasActiveMatch)
            return RejectCommand(Identity, Request, TEXT("unauthorized"));
        Store->Locked = Request.Operation == TEXT("lock");
    }
    else if (Request.Operation == TEXT("combat-pause") || Request.Operation == TEXT("combat-resume"))
    {
        if (!IsOrganizer || !HasActiveMatch)
            return RejectCommand(Identity, Request, TEXT("unauthorized"));
        Store->Paused = Request.Operation == TEXT("combat-pause");
        // Accepted inputs keep their nonce and pending pair across a pause.
        Accumulator = 0;
    }
    else if (Request.Operation == TEXT("character") || Request.Operation == TEXT("stage"))
    {
        return ApplySelection(Identity, Request, RequestingMember, IsOrganizer, HasActiveMatch);
    }
    else if (Request.Operation == TEXT("ready"))
    {
        if (HasActiveMatch || RequestingMember->Seat < 0 ||
            Request.Assignment != RequestingMember->Assignment ||
            Request.Match != (Store->Matches.Num() > 0 ? Store->Matches.Last().Id : FString()))
            return RejectCommand(Identity, Request, TEXT("stale assignment"));
        if (Store->Content.IsEmpty() || !Store->Selection.Stage || Store->Selection.PlayerListP1.Num() == 0 ||
            Store->Selection.PlayerListP2.Num() == 0 || !ContentValid(*RequestingMember, Store->Content))
        {
            RequestingMember->Ready = false;
            return RejectCommand(Identity, Request, TEXT("content unavailable"));
        }
        RequestingMember->Ready = true;
    }
    else if (Request.Operation == TEXT("start"))
    {
        return StartSelectedMatch(Identity, Request, RequestingMember, IsOrganizer, HasActiveMatch);
    }
    else if (Request.Operation == TEXT("input"))
    {
        if (!HasActiveMatch || Store->Paused || RequestingMember->Seat < 0 ||
            Request.Assignment != RequestingMember->Assignment || Request.Match != Store->Matches.Last().Id ||
            Store->Matches.Last().Frames.IsEmpty() ||
            !ContentValid(*RequestingMember, Store->Matches.Last().Content) ||
            Request.Number != Store->Matches.Last().Frames.Num() ||
            PendingInputs.Contains(RequestingMember->Seat))
            return RejectCommand(Identity, Request, TEXT("input rejected"));
        int32 Bits = 0;
        int64 Parsed = 0;
        const FString Digits = Request.Value.StartsWith(TEXT("-")) || Request.Value.StartsWith(TEXT("+"))
                                   ? Request.Value.Mid(1) : Request.Value;
        bool Decimal = !Digits.IsEmpty();
        for (const TCHAR Ch : Digits)
            Decimal &= Ch >= TEXT('0') && Ch <= TEXT('9');
        if (!Decimal || !LexTryParseString(Parsed, *Request.Value) || Parsed < MIN_int32 || Parsed > MAX_int32)
            return RejectCommand(Identity, Request, TEXT("invalid input"));
        Bits = int32(Parsed);
        Bits &= ~(INP_Rematch | INP_ResetTraining);
        PendingInputs.Add(RequestingMember->Seat, Bits);
    }
    else if (Request.Operation == TEXT("select-match"))
    {
        auto *Match = FindMatch(Request.Match);
        if (!Match)
            return RejectCommand(Identity, Request, TEXT("history unavailable"));
        if (!ContentValid(*RequestingMember, Match->Content))
        {
            ContentRetryMatches.Add(Identity, Request.Match);
            return RejectCommand(Identity, Request, RequestingMember->Diagnostic);
        }
        RequestingMember->SelectedMatch = Request.Match;
        RequestingMember->Mode = TEXT("live");
        RequestingMember->Cursor = Edge(*Match);
    }
    else if (Request.Operation == TEXT("seek"))
    {
        auto *Match = FindMatch(RequestingMember->SelectedMatch);
        if (!Match || Request.Match != Match->Id || Request.Number < 0 || Request.Number > Edge(*Match))
            return RejectCommand(Identity, Request, TEXT("seek rejected"));
        RequestingMember->Cursor = Request.Number;
        RequestingMember->Mode = TEXT("paused");
    }
    else if (Request.Operation == TEXT("pause") || Request.Operation == TEXT("resume") ||
             Request.Operation == TEXT("live"))
    {
        if (RequestingMember->Mode == TEXT("live"))
            if (auto *Selected = FindMatch(RequestingMember->SelectedMatch))
                RequestingMember->Cursor = Edge(*Selected);
        RequestingMember->Mode = Request.Operation == TEXT("pause")    ? TEXT("paused")
                                 : Request.Operation == TEXT("resume") ? TEXT("playing")
                                                                       : TEXT("live");
    }
    else if (Request.Operation == TEXT("export"))
    {
        return ExportSelectedMatch(Identity, Request, RequestingMember, IsOrganizer, HasActiveMatch);
    }
    else
        return RejectCommand(Identity, Request, TEXT("unknown command"));
    return {};
}

TOptional<FRoomDelivery> USpectatorRoom::ApplySeatOffer(const FString &Identity,
                                                        const FRoomCommand &Request,
                                                        FRoomMember *&RequestingMember,
                                                        bool IsOrganizer,
                                                        bool HasActiveMatch)
{
    if (!IsOrganizer || HasActiveMatch || Request.Number < 0 || Request.Number > 1 ||
        !Store->Queue.Contains(Request.Value))
        return RejectCommand(Identity, Request, TEXT("offer rejected"));
    for (const auto &Other : Store->Members)
        if (Other.Seat == Request.Number)
            return RejectCommand(Identity, Request, TEXT("occupied"));
    auto *Recipient = Member(Request.Value);
    if (!Recipient || !Recipient->Present || Recipient->Seat >= 0)
        return RejectCommand(Identity, Request, TEXT("unavailable"));
    for (auto It = Store->Offers.CreateIterator(); It; ++It)
        if (It.Value().Seat == Request.Number)
            It.RemoveCurrent();
    FRoomOffer Offer;
    Offer.Seat = Request.Number;
    Offer.Id = CreateRoomIdentity();
    Store->Offers.Add(Request.Value, Offer);

    return {};
}

TOptional<FRoomDelivery> USpectatorRoom::ApplySeatResponse(const FString &Identity,
                                                           const FRoomCommand &Request,
                                                           FRoomMember *&RequestingMember,
                                                           bool IsOrganizer,
                                                           bool HasActiveMatch)
{
    const FRoomOffer *Offer = Store->Offers.Find(Identity);
    if (HasActiveMatch || !Offer || RequestingMember->Seat >= 0 || Request.Assignment != Offer->Id)
        return RejectCommand(Identity, Request, TEXT("stale offer"));
    if (Request.Operation == TEXT("accept"))
    {
        RequestingMember->Seat = Offer->Seat;
        RequestingMember->Assignment = CreateRoomIdentity();
        RequestingMember->Ready = false;
        Store->Queue.Remove(Identity);
    }
    Store->Offers.Remove(Identity);

    return {};
}

TOptional<FRoomDelivery> USpectatorRoom::ApplySelection(const FString &Identity,
                                                        const FRoomCommand &Request,
                                                        FRoomMember *&RequestingMember,
                                                        bool IsOrganizer,
                                                        bool HasActiveMatch)
{
    if (HasActiveMatch || Store->Locked)
        return RejectCommand(Identity, Request, TEXT("locked"));
    if (Request.Operation == TEXT("character"))
    {
        if (RequestingMember->Seat < 0 || Request.Assignment != RequestingMember->Assignment)
            return RejectCommand(Identity, Request, TEXT("unauthorized"));
        TSoftObjectPtr<UPrimaryCharaData> Asset{FSoftObjectPath(Request.Value)};
        if (!Asset.LoadSynchronous() || !Asset.Get()->PlayerClass)
            return RejectCommand(Identity, Request, TEXT("content:character"));
        if (RequestingMember->Seat == 0)
        {
            Store->Selection.PlayerListP1 = {Asset};
            Store->Selection.ColorIndicesP1 = {0};
        }
        else
        {
            Store->Selection.PlayerListP2 = {Asset};
            Store->Selection.ColorIndicesP2 = {0};
        }
    }
    else
    {
        if (!IsOrganizer)
            return RejectCommand(Identity, Request, TEXT("unauthorized"));
        auto *Stage = LoadObject<UPrimaryStageData>(nullptr, *Request.Value);
        if (!Stage)
            return RejectCommand(Identity, Request, TEXT("content:stage"));
        Store->Selection.Stage = Stage;
    }
    auto *GameInstance = Cast<UNightSkyGameInstance>(GetGameInstance());
    Store->Content = InspectContent(Store->Selection, GameInstance ? GameInstance->BattleVersion : TEXT(""),
        GameInstance ? GameInstance->AnnouncerData.Get() : nullptr, GameInstance ? GameInstance->MusicData.Get() : nullptr);
    for (auto &Other : Store->Members)
        Other.Ready = false;

    return {};
}

TOptional<FRoomDelivery> USpectatorRoom::StartSelectedMatch(const FString &Identity,
                                                            const FRoomCommand &Request,
                                                            FRoomMember *&RequestingMember,
                                                            bool IsOrganizer,
                                                            bool HasActiveMatch)
{
    if (!IsOrganizer || HasActiveMatch)
        return RejectCommand(Identity, Request, TEXT("unauthorized"));
    if (Store->Content.IsEmpty() || !Store->Selection.Stage || Store->Selection.PlayerListP1.IsEmpty() ||
        Store->Selection.PlayerListP2.IsEmpty())
        return RejectCommand(Identity, Request, TEXT("content unavailable"));
    FRoomMember *Seats[2] = {nullptr, nullptr};
    for (auto &Other : Store->Members)
        if (Other.Seat >= 0 && Other.Seat < 2)
            Seats[Other.Seat] = &Other;
    for (auto *Seat : Seats)
        if (!Seat || !Seat->Present || !Seat->Ready || !ContentValid(*Seat, Store->Content))
            return RejectCommand(Identity, Request, TEXT("fighters not ready"));
    FRoomMatch Match;
    Match.Id = CreateRoomIdentity();
    Match.StartTick = RoomTick();
    Match.Participants = {Seats[0]->Id, Seats[1]->Id};
    Match.Configuration = Store->Selection;
    Match.Content = Store->Content;
    if (auto *GameInstance = Cast<UNightSkyGameInstance>(GetGameInstance()))
    {
        Match.Training = GameInstance->IsTraining;
        Match.AnnouncerData = GameInstance->AnnouncerData;
        Match.MusicData = GameInstance->MusicData;
        if (GameInstance->SettingsInfo && !UGameplayStatics::SaveGameToMemory(GameInstance->SettingsInfo, Match.StartupSettings))
            return RejectCommand(Identity, Request, TEXT("content:startup settings"));
        Match.Content = InspectContent(Match.Configuration, GameInstance->BattleVersion, Match.AnnouncerData, Match.MusicData);
        for (auto *Seat : Seats)
            if (!ContentValid(*Seat, Match.Content))
                return RejectCommand(Identity, Request, TEXT("fighters not ready"));
    }
    const FString Initialized = InitializeMatch(Match);
    if (Initialized != TEXT("ok"))
        return RejectCommand(Identity, Request, Initialized);
    Store->Matches.Add(Match);
    Store->Recovering = false;
    Store->Offers.Empty();
    Store->Paused = false;
    PendingInputs.Empty();
    Accumulator = 0;
    for (auto &Other : Store->Members)
    {
        Other.Ready = false;
        if (Other.SelectedMatch.IsEmpty())
            Other.SelectedMatch = Match.Id;
    }
    return {};
}

TOptional<FRoomDelivery> USpectatorRoom::ExportSelectedMatch(const FString &Identity,
                                                             const FRoomCommand &Request,
                                                             FRoomMember *&RequestingMember,
                                                             bool IsOrganizer,
                                                             bool HasActiveMatch)
{
    auto *Match = FindMatch(Request.Match);
    if (!Match)
        return RejectCommand(Identity, Request, TEXT("history unavailable"));
    if (Match->OutcomeTick < 0 || Match->Frames.Num() == 0 || Edge(*Match) != Match->Frames.Num() - 1 ||
        RoomTick() - Match->OutcomeTick < Store->Delay)
        return RejectCommand(Identity, Request, TEXT("pending"));
    // Export preserves the recording and its required revisions. Installed content
    // is checked when presenting or playing it, not when copying released history.
    auto *Export = NewObject<URoomStore>();
    Export->Id = Store->Id;
    Export->Matches.Add(*Match);
    FRoomDelivery Delivery = Observe(Identity);
    Delivery.Replay = EncodeRoomStore(Export);
    Delivery.Status = TEXT("exported");
    Delivery.RequestNonce = Request.Nonce;
    return Delivery;
}

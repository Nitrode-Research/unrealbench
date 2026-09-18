#include "SpectatorRoom.h"

// Compile-safe public seams only. No room policy, persistence, transport,
// prediction, content validation or playback implementation is supplied here.

bool USpectatorRoom::Create(const FString &Slot, const FString &Organizer, const FString &Secret, int32 Delay)
{
    return false;
}

bool USpectatorRoom::Recover(const FString &Slot)
{
    return false;
}

int64 USpectatorRoom::RoomTick() const
{
    return 0;
}

bool USpectatorRoom::IsActive() const
{
    return false;
}

bool USpectatorRoom::Authenticate(const FString &Identity,
                                  const FString &Secret,
                                  const TMap<FString, FString> &Content)
{
    return false;
}

TMap<FString, FString> USpectatorRoom::InspectRequiredContent(const TMap<FString, FString> &Required,
                                                              const FString &GameplayRevision)
{
    return {};
}

TMap<FString, FString> USpectatorRoom::InspectContent(const FBattleData &Config,
                                                      const FString &GameplayRevision)
{
    return {};
}

void USpectatorRoom::Finish(const FString &Outcome)
{
}

void USpectatorRoom::Depart(const FString &Identity)
{
}

FRoomDelivery USpectatorRoom::Observe(const FString &Identity)
{
    FRoomDelivery Delivery;
    Delivery.Status = TEXT("not implemented");
    return Delivery;
}

void USpectatorRoom::PlaybackTick(const FString &Identity)
{
}

FRoomDelivery USpectatorRoom::Command(const FString &Identity, const FRoomCommand &C)
{
    FRoomDelivery Delivery;
    Delivery.Status = TEXT("not implemented");
    return Delivery;
}

void USpectatorRoom::BindBattle(ANightSkyGameState *InBattle)
{
}

void USpectatorRoom::BattleTick(float Delta)
{
}

bool USpectatorRoom::DecodeConfirmedInputs(const FRoomFrame &Frame,
                                           FString &MatchId,
                                           TArray<int32> &InputsA,
                                           TArray<int32> &InputsB,
                                           FString &Expected)
{
    return false;
}

bool USpectatorRoom::PlayFrame(ANightSkyGameState *InBattle, const FRoomFrame &Frame)
{
    return false;
}

bool USpectatorRoom::BeginPrediction(URoomConnection *Connection, const FRoomDelivery &Delivery)
{
    return false;
}

void USpectatorRoom::SubmitPrediction(const FString &Match, int32 Frame, int32 Input)
{
}

void USpectatorRoom::ReceivePrediction(const TArray<uint8> &Bytes)
{
}

FString USpectatorRoom::TickPrediction(bool CombatPaused)
{
    return TEXT("not implemented");
}

int32 USpectatorRoom::PredictionFrame() const
{
    return -1;
}

int32 USpectatorRoom::PredictionRollbacks() const
{
    return 0;
}

int32 USpectatorRoom::PredictionConfirmedFrame() const
{
    return -1;
}

void URoomPlaybackGameInstance::Init()
{
    UGameInstance::Init();
}

ANightSkyGameState *USpectatorRoom::PresentationBattle() const
{
    return nullptr;
}

UTextureRenderTarget2D *USpectatorRoom::PresentationTexture() const
{
    return nullptr;
}

void USpectatorRoom::Deinitialize()
{
    Super::Deinitialize();
}

FString USpectatorRoom::PresentDelivery(const FRoomDelivery &Delivery)
{
    return TEXT("not implemented");
}

bool USpectatorRoom::ReadReplay(const TArray<uint8> &Bytes, FRoomMatch &Match)
{
    return false;
}

URoomConnection::URoomConnection()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void URoomConnection::Authenticate(const FString &Identity,
                                   const FString &Secret,
                                   const TArray<FString> &Content)
{
}

void URoomConnection::BeginPlay()
{
    Super::BeginPlay();
}

void URoomConnection::ServerAuthenticate_Implementation(const FString &Identity,
                                                        const FString &Secret,
                                                        const TArray<FString> &Content)
{
}

void URoomConnection::Submit(const FRoomCommand &C)
{
}

void URoomConnection::ServerSubmit_Implementation(const FRoomCommand &C)
{
}

FRoomAuthorityDeliveryEvent URoomConnection::AuthorityDelivery;

void URoomConnection::SendDelivery(const FRoomDelivery &Delivery)
{
}

void URoomConnection::DeliverPart_Implementation(const FString &Transfer,
                                                 int32 Index,
                                                 int32 Count,
                                                 const TArray<uint8> &Bytes)
{
}

void URoomConnection::Deliver_Implementation(const FRoomDelivery &D)
{
}

void URoomConnection::ServerPredictionPacket_Implementation(const FString &Match,
                                                            const FString &Assignment,
                                                            const TArray<uint8> &Bytes)
{
}

void URoomConnection::ClientPredictionPacket_Implementation(const FString &Match, const TArray<uint8> &Bytes)
{
}

void URoomConnection::EndPlay(const EEndPlayReason::Type Reason)
{
    Super::EndPlay(Reason);
}

void URoomConnection::TickComponent(float Delta,
                                    ELevelTick TickType,
                                    FActorComponentTickFunction *TickFunction)
{
    Super::TickComponent(Delta, TickType, TickFunction);
}

TArray<FString> USpectatorRoom::SavedReplays() const
{
    return {};
}

FString USpectatorRoom::PlaySavedReplay(const FString &ReplayId)
{
    return TEXT("not implemented");
}

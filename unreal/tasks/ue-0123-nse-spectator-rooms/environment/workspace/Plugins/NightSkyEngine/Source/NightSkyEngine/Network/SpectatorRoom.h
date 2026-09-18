#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Components/ActorComponent.h"
#include "GameFramework/SaveGame.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "SpectatorRoom.generated.h"

USTRUCT(BlueprintType)

struct NIGHTSKYENGINE_API FRoomCommand
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite)
    FString Operation;
    UPROPERTY(BlueprintReadWrite)
    FString Value;
    UPROPERTY(BlueprintReadWrite)
    FString Match;
    UPROPERTY(BlueprintReadWrite)
    FString Assignment;
    UPROPERTY(BlueprintReadWrite)
    FString Nonce;
    UPROPERTY(BlueprintReadWrite)
    FString Membership;
    UPROPERTY(BlueprintReadWrite)
    int32 Number = 0;
};
USTRUCT(BlueprintType)

struct NIGHTSKYENGINE_API FRoomFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    int64 ConfirmedTick = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 Input1 = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 Input2 = 0;
    UPROPERTY()
    TArray<uint8> State;
    UPROPERTY(BlueprintReadOnly)
    FString Digest;
};
USTRUCT(BlueprintType)

struct NIGHTSKYENGINE_API FRoomMatch
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    FString Id;
    UPROPERTY(BlueprintReadOnly)
    TArray<FString> Participants;
    UPROPERTY(BlueprintReadOnly)
    int64 StartTick = 0;
    UPROPERTY()
    bool Training = false;
    UPROPERTY()
    TSubclassOf<class ANightSkyGameState> BattleClass;
    UPROPERTY()
    FBattleData Configuration;
    UPROPERTY()
    TMap<FString, FString> Content;
    UPROPERTY()
    TArray<FRoomFrame> Frames;
    UPROPERTY(BlueprintReadOnly)
    FString Outcome;
    UPROPERTY(BlueprintReadOnly)
    int64 OutcomeTick = -1;
};
USTRUCT(BlueprintType)

struct NIGHTSKYENGINE_API FRoomDelivery
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    FString Status;
    UPROPERTY(BlueprintReadOnly)
    FString RequestNonce;
    UPROPERTY(BlueprintReadOnly)
    FString Membership;
    UPROPERTY(BlueprintReadOnly)
    FString Room;
    UPROPERTY(BlueprintReadOnly)
    FString Match;
    UPROPERTY(BlueprintReadOnly)
    FString Role;
    UPROPERTY(BlueprintReadOnly)
    int32 FighterSeat = -1;
    UPROPERTY(BlueprintReadOnly)
    FString Assignment;
    UPROPERTY(BlueprintReadOnly)
    FString Mode;
    UPROPERTY(BlueprintReadOnly)
    FString Outcome;
    UPROPERTY(BlueprintReadOnly)
    FString Offer;
    UPROPERTY(BlueprintReadOnly)
    int32 OfferedSeat = -1;
    UPROPERTY(BlueprintReadOnly)
    int32 Frame = -1;
    UPROPERTY(BlueprintReadOnly)
    int32 Edge = -1;
    UPROPERTY(BlueprintReadOnly)
    bool Locked = false;
    UPROPERTY(BlueprintReadOnly)
    bool Recovering = false;
    UPROPERTY(BlueprintReadOnly)
    FString SelectedStage;
    UPROPERTY(BlueprintReadOnly)
    TArray<FString> SelectedCharacters;
    UPROPERTY(BlueprintReadOnly)
    TMap<FString, FString> RequiredContent;
    UPROPERTY(BlueprintReadOnly)
    bool Paused = false;
    UPROPERTY(BlueprintReadOnly)
    int64 Acknowledgement = 0;
    UPROPERTY(BlueprintReadOnly)
    TArray<FString> Roster;
    UPROPERTY(BlueprintReadOnly)
    FRoomFrame Gameplay;
    UPROPERTY()
    TArray<uint8> Setup;
    UPROPERTY()
    TArray<uint8> Replay;
};
/** Isolated game instance for a retained timeline; it never owns the room connection. */
UCLASS()

class NIGHTSKYENGINE_API URoomPlaybackGameInstance : public UNightSkyGameInstance
{
    GENERATED_BODY()
  public:
    virtual void Init() override;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRoomDeliveryEvent, const FRoomDelivery &, Delivery);
DECLARE_MULTICAST_DELEGATE_OneParam(FRoomAuthorityDeliveryEvent, const FRoomDelivery &);
UCLASS()

class NIGHTSKYENGINE_API USpectatorRoom : public UGameInstanceSubsystem
{
    GENERATED_BODY()
  public:
    FString PresentDelivery(const FRoomDelivery &Delivery);
    bool BeginPrediction(class URoomConnection *Connection, const FRoomDelivery &Delivery);
    void SubmitPrediction(const FString &Match, int32 Frame, int32 Input);
    void ReceivePrediction(const TArray<uint8> &Bytes);
    FString TickPrediction(bool CombatPaused = false);
    int32 PredictionFrame() const;
    int32 PredictionRollbacks() const;
    int32 PredictionConfirmedFrame() const;
    class ANightSkyGameState *PresentationBattle() const;
    UFUNCTION(BlueprintCallable)
    class UTextureRenderTarget2D *PresentationTexture() const;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable)
    FString PlaySavedReplay(const FString &ReplayId);
    UFUNCTION(BlueprintCallable)
    TArray<FString> SavedReplays() const;
    UFUNCTION(BlueprintCallable)
    bool Create(const FString &Slot, const FString &Organizer, const FString &Secret, int32 Delay);
    UFUNCTION(BlueprintCallable)
    bool Recover(const FString &Slot);
    bool Authenticate(const FString &Identity, const FString &Secret, const TMap<FString, FString> &Content);
    FRoomDelivery Command(const FString &Identity, const FRoomCommand &Command);
    FRoomDelivery Observe(const FString &Identity);
    void Depart(const FString &Identity);
    void BindBattle(class ANightSkyGameState *InBattle);
    void BattleTick(float Delta);
    void PlaybackTick(const FString &Identity);

    bool IsEnabled() const
    {
        return false;
    }

    bool IsActive() const;

    void EnableClient()
    {
    }

    FString LoginIdentity, LoginSecret;
    TArray<FString> LoginContent;
    void Finish(const FString &Outcome);
    int64 RoomTick() const;

    void SetClock(TFunction<int64()> Provider)
    {
    }

    static TMap<FString, FString> InspectRequiredContent(const TMap<FString, FString> &Required,
                                                         const FString &GameplayRevision);
    static TMap<FString, FString> InspectContent(const FBattleData &Configuration,
                                                 const FString &GameplayRevision);
    static bool DecodeConfirmedInputs(const FRoomFrame &Frame,
                                      FString &MatchId,
                                      TArray<int32> &InputsA,
                                      TArray<int32> &InputsB,
                                      FString &Expected);
    static bool PlayFrame(class ANightSkyGameState *InBattle, const FRoomFrame &Frame);
    static bool ReadReplay(const TArray<uint8> &Bytes, FRoomMatch &Match);
};
UCLASS(ClassGroup = Network, meta = (BlueprintSpawnableComponent))

class NIGHTSKYENGINE_API URoomConnection : public UActorComponent
{
    GENERATED_BODY()
  public:
    URoomConnection();
    UFUNCTION(BlueprintCallable)
    void Authenticate(const FString &Identity, const FString &Secret, const TArray<FString> &Content);
    UFUNCTION(Server, Reliable)
    void ServerAuthenticate(const FString &Identity, const FString &Secret, const TArray<FString> &Content);
    UFUNCTION(BlueprintCallable)
    void Submit(const FRoomCommand &Command);
    UFUNCTION(Server, Reliable)
    void ServerSubmit(const FRoomCommand &Command);
    static FRoomAuthorityDeliveryEvent AuthorityDelivery;
    void SendDelivery(const FRoomDelivery &Delivery);

    void Deliver(const FRoomDelivery &Delivery)
    {
        Deliver_Implementation(Delivery);
    }

    void Deliver_Implementation(const FRoomDelivery &Delivery);
    UFUNCTION(Client, Reliable)
    void DeliverPart(const FString &Transfer, int32 Index, int32 Count, const TArray<uint8> &Bytes);
    UPROPERTY(BlueprintReadOnly)
    int32 ReceivedChunks = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 ReassembledDeliveries = 0;
    UFUNCTION(Server, Unreliable)
    void ServerPredictionPacket(const FString &Match, const FString &Assignment, const TArray<uint8> &Bytes);
    UFUNCTION(Client, Unreliable)
    void ClientPredictionPacket(const FString &Match, const TArray<uint8> &Bytes);
    UPROPERTY(BlueprintAssignable)
    FRoomDeliveryEvent OnTransportDelivery;
    UPROPERTY(BlueprintAssignable)
    FRoomDeliveryEvent OnDelivery;
    UPROPERTY(BlueprintReadOnly)
    FRoomDelivery LastDelivery;
    UPROPERTY()
    TObjectPtr<class URoomPanel> Panel;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime,
                               ELevelTick TickType,
                               FActorComponentTickFunction *TickFunction) override;
};

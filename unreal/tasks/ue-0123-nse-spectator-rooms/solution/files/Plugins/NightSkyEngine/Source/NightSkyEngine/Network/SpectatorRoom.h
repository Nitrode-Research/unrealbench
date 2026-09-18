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
    // Preserve startup inputs independently of later host settings or viewer defaults.
    UPROPERTY()
    TArray<uint8> StartupSettings;
    UPROPERTY()
    TObjectPtr<USoundData> AnnouncerData;
    UPROPERTY()
    TObjectPtr<USoundData> MusicData;
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

struct NIGHTSKYENGINE_API FRoomMember
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    FString Id;
    UPROPERTY(BlueprintReadOnly)
    FString Membership;
    UPROPERTY()
    FString Credential;
    UPROPERTY(BlueprintReadOnly)
    int32 Seat = -1;
    UPROPERTY(BlueprintReadOnly)
    FString Assignment;
    UPROPERTY(BlueprintReadOnly)
    FString SelectedMatch;
    UPROPERTY(BlueprintReadOnly)
    int32 Cursor = -1;
    UPROPERTY(BlueprintReadOnly)
    FString Mode = TEXT("live");
    UPROPERTY(BlueprintReadOnly)
    bool Ready = false;
    UPROPERTY(BlueprintReadOnly)
    bool Present = true;
    UPROPERTY()
    TMap<FString, FString> Content;
    UPROPERTY()
    TSet<FString> Consumed;
    UPROPERTY(BlueprintReadOnly)
    FString Diagnostic;
    // Acknowledges this member's durable operations without counting hidden gameplay.
    UPROPERTY()
    int64 Acknowledgement = 0;
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
    // Public selection identities only. Outcomes and frame bounds use their release gates.
    UPROPERTY(BlueprintReadOnly)
    TArray<FString> RetainedMatches;
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
USTRUCT()

struct FRoomOffer
{
    GENERATED_BODY()
    UPROPERTY()
    FString Id;
    UPROPERTY()
    int32 Seat = -1;
};
UCLASS()

class NIGHTSKYENGINE_API URoomStore : public USaveGame
{
    GENERATED_BODY()
  public:
    UPROPERTY()
    FString Id;
    UPROPERTY()
    FString Organizer;
    UPROPERTY()
    int32 Delay = 0;
    UPROPERTY()
    int64 Epoch = 0;
    UPROPERTY()
    int64 Revision = 0;
    UPROPERTY()
    bool Locked = false;
    UPROPERTY()
    bool Recovering = false;
    UPROPERTY()
    bool Paused = false;
    UPROPERTY()
    TArray<FRoomMember> Members;
    UPROPERTY()
    TArray<FRoomMatch> Matches;
    UPROPERTY()
    TArray<FString> Queue;
    UPROPERTY()
    TMap<FString, FRoomOffer> Offers;
    UPROPERTY()
    FBattleData Selection;
    UPROPERTY()
    TMap<FString, FString> Content;
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
        return Store != nullptr || ClientRoom;
    }

    bool IsActive() const;

    void EnableClient()
    {
        ClientRoom = true;
    }

    FString LoginIdentity, LoginSecret;
    TArray<FString> LoginContent;
    void Finish(const FString &Outcome);
    int64 RoomTick() const;

    void SetClock(TFunction<int64()> Provider)
    {
        Clock = MoveTemp(Provider);
    }

    static TMap<FString, FString> InspectRequiredContent(const TMap<FString, FString> &Required,
                                                         const FString &GameplayRevision);
    static TMap<FString, FString> InspectContent(const FBattleData &Configuration,
                                                 const FString &GameplayRevision);
    static TMap<FString, FString> InspectContent(const FBattleData &Configuration,
                                                 const FString &GameplayRevision,
                                                 USoundData *AnnouncerData,
                                                 USoundData *MusicData);
    static bool DecodeConfirmedInputs(const FRoomFrame &Frame,
                                      FString &MatchId,
                                      TArray<int32> &InputsA,
                                      TArray<int32> &InputsB,
                                      FString &Expected);
    static bool PlayFrame(class ANightSkyGameState *InBattle, const FRoomFrame &Frame);
    static bool ReadReplay(const TArray<uint8> &Bytes, FRoomMatch &Match);

  private:
    static bool PlayFrameInternal(class ANightSkyGameState *InBattle, const FRoomFrame &Frame, bool Quiet, FString *PresentationError = nullptr);
    FRoomDelivery RejectCommand(const FString &Identity, const FRoomCommand &Request, const FString &Reason);
    TOptional<FRoomDelivery> ApplyCommand(const FString &Identity,
                                          const FRoomCommand &Request,
                                          FRoomMember *&RequestingMember);
    TOptional<FRoomDelivery> ApplySeatOffer(const FString &Identity,
                                            const FRoomCommand &Request,
                                            FRoomMember *&RequestingMember,
                                            bool IsOrganizer,
                                            bool HasActiveMatch);
    TOptional<FRoomDelivery> ApplySeatResponse(const FString &Identity,
                                               const FRoomCommand &Request,
                                               FRoomMember *&RequestingMember,
                                               bool IsOrganizer,
                                               bool HasActiveMatch);
    TOptional<FRoomDelivery> ApplySelection(const FString &Identity,
                                            const FRoomCommand &Request,
                                            FRoomMember *&RequestingMember,
                                            bool IsOrganizer,
                                            bool HasActiveMatch);
    TOptional<FRoomDelivery> StartSelectedMatch(const FString &Identity,
                                                const FRoomCommand &Request,
                                                FRoomMember *&RequestingMember,
                                                bool IsOrganizer,
                                                bool HasActiveMatch);
    TOptional<FRoomDelivery> ExportSelectedMatch(const FString &Identity,
                                                 const FRoomCommand &Request,
                                                 FRoomMember *&RequestingMember,
                                                 bool IsOrganizer,
                                                 bool HasActiveMatch);

    UPROPERTY()
    TObjectPtr<URoomStore> Store;
    // Recoverable but not yet anchored state is never served by Observe.
    UPROPERTY()
    TObjectPtr<URoomStore> ConfirmingStore;
    TArray<uint8> ConfirmationAnchor;
    bool ConfirmationNeedsCompaction = false;
    TMap<FString, int64> DeferredDepartures;
    TMap<FString, FString> DeferredDepartureMemberships;
    bool FlushConfirmation();
    bool CommitConfirmation(const FRoomFrame *Frame, const FString &Outcome);
    void DrainDepartures();
    bool DepartAt(const FString &Identity, int64 AcceptedTick);
    UPROPERTY()
    TObjectPtr<URoomPlaybackGameInstance> PlaybackGame;
    UPROPERTY()
    TObjectPtr<class UTextureRenderTarget2D> PlaybackTexture;
    TWeakObjectPtr<class ANightSkyGameState> PlaybackBattle;
    TWeakObjectPtr<class ASceneCapture2D> PlaybackCapture;
    FString PlaybackMatch, PlaybackRoom;
    struct FVerifiedTimeline
    {
        TArray<int32> InputsA, InputsB;
        FString InitialDigest;
        bool Failed = false;
    };
    // Commitments outlive presentation actors and survive A -> B -> A travel.
    TMap<FString, TMap<FString, FVerifiedTimeline>> VerifiedTimelines;
    TSharedPtr<struct FRoomPrediction> Prediction;
    FString PendingPredictionMatch;
    TMap<int32, int32> PendingPredictionInputs;
    void ClosePresentation();
    TWeakObjectPtr<class ANightSkyGameState> Battle;
    FString SaveSlot;
    TArray<uint8> LastDurable;
    TFunction<int64()> Clock;
    TMap<int32, int32> PendingInputs;
    // Diagnostic request context only; acknowledged selection remains in Store.
    TMap<FString, FString> ContentRetryMatches;
    float Accumulator = 0;
    double MonotonicEpoch = 0;
    int64 BaseTick = 0;
    bool ClientRoom = false;
    bool ApplyingCommand = false;
    bool CommittingFrame = false;
    FString PendingOutcome;
    bool Save();
    void Retain();
    bool ContentValid(FRoomMember &Member, const TMap<FString, FString> &Required);
    FRoomMember *Member(const FString &Identity);
    FRoomMatch *FindMatch(const FString &Identity);
    int32 Edge(const FRoomMatch &Match) const;
    bool Capture(int32 Input1, int32 Input2);
    FString InitializeMatch(FRoomMatch &Match);
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
    TMap<FString, FRoomCommand> PendingPredictionCommands;
    FString AttemptedContent;
    FString IncomingTransfer;
    FString CompletedTransfer;
    TArray<FString> CompletedPartHashes;
    TArray<TArray<uint8>> IncomingParts;
    TSet<int32> ReceivedParts;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime,
                               ELevelTick TickType,
                               FActorComponentTickFunction *TickFunction) override;

  private:
    friend struct FRoomPrediction;
    struct FPendingPredictionPacket
    {
        TWeakObjectPtr<URoomConnection> Sender;
        TWeakObjectPtr<class UNetConnection> Connection;
        FString SenderIdentity, SenderMembership, SenderAssignment;
        FString RecipientIdentity, RecipientMembership, RecipientAssignment, Room, Match;
        TArray<uint8> Bytes;
        double ExpiresAt = 0;
        int32 SenderSeat = -1;
        bool ToServer = false;
    };
    TArray<FPendingPredictionPacket> PendingPredictionPackets;
    void SendPredictionPacket(const FString &Match, const FString &Assignment, const TArray<uint8> &Bytes);
    void RelayPredictionPacket(URoomConnection *Sender, const FRoomDelivery &SenderState,
                               const FRoomDelivery &RecipientState, const TArray<uint8> &Bytes);
    void DrainPredictionPackets();
    struct FIncomingRoomTransfer
    {
        TArray<TArray<uint8>> Parts;
        TSet<int32> Received;
    };
    TMap<FString, FIncomingRoomTransfer> Transfers;
    TMap<FString, TArray<FString>> CompletedTransfers;
    FString AuthenticatedIdentity;
    float PlaybackAccumulator = 0;
    bool IntegrityFailed = false;
    FString LastSentSignature;
};

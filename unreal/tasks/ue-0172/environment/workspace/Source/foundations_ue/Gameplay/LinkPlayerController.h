#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Rules/LinkCommands.h"
#include "Rules/LinkNavigation.h"
#include "Rules/LinkCamera.h"
#include "Rules/LinkCameraResponse.h"
#include "Rules/LinkInteractionCamera.h"
#include "LinkPlayerController.generated.h"

UCLASS()
class FOUNDATIONS_UE_API ALinkPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void UpdateCameraManager(float DeltaSeconds) override;
    UFUNCTION(Exec) void SetCameraWeight(int32 Percent);
    UFUNCTION(Exec) void PreviewInteractionCamera(FName Id);
    FName GetSelectedCameraId() const { return CameraTransition.Selected; }
    const FLinkCameraTransition& GetCameraTransition() const { return CameraTransition; }
    float GetCameraWeight() const { return ViewTuning.CameraWeight; }
    const FLinkCameraFocusState& GetCameraFocusState() const { return CameraFocus; }
    ALinkPlayerController();
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaTime) override;
    virtual void SetupInputComponent() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    class ALinkCharacter* GetCharacterAt(int32 Index) const;
    bool CommandMove(int32 Index, const FVector& Target);
    bool NavigateCharacter(int32 Index, const FVector& Target);
    void ApplyCommandFrame(const FLinkCommandFrame& Frame, const FVector& Target);
    const FLinkCommands& GetCommandState() const { return Commands; }
    bool InteractWith(class ALinkInteractionActor* Target,int32 PlayerIndex);
    class ALinkInteractionActor* GetInteractionFor(int32 PlayerIndex) const;
    class ALinkInteractionActor* GetActiveInteraction() const { return ActiveInteraction; }
    class ALinkInteractionActor* GetHoveredInteraction() const { return HoveredInteraction; }
    double GetCommandFeedbackTime(int32 Index) const { return CommandFeedbackTimes[Index]; }
    FVector GetCommandFeedbackLocation(int32 Index) const { return CommandFeedbackLocations[Index]; }
    void CloseInteractionView();
    void TogglePauseOrClose();
    void ToggleAudio();
    FString AudioLabel() const;
    void QuickSave();
    void QuickLoad();
    bool SaveToSlot(const FString& Slot);
    bool LoadFromSlot(const FString& Slot);
    void AutoSave();
    void RestartRun();
    bool IsRunCompleted() const;
    bool ChooseAction(int32 EntryId);
    const TArray<int32>& GetActionChoices() const { return ActionChoices; }
    FString ActionLabel(int32 EntryId) const;
    FString InventoryLabel(int32 PlayerIndex) const;
    FString HoverLabel() const;
    const FString& GetFeedback() const { return Feedback; }
    int32 GetViewRevision() const { return ViewRevision; }
    bool IsWaitingForPartner() const { return AwaitedParticipants!=0; }
    class ACameraActor* GetSharedCamera() const { return SharedCamera; }
    class ALinkChain* GetChain() const { return Chain; }

private:
    void CommandLeft();
    void CommandRight();
    void UpdateCommands();
    void UpdateMovement();
    void UpdateSharedView(float DeltaTime, bool bSnap=false);
    void LeaveInteraction(int32 PlayerIndex);
    void OpenInteraction(ALinkInteractionActor* Target);
    void PumpInteraction();
    void UpdatePartnerArrival();
    void CompleteRoute();
    void TravelToMap(FName Map);
    bool FindRoute(class ALinkCharacter* From, const FVector& Target, TArray<FVector>& OutCorners) const;
    FLinkCommands Commands;
    FLinkMotionTuning Tuning;
    FLinkCameraTuning ViewTuning;
    FLinkCameraState ViewState;
    FLinkCameraFocusState CameraFocus;
    FLinkInteractionCameraLibrary InteractionCameras;
    FLinkCameraTransition CameraTransition;
    TMap<FName,FVector> InteractionCameraTargets;
    FName ReviewCameraId,CameraScene;
    uint8 PerformedButtons = 0;
    FVector Destinations[2] = {FVector::ZeroVector, FVector::ZeroVector};
    FVector RememberedDirections[2] = {FVector::ZeroVector, FVector::ZeroVector};
    double CommandFeedbackTimes[2]={-100,-100};
    FVector CommandFeedbackLocations[2]={FVector::ZeroVector,FVector::ZeroVector};
    UPROPERTY()
    TArray<TObjectPtr<class ALinkCharacter>> Characters;
    UPROPERTY()
    TObjectPtr<class ACameraActor> SharedCamera;
    UPROPERTY()
    TObjectPtr<class ALinkChain> Chain;
    UPROPERTY() TArray<TObjectPtr<class ALinkInteractionActor>> Interactions;
    UPROPERTY() TObjectPtr<class ALinkInteractionActor> HoveredInteraction;
    UPROPERTY() TObjectPtr<class ALinkInteractionActor> ActiveInteraction;
    TUniquePtr<class FLinkStoryEngine> InteractionEngine;
    TArray<int32> ActionChoices;
    int32 QueuedEntry=0,ViewRevision=0;
    FString Feedback;
    bool bRouteRequested=false;
    uint8 AwaitedParticipants=0;
    bool bTransitioning=false;
    FTimerHandle TransitionTimer;
    TSharedPtr<class SLinkHUD> Interface;
};

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rules/LinkInteraction.h"
#include "LinkInteractionActor.generated.h"

UCLASS()
class FOUNDATIONS_UE_API ALinkInteractionActor : public AActor
{
    GENERATED_BODY()
public:
    ALinkInteractionActor();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    bool Focus(int32 Player,class ALinkCharacter* Character);
    void Leave(int32 Player);
    FVector WaypointFor(int32 Player) const;
    bool IsReady(int32 Player) const { return Participants.IsReady(Player); }
    FLinkStoryContext& GetContext() { return Context; }
    const FLinkInteractionState& GetParticipants() const { return Participants; }
    UPROPERTY(EditAnywhere,Category="Interaction") FString PersistentId;
    UPROPERTY(EditAnywhere,Category="Interaction") FString DisplayName=TEXT("Interact");
    UPROPERTY(EditAnywhere,Category="Interaction") int32 EventId=0;
    UPROPERTY(EditAnywhere,Category="Interaction") int32 InitialItem=0;
    UPROPERTY(EditAnywhere,Category="Interaction") FName DestinationMap;
    UPROPERTY(EditAnywhere,Category="Interaction") bool bCompletesRun=false;
    UPROPERTY(EditAnywhere,Category="Interaction") float ArrivalRadius=170;
    UPROPERTY(EditAnywhere,Category="Interaction") int32 VisualItemId=0;
    UPROPERTY(EditAnywhere,Category="Interaction") FName ItemVisualTag;
    UPROPERTY(EditAnywhere,Category="Interaction") TArray<FVector> WaypointOffsets;
    UPROPERTY(EditAnywhere,Category="Interaction Camera") FName SourceCameraId;
    bool GetCameraTarget(FVector& Out) const { Out=GetActorTransform().TransformPosition(CameraTarget);return bCameraTargetValid; }
private:
    FVector CameraTarget=FVector::ZeroVector;
    bool bCameraTargetValid=false;
    void UpdateFacts();
    void UpdateItemVisual();
    FLinkInteractionState Participants;
    FLinkStoryContext Context;
    int32 LastVisualValue=INDEX_NONE;
    UPROPERTY() TObjectPtr<class ULinkSession> Session;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UBoxComponent> Selection;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class USphereComponent> Area;
};

#include "Gameplay/LinkPlayerController.h"
#include "Gameplay/LinkCharacter.h"
#include "Gameplay/LinkChain.h"
#include "Gameplay/LinkInteractionActor.h"
#include "Gameplay/LinkSession.h"
#include "Gameplay/LinkAudio.h"
#include "Gameplay/LinkSceneLayout.h"
#include "UI/LinkHUD.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "TimerManager.h"

ALinkPlayerController::ALinkPlayerController()
{
    bShowMouseCursor = true;
    bAutoManageActiveCameraTarget = false;
}

void ALinkPlayerController::BeginPlay()
{
    Super::BeginPlay();
    Tuning = FLinkMotionTuning::Load();
    Commands = FLinkCommands(Tuning.ReleaseGrace);
    Interactions.SetNum(2);
    FLinkSaveSnapshot Restored;
    const bool bRestoring=GetGameInstance()->GetSubsystem<ULinkSession>()->ConsumeSpawnState(UGameplayStatics::GetCurrentLevelName(this),Restored);
    auto* Layout=ALinkSceneLayout::Find(GetWorld());
    ViewTuning = FLinkCameraTuning::Load(Layout && Layout->SourceScene==TEXT("EntryGate") &&
        UGameplayStatics::GetCurrentLevelName(this)==TEXT("EntryGate_Source"));
    if(ViewTuning.bSourceProjection)
    {CameraScene=Layout->SourceScene;ensureMsgf(InteractionCameras.Load(),TEXT("Source interaction camera data failed to load"));}
    for (int32 Index = 0; Index < 2; ++Index)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        FTransform Start(FRotator::ZeroRotator,FVector(0,Index==0?-180:180,110));
        if (Layout) { ensureMsgf(Layout->GetPlayerStart(Index,Start),TEXT("Source scene starting data failed to load")); }
        if (bRestoring && !Restored.bUseSceneStart) { Start=FTransform(FRotator(0,Restored.Yaws[Index],0),Restored.Positions[Index]); }
        ALinkCharacter* CrewMember = GetWorld()->SpawnActor<ALinkCharacter>(Start.GetLocation(),Start.Rotator(),Params);
        Characters.Add(CrewMember);
        if (CrewMember)
        {
            CrewMember->SetIdentity(Index);
            CrewMember->GetCharacterMovement()->RotationRate = FRotator(0, Tuning.RotationSpeed, 0);
            Destinations[Index] = CrewMember->GetNavAgentLocation();
        }
    }
    Chain = GetWorld()->SpawnActor<ALinkChain>();
    if (Chain) { Chain->Initialize(GetCharacterAt(0), GetCharacterAt(1)); }
    if (Chain && Layout && (!bRestoring || Restored.bUseSceneStart)) { Chain->InitializeAuthoredCurve(Layout->GetChainCentres()); }
    SharedCamera = GetWorld()->SpawnActor<ACameraActor>();
    if (SharedCamera)
    {
        SharedCamera->GetCameraComponent()->SetProjectionMode(ECameraProjectionMode::Orthographic);
        SharedCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
        if (ViewTuning.bSourceProjection)
        {
            auto* Camera=SharedCamera->GetCameraComponent();
            Camera->bOverrideAspectRatioAxisConstraint=true;
            Camera->AspectRatioAxisConstraint=AspectRatio_MaintainXFOV;
            Camera->SetAutoCalculateOrthoPlanes(false);
            Camera->SetUpdateOrthoPlanes(false);
            Camera->SetUseCameraHeightAsViewTarget(false);
            Camera->SetOrthoNearClipPlane(ViewTuning.NearClip);
            Camera->SetOrthoFarClipPlane(ViewTuning.FarClip);
        }
        SharedCamera->SetActorRotation(ViewTuning.Rotation());
        UpdateSharedView(0,true);
        SetViewTarget(SharedCamera);
    }
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    if(IsLocalController() && GetWorld()->GetGameViewport())
    {
        SAssignNew(Interface,SLinkHUD).Owner(this);
        GetWorld()->GetGameViewport()->AddViewportWidgetContent(Interface.ToSharedRef(),10);
    }
    if(IsRunCompleted())
    {
        Feedback=TEXT("The route is clear. You found a way into the plant.");
        GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this](){SetPause(true);++ViewRevision;}));
    }
    else if(PlayerCameraManager){PlayerCameraManager->StartCameraFade(1,0,0.45f,FLinearColor::Black,false,false);}
}

void ALinkPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    // Unity's fixed callbacks consume the preceding Update's focus selection.
    if(ViewTuning.bSourceProjection&&!IsPaused()){CameraFocus.Advance(DeltaTime,ViewTuning);}
    UpdateCommands();
    UpdateMovement();
    UpdatePartnerArrival();
    if(ViewTuning.bSourceProjection)
    {
        const int32 Index=Commands.GetFocus();const auto* Selected=GetCharacterAt(Index);
        const auto Current=Index==0?ELinkCameraFocus::Left:Index==1?ELinkCameraFocus::Right:ELinkCameraFocus::None;
        const bool bEligible=Selected&&(Selected->GetMotionMode()==ELinkMotionMode::Navigate||Selected->GetMotionMode()==ELinkMotionMode::Interact);
        const bool bShared=Selected&&GetInteractionFor(Index)&&GetInteractionFor(Index)==GetInteractionFor(1-Index);
        CameraFocus.Focus=ResolveLinkCameraFocus(CameraFocus.Focus,Current,bEligible,bShared,Commands.IsTightFollowing(),ActiveInteraction!=nullptr);
    }
    else {UpdateSharedView(DeltaTime);}
}

void ALinkPlayerController::UpdateCameraManager(float DeltaSeconds)
{
    // This hook runs after character movement, corresponding to source group /
    // Cinemachine LateUpdate. Prototype maps retain their existing tick order.
    if(ViewTuning.bSourceProjection&&!IsPaused()){UpdateSharedView(DeltaSeconds);}
    Super::UpdateCameraManager(DeltaSeconds);
}
void ALinkPlayerController::SetCameraWeight(int32 Percent) { ViewTuning.CameraWeight=FMath::Clamp(Percent,0,100); }
void ALinkPlayerController::PreviewInteractionCamera(FName Id)
{
#if !UE_BUILD_SHIPPING
    if(Id.IsNone()||Id==TEXT("Explore")){ReviewCameraId=NAME_None;return;}
    const auto* Profile=InteractionCameras.Profiles.Find(Id);
    if(ViewTuning.bSourceProjection&&Profile&&Profile->Scene==CameraScene){ReviewCameraId=Id;}
#endif
}

void ALinkPlayerController::UpdateSharedView(float DeltaTime, bool bSnap)
{
    if (SharedCamera && Characters.Num()==2 && Characters[0] && Characters[1])
    {
        int32 Width=0, Height=0;
        GetViewportSize(Width,Height);
        const float Aspect=Height>0 ? float(Width)/Height : 16.f/9.f;
        if(ViewTuning.bSourceProjection)
        {
            ViewState.StepSource(CameraFocus.GroupPosition(Characters[0]->GetActorLocation(),Characters[1]->GetActorLocation()),Aspect,DeltaTime,ViewTuning,bSnap);
        }
        else
        {
            ViewState.Step(Characters[0]->GetActorLocation(),Characters[1]->GetActorLocation(),Characters[0]->GetVelocity(),Characters[1]->GetVelocity(),
                Commands.GetFocus(),Commands.IsTightFollowing(),Aspect,DeltaTime,ViewTuning,bSnap);
        }
        const FVector ExplorePosition=ViewState.Aim-ViewTuning.Rotation().Vector()*ViewTuning.Distance;
        FLinkCameraPose Pose{ExplorePosition,ViewTuning.VerticalHalfHeight};
        if(ViewTuning.bSourceProjection)
        {
            FName Requested=ActiveInteraction?ActiveInteraction->SourceCameraId:ReviewCameraId;
            const auto* Profile=InteractionCameras.Profiles.Find(Requested);
            if(!Profile||Profile->Scene!=CameraScene||Profile->Priority<=InteractionCameras.ExplorationPriority){Requested=TEXT("Explore");}
            else if(ActiveInteraction)
            {
                FVector Target;
                if(ActiveInteraction->GetCameraTarget(Target)){InteractionCameraTargets.Add(Requested,Target);}
                else {Requested=TEXT("Explore");}
            }
            CameraTransition.Step(Requested,DeltaTime,InteractionCameras.BlendSeconds,bSnap);
            Pose=CameraTransition.Evaluate([&](FName Id)
            {
                if(const auto* Camera=InteractionCameras.Profiles.Find(Id))
                {
                    const FVector* Cached=InteractionCameraTargets.Find(Id);
                    return FLinkCameraPose{(Cached?*Cached:Camera->Target)-ViewTuning.Rotation().Vector()*ViewTuning.Distance,Camera->HalfHeight};
                }
                return FLinkCameraPose{ExplorePosition,ViewTuning.VerticalHalfHeight};
            });
        }
        SharedCamera->SetActorLocation(Pose.Position);
        SharedCamera->GetCameraComponent()->SetOrthoWidth(ViewTuning.bSourceProjection?2*Pose.HalfHeight*Aspect:ViewState.Width);
    }
}

void ALinkPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ALinkPlayerController::CommandLeft);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ALinkPlayerController::CommandRight);
    InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&ALinkPlayerController::TogglePauseOrClose).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::M,IE_Pressed,this,&ALinkPlayerController::ToggleAudio).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::F5,IE_Pressed,this,&ALinkPlayerController::QuickSave).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::F9,IE_Pressed,this,&ALinkPlayerController::QuickLoad).bExecuteWhenPaused=true;
}

ALinkCharacter* ALinkPlayerController::GetCharacterAt(int32 Index) const
{
    return Characters.IsValidIndex(Index) ? Characters[Index] : nullptr;
}

bool ALinkPlayerController::CommandMove(int32 Index, const FVector& Target)
{
    ALinkCharacter* CrewMember = GetCharacterAt(Index);
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    FNavLocation Projected;
    if (!CrewMember || !Navigation || Target.ContainsNaN()
        || !Navigation->ProjectPointToNavigation(Target, Projected, FVector(60, 60, 200))) { return false; }
    return CrewMember->MoveTo(Projected.Location, 5.f, Tuning.WalkSpeed, Tuning.Acceleration);
}

bool ALinkPlayerController::NavigateCharacter(int32 Index, const FVector& Target)
{
    if(bTransitioning){return false;}
    auto* CrewMember = GetCharacterAt(Index);
    auto* Other = GetCharacterAt(1 - Index);
    TArray<FVector> Route;
    if (!CrewMember || !Other || !FindRoute(CrewMember, Target, Route)) { return false; }
    LeaveInteraction(Index);
    Destinations[Index] = Route.Last();
    CommandFeedbackTimes[Index]=GetWorld()->GetTimeSeconds();CommandFeedbackLocations[Index]=Route.Last();
    if(auto* Audio=GetWorld()->GetSubsystem<ULinkAudio>()){Audio->Command();}
    CrewMember->SetMotionMode(ELinkMotionMode::Navigate);
    if (Other->GetMotionMode() == ELinkMotionMode::Navigate) { Other->SetMotionMode(ELinkMotionMode::Follow); }
    return true;
}

void ALinkPlayerController::ApplyCommandFrame(const FLinkCommandFrame& Frame, const FVector& Target)
{
    const FLinkCommandActions Actions = Commands.Update(Frame);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        if (Actions.Navigate & (1 << Index)) { NavigateCharacter(Index, Target); }
        if (Actions.Interact & (1 << Index)) { InteractWith(HoveredInteraction,Index); }
        if ((Actions.Follow & (1 << Index)) && GetCharacterAt(Index))
        {
            LeaveInteraction(Index);GetCharacterAt(Index)->SetMotionMode(ELinkMotionMode::Follow);
        }
    }
    const int32 Leader = Commands.GetLeader();
    if (Leader != INDEX_NONE && Frame.Target == ELinkTargetKind::Ground)
    {
        TArray<FVector> Route;
        if (FindRoute(GetCharacterAt(Leader), Target, Route)) { Destinations[Leader] = Route.Last(); }
    }
}

void ALinkPlayerController::UpdateCommands()
{
    if (ActiveInteraction||bTransitioning) { PerformedButtons=0; return; }
    FLinkCommandFrame Frame;
    Frame.Pressed = PerformedButtons;
    PerformedButtons = 0;
    Frame.Held = (IsInputKeyDown(EKeys::LeftMouseButton) ? 1 : 0) | (IsInputKeyDown(EKeys::RightMouseButton) ? 2 : 0);
    Frame.RealTime = GetWorld()->GetRealTimeSeconds();
    float MouseX = 0, MouseY = 0;
    int32 Width = 0, Height = 0;
    GetViewportSize(Width, Height);
    Frame.bInsideViewport = GetMousePosition(MouseX, MouseY) && MouseX >= 0 && MouseY >= 0 && MouseX <= Width && MouseY <= Height;
    FHitResult Hit;
    const bool bHasGround = GetHitResultUnderCursor(ECC_Visibility, false, Hit);
    Frame.Target = bHasGround ? ELinkTargetKind::Ground : ELinkTargetKind::None;
    FHitResult InteractionHit;
    HoveredInteraction=nullptr;
    if (Commands.GetLeader()==INDEX_NONE && GetHitResultUnderCursor(ECC_GameTraceChannel1,false,InteractionHit))
    {
        HoveredInteraction=Cast<ALinkInteractionActor>(InteractionHit.GetActor());
        if (HoveredInteraction) { Frame.Target=ELinkTargetKind::Interaction; }
    }
    ApplyCommandFrame(Frame, bHasGround ? Hit.ImpactPoint : FVector::ZeroVector);
}

bool ALinkPlayerController::FindRoute(ALinkCharacter* From, const FVector& Target, TArray<FVector>& OutCorners) const
{
    if (!From || Target.ContainsNaN()) { return false; }
    UNavigationPath* Route = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), From->GetNavAgentLocation(), Target, From);
    if (!Route || !Route->IsValid() || Route->IsPartial() || Route->PathPoints.IsEmpty()) { return false; }
    OutCorners = Route->PathPoints;
    return true;
}

void ALinkPlayerController::UpdateMovement()
{
    for (int32 Index = 0; Index < 2; ++Index)
    {
        auto* CrewMember = GetCharacterAt(Index);
        auto* Other = GetCharacterAt(1 - Index);
        if (!CrewMember || !Other) { continue; }
        if (CrewMember->GetMotionMode() == ELinkMotionMode::Idle
            && (Other->GetMotionMode() == ELinkMotionMode::Navigate || Other->GetMotionMode() == ELinkMotionMode::Interact))
        {
            CrewMember->SetMotionMode(ELinkMotionMode::Follow);
        }
        if (CrewMember->GetMotionMode() == ELinkMotionMode::Navigate)
        {
            FVector Target = Destinations[Index];
            if (Other->GetMotionMode() != ELinkMotionMode::Follow)
            {
                TArray<FVector> Path;
                if (!FindRoute(Other, Target, Path)) { CrewMember->StopNavigation(); continue; }
                FVector Limited;
                if (LinkNavigation::ClipPath(Path, Tuning.LimitDistance, Limited)) { Target = Limited; }
            }
            CrewMember->MoveTo(Target, 5.f, Tuning.WalkSpeed, Tuning.Acceleration);
        }
        else if (CrewMember->GetMotionMode() == ELinkMotionMode::Follow)
        {
            TArray<FVector> Path;
            if (!FindRoute(CrewMember, Other->GetNavAgentLocation(), Path)) { CrewMember->StopNavigation(); continue; }
            float PathLength = 0;
            for (int32 Point = 1; Point < Path.Num(); ++Point) { PathLength += FVector::Distance(Path[Point-1], Path[Point]); }
            FVector Velocity = Other->GetVelocity();
            Velocity.Z = 0;
            const auto Motion = LinkNavigation::Follow({CrewMember->GetNavAgentLocation(), Other->GetNavAgentLocation(), Velocity,
                Destinations[1-Index], RememberedDirections[Index], PathLength, Commands.IsTightFollowing()}, Tuning);
            RememberedDirections[Index] = Motion.RememberedOtherDirection;
            CrewMember->MoveTo(Motion.Destination, Motion.StopDistance, Motion.Speed, Motion.Acceleration);
        }
        else if (CrewMember->GetMotionMode()==ELinkMotionMode::Interact && GetInteractionFor(Index))
        {
            auto* Target=GetInteractionFor(Index);
            FVector Destination=Target->WaypointFor(Index);
            if (!Target->IsReady(Index) && GetInteractionFor(1-Index) && GetInteractionFor(1-Index)!=Target)
            {
                TArray<FVector> Path; FVector Limited;
                if (!FindRoute(Other,Destination,Path)) { CrewMember->StopNavigation(); continue; }
                if (LinkNavigation::ClipPath(Path,Tuning.LimitDistance,Limited)) { NavigateCharacter(Index,Limited); continue; }
            }
            const float Scale=Target->IsReady(Index)?0.5f:1.f;
            CrewMember->MoveTo(Destination,5.f,Tuning.WalkSpeed*Scale,Tuning.Acceleration*Scale);
        }
    }
}

ALinkInteractionActor* ALinkPlayerController::GetInteractionFor(int32 PlayerIndex) const
{
    return Interactions.IsValidIndex(PlayerIndex)?Interactions[PlayerIndex]:nullptr;
}
void ALinkPlayerController::LeaveInteraction(int32 PlayerIndex)
{
    if (auto* Existing=GetInteractionFor(PlayerIndex)) { Existing->Leave(PlayerIndex); Interactions[PlayerIndex]=nullptr; }
}
bool ALinkPlayerController::InteractWith(ALinkInteractionActor* Target,int32 PlayerIndex)
{
    auto* CrewMember=GetCharacterAt(PlayerIndex);
    if(bTransitioning){return false;}
    if (!Target || Target->GetWorld()!=GetWorld() || !CrewMember) { return false; }
    if (GetInteractionFor(PlayerIndex)==Target && Target->IsReady(PlayerIndex))
    {
        OpenInteraction(Target); Commands.Reset(); return true;
    }
    if (GetInteractionFor(PlayerIndex)!=Target)
    {
        LeaveInteraction(PlayerIndex);
        if (!Target->Focus(PlayerIndex,CrewMember)) { return false; }
        Interactions[PlayerIndex]=Target;
        CommandFeedbackTimes[PlayerIndex]=GetWorld()->GetTimeSeconds();CommandFeedbackLocations[PlayerIndex]=Target->WaypointFor(PlayerIndex);
        if(auto* Audio=GetWorld()->GetSubsystem<ULinkAudio>()){Audio->Command();}
        CrewMember->SetMotionMode(ELinkMotionMode::Interact);
        auto* Other=GetCharacterAt(1-PlayerIndex);
        if (Other && Other->GetMotionMode()==ELinkMotionMode::Navigate) { Other->SetMotionMode(ELinkMotionMode::Follow); }
    }
    return true;
}

void ALinkPlayerController::CommandLeft() { PerformedButtons |= 1; }
void ALinkPlayerController::CommandRight() { PerformedButtons |= 2; }

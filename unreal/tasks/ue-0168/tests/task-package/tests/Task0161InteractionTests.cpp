// Source-derived UE-0161 behavioral verifier. Gameplay authority remains supplied.
#include "AI/RTSAIStrategySubsystem.h"
#include "Camera/RTSCameraPawn.h"
#include "Economy/RTSEconomySubsystem.h"
#include "EnhancedInputComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/RTSPlayerController.h"
#include "InputAction.h"
#include "Match/RTSMatchSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Presentation/RTSWorldOverlaySubsystem.h"
#include "Selection/RTSSelectionSubsystem.h"
#include "Structures/RTSDeploymentViewSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UI/RTSHUDSnapshot.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace Task0161Interaction
{
UWorld* PIE()
{
    for (const FWorldContext& C : GEngine->GetWorldContexts())
        if (C.WorldType == EWorldType::PIE) return C.World();
    return nullptr;
}

ARTSCombatUnit* Spawn(UWorld& W, int32 Id, FGenericTeamId Team, FVector Position)
{
    const FTransform Transform(FRotator::ZeroRotator, Position);
    ARTSCombatUnit* U = W.SpawnActorDeferred<ARTSCombatUnit>(ARTSCombatUnit::StaticClass(), Transform,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (U) { U->ConfigureMilestone2Unit(ERTSUnitType::InfantrySquad, Id, Team); U->FinishSpawning(Transform); }
    return U;
}

class FScenario final : public IAutomationLatentCommand
{
public:
    FScenario(FAutomationTestBase* InTest, int32 InCase) : Test(InTest), Case(InCase), Deadline(FPlatformTime::Seconds()+45.0) {}
    bool Update() override
    {
        if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("Incomplete interface fixture or viewport")); return true; }
        UWorld* W = PIE();
        if (!W) return false;
        auto* PC = Cast<ARTSPlayerController>(W->GetFirstPlayerController());
        if (!PC || !PC->GetLocalPlayer()) return false;
        auto* Selection = PC->GetLocalPlayer()->GetSubsystem<URTSSelectionSubsystem>();
        auto* View = PC->GetLocalPlayer()->GetSubsystem<URTSDeploymentViewSubsystem>();
        auto* Match = W->GetSubsystem<URTSMatchSubsystem>();
        auto* Overlays = W->GetSubsystem<URTSWorldOverlaySubsystem>();
        auto* Structures = W->GetSubsystem<URTSStructureSubsystem>();
        auto* Commands = W->GetSubsystem<URTSCommandSubsystem>();
        auto* Camera = Cast<ARTSCameraPawn>(PC->GetPawn());
        if (!Selection || !View || !Match || !Overlays || !Structures || !Commands || !Camera) return false;
        if (auto* AI = W->GetSubsystem<URTSAIStrategySubsystem>()) AI->StopFaction(RTSTeams::Enemy);
        auto* CV = Match->GetCommandVehicle(RTSTeams::Player);
        if (!Test->TestNotNull(TEXT("A real friendly Command Vehicle exists"), CV)) return true;
        Camera->SetKeyboardPanInput(FVector2D::ZeroVector);
        Camera->SetEdgePanInput(FVector2D::ZeroVector);
        if (Case == 1) return VerifyPreview(*W, *PC, *View, *CV, *Structures, *Overlays, *Match);
        if (Case == 2) return VerifyWarnings(*W, *PC, *View);
        if (!First.IsValid())
        {
            auto* Input=Cast<UEnhancedInputComponent>(PC->InputComponent);
            if (!Test->TestNotNull(TEXT("Production Enhanced Input bindings exist"),Input)) return true;
            auto Fire=[&](const TCHAR* ActionName)
            {
                for (const auto& Binding : Input->GetActionEventBindings())
                {
                    const UInputAction* Action=Binding->GetAction();
                    if (Action && Action->GetFName()==FName(ActionName) && Binding->GetTriggerEvent()==ETriggerEvent::Started)
                    {
                        Binding->Execute(FInputActionInstance(Action));
                        return true;
                    }
                }
                Test->AddError(FString::Printf(TEXT("Supplied action binding missing: %s"),ActionName));
                return false;
            };
            TArray<ARTSCombatUnit*> CommandSelection{CV}; Selection->ReplaceWith(CommandSelection);
            Fire(TEXT("RTSDeployHeadquarters"));
            Test->TestTrue(TEXT("Bound deployment action starts selected Command Vehicle preview"),View->GetCommandVehicle()==CV);
            PC->PlayerTick(0);
            Fire(TEXT("RTSCancel"));
            Test->TestFalse(TEXT("Bound cancel action clears HQ preview"),View->IsPreviewActive());
            const TCHAR* PlacementActions[]={TEXT("RTSPlaceExtractor"),TEXT("RTSPlaceGenerator"),TEXT("RTSPlaceFactory"),TEXT("RTSPlaceSupplyDepot"),TEXT("RTSPlaceTurret")};
            const ERTSStructureType PlacementTypes[]={ERTSStructureType::MaterialExtractor,ERTSStructureType::PowerGenerator,ERTSStructureType::Factory,ERTSStructureType::SupplyDepot,ERTSStructureType::DefensiveTurret};
            for (int32 I=0;I<5;++I)
            {
                Fire(PlacementActions[I]);
                const auto Request=View->GetPlacementRequest();
                Test->TestTrue(TEXT("Bound placement action retains requested type and owner"),Request.IsSet() && Request->StructureType==PlacementTypes[I] && Request->TeamId==RTSTeams::Player);
                Fire(TEXT("RTSCancel"));
                Test->TestFalse(TEXT("Bound cancel clears structure preview"),View->IsStructurePreviewActive());
            }
            Selection->Clear();
            Camera->SetActorLocation(CV->GetActorLocation());
            First = Spawn(*W, 96101, RTSTeams::Player, CV->GetActorLocation()+FVector(500,0,100));
            Second = Spawn(*W, 96102, RTSTeams::Player, CV->GetActorLocation()+FVector(-500,0,100));
            Enemy = Spawn(*W, 96103, RTSTeams::Enemy, CV->GetActorLocation()+FVector(0,650,100));
            if (!First.IsValid() || !Second.IsValid() || !Enemy.IsValid()) { Test->AddError(TEXT("Failed to spawn routing units")); return true; }
            ReadyAt = FPlatformTime::Seconds()+0.5;
            return false;
        }
        if (FPlatformTime::Seconds() < ReadyAt) return false;
        FVector2D A, B, E;
        if (!PC->ProjectWorldLocationToScreen(First->GetStatusAnchorWorldLocation(), A)
            || !PC->ProjectWorldLocationToScreen(Second->GetStatusAnchorWorldLocation(), B)
            || !PC->ProjectWorldLocationToScreen(Enemy->GetStatusAnchorWorldLocation(), E)) return false;
        int32 Width=0, Height=0; PC->GetViewportSize(Width,Height);
        if (Width<=0 || Height<=0) return false;
        Selection->Clear();
        PC->SelectAtScreenPosition(A,false);
        Test->TestTrue(TEXT("Screen click selects the intended live friendly unit"), Selection->GetLivingUnits().Contains(First.Get()));
        PC->SelectAtScreenPosition(B,true);
        Test->TestEqual(TEXT("Toggle adds a second unit"), Selection->GetLivingUnits().Num(),2);
        PC->SelectAtScreenPosition(A,true);
        Test->TestTrue(TEXT("Toggle removes the first unit"), !Selection->GetLivingUnits().Contains(First.Get()) && Selection->GetLivingUnits().Contains(Second.Get()));
        PC->SelectInsideScreenRect(FBox2D(A+FVector2D(80,80),A-FVector2D(80,80)),false);
        Test->TestTrue(TEXT("Reversed marquee corners are normalized"), Selection->GetLivingUnits().Contains(First.Get()));
        View->BeginHeadquartersPreview(*CV);
        int32 Accepted=0;
        const FDelegateHandle Handle=Commands->OnCommandAccepted().AddLambda([&Accepted](const FRTSCommandResult&){++Accepted;});
        PC->IssueContextCommandAtScreenPosition(E);
        Commands->OnCommandAccepted().Remove(Handle);
        Test->TestEqual(TEXT("One context gesture dispatches one authoritative group command"), Accepted,1);
        Test->TestFalse(TEXT("Context commands cancel the pending preview"),View->IsPreviewActive());
        Test->TestEqual(TEXT("Screen enemy routes to Attack"),First->GetOrderComponent()->GetSnapshot().Kind,ERTSOrderKind::Attack);
        Test->TestTrue(TEXT("Attack retains clicked target identity"),First->GetOrderComponent()->GetSnapshot().Target==Enemy.Get());
        Selection->Clear();
        const auto Before=First->GetOrderComponent()->GetSnapshot().GroupCommandId;
        PC->IssueContextCommandAtScreenPosition(E);
        Test->TestEqual(TEXT("Empty selection does not issue a new command"),First->GetOrderComponent()->GetSnapshot().GroupCommandId,Before);
        return true;
    }
private:
    bool VerifyWarnings(UWorld& W, ARTSPlayerController& PC, URTSDeploymentViewSubsystem& View)
    {
        auto* Economy=W.GetSubsystem<URTSEconomySubsystem>();
        if (!Test->TestNotNull(TEXT("Economy fixture exists"),Economy)) return true;
        FRTSEconomyDelta D; D.PowerDemand=Economy->GetSnapshot(RTSTeams::Player).PowerGeneration+27;
        const int64 Tx=Economy->AllocateTransactionId();
        Test->TestTrue(TEXT("Fixture changes authoritative demand"),Economy->TryCommit(Tx,RTSTeams::Player,D).bAccepted);
        View.BeginStructurePreview(ERTSStructureType::Factory,RTSTeams::Player);
        View.UpdateStructurePreviewLocation(FVector(999999,999999,0));
        const auto Before=Economy->GetSnapshot(RTSTeams::Player);
        const auto S=FRTSHUDSnapshotAdapter::Capture(&PC);
        Test->TestEqual(TEXT("Brownout is a critical warning"),S.WarningTone,ERTSHUDMessageTone::Critical);
        Test->TestTrue(TEXT("Demand deficit is exposed"),S.WarningText.Contains(TEXT("27")));
        Test->TestEqual(TEXT("Invalid preview has critical context"),S.ContextTone,ERTSHUDMessageTone::Critical);
        Test->TestFalse(TEXT("Refusal context remains informative"),S.ContextText.IsEmpty());
        Test->TestEqual(TEXT("Rendering snapshots never spend Materials"),Economy->GetSnapshot(RTSTeams::Player).Materials,Before.Materials);
        Economy->TryRollback(Tx); View.CancelPreview();
        const auto Clear=FRTSHUDSnapshotAdapter::Capture(&PC);
        Test->TestTrue(TEXT("Clearing demand removes the stale brownout warning"),!Clear.WarningText.Contains(TEXT("LOW POWER")));
        Test->TestTrue(TEXT("Null controller has unavailable fallback"),!FRTSHUDSnapshotAdapter::Capture(nullptr).bAvailable);
        return true;
    }
    bool VerifyPreview(UWorld& W, ARTSPlayerController& PC, URTSDeploymentViewSubsystem& View,
        ARTSCombatUnit& CV, URTSStructureSubsystem& Structures, URTSWorldOverlaySubsystem& Overlays, URTSMatchSubsystem& Match)
    {
        View.BeginHeadquartersPreview(CV); PC.PlayerTick(0);
        auto HasSource=[&](ERTSWorldOverlaySourceKind Kind) { return Overlays.GetSnapshots().ContainsByPredicate([Kind](const FRTSWorldOverlaySnapshot& S){return S.Descriptor.SourceKind==Kind;}); };
        Test->TestTrue(TEXT("HQ preview projects typed footprint and build area"),HasSource(ERTSWorldOverlaySourceKind::DeploymentPreview));
        View.CancelPreview(); PC.PlayerTick(0);
        Test->TestFalse(TEXT("Canceled previews leave no world overlay"),HasSource(ERTSWorldOverlaySourceKind::DeploymentPreview));
        Test->TestTrue(TEXT("Real deployment succeeds"),Structures.TryDeployHeadquarters(CV).bAccepted);
        Test->TestTrue(TEXT("Structure events create a persistent build area"),HasSource(ERTSWorldOverlaySourceKind::Structure));
        FRTSWorldOverlayDescriptor D; D.Mode=ERTSWorldOverlayMode::BuildArea;
        D.SourceKind=ERTSWorldOverlaySourceKind::Showcase; D.StableSourceId=961; D.Radius=300; D.TeamId=RTSTeams::Player;
        Test->TestTrue(TEXT("Independent showcase accepted"),Overlays.Submit(D).bAccepted);
        auto* EnemyCV=Match.GetCommandVehicle(RTSTeams::Enemy);
        if (!Test->TestNotNull(TEXT("Enemy command identity exists"),EnemyCV)) return true;
        Test->TestTrue(TEXT("Match resolves through authority"),Match.ReportCommandVehicleDestroyed(*EnemyCV).bAccepted);
        Test->TestFalse(TEXT("Resolved match clears gameplay overlays"),HasSource(ERTSWorldOverlaySourceKind::Structure));
        Test->TestTrue(TEXT("Resolved match preserves explicitly independent showcase"),HasSource(ERTSWorldOverlaySourceKind::Showcase));
        return true;
    }
    FAutomationTestBase* Test; int32 Case; double Deadline; double ReadyAt=0;
    TWeakObjectPtr<ARTSCombatUnit> First,Second,Enemy;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0161Routing,"Task0168.Rendered.Task0161.Input.ScreenRouting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FTask0161Routing::RunTest(const FString&)
{
    if (!AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"),true)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(Task0161Interaction::FScenario(this,0));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0161Preview,"Task0168.Headless.Task0161.Overlay.PreviewLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FTask0161Preview::RunTest(const FString&)
{
    if (!AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"),true)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(Task0161Interaction::FScenario(this,1));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0161Warnings,"Task0168.Headless.Task0161.HUD.WarningPrecedence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FTask0161Warnings::RunTest(const FString&)
{
    if (!AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"),true)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(Task0161Interaction::FScenario(this,2));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand()); return true;
}
#endif

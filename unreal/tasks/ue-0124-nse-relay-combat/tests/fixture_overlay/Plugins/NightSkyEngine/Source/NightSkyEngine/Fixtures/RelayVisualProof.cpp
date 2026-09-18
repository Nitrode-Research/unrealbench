#include "RelayVisualProof.h"
#include "LoadingScreenManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HighResScreenshot.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Components/InputComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"

void ARelayVisualProofGameMode::BeginPlay()
{
    Super::BeginPlay();
    FParse::Value(FCommandLine::Get(), TEXT("RelayVisualProof="), ProofDirectory);
    if (!ProofDirectory.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*ProofDirectory, true);
        if (auto Battle = GetGameState<ARelayFixtureBattle>())
        {
            Battle->bPauseGame = true;
        }
    }
}

namespace
{
    struct FRelayProofInputs
    {
        int32 FirstTeam;
        int32 SecondTeam;
    };

    FRelayProofInputs PrepareProofFrame(ARelayFixtureBattle *Battle, int32 Frame)
    {
        if (Frame == 0)
        {
            for (auto Fighter : Battle->Players)
            {
                Fighter->PosX = Fighter->PlayerIndex == 0 ? -600000 : 600000;
            }
        }
        int32 FirstTeamInput = Frame == 0                   ? RelaySlot2
                               : Frame == 18 || Frame == 48 ? RelaySlot3
                               : Frame == 50 || Frame == 80 ? INP_ResetTraining
                               : Frame == 81                ? RelaySlot2
                                                            : 0;
        int32 SecondTeamInput = 0;
        if (Frame == 51)
        {
            for (auto Fighter : Battle->Players)
            {
                Fighter->PosX = Fighter->PlayerIndex == 0 ? -150000 : 150000;
            }
            Battle->Players[0]->SetHealth(1);
            Battle->Players[1]->SetHealth(1);
            FirstTeamInput = RelaySlot2;
            SecondTeamInput = INP_A;
        }
        if (Frame == 82)
        {
            Battle->Players[0]->PosX = -600000;
            Battle->Players[1]->PosX = -150000;
            Battle->Players[3]->PosX = 150000;
        }
        if (Frame == 83)
        {
            SecondTeamInput = INP_B;
        }
        if (Frame == 101)
        {
            FirstTeamInput = INP_ResetTraining;
        }
        // A selected generated UI episode uses the same published LCG in the driver.
        uint32 RandomState = 39009;
        auto Next = [&]() {
            RandomState = RandomState * 1664525u + 1013904223u;
            return RandomState;
        };
        const int TargetSlot = 1 + int(Next() % 2), RouteSlot = 3 - TargetSlot;
        const int TeamSpacing = 600000 + int(Next() % 300001);
        if (Frame == 102)
        {
            for (auto Fighter : Battle->Players)
            {
                Fighter->SetHealth(2000 + int(Next() % 1001));
                Fighter->SetRecoverableHealth(0);
                Fighter->PosX = Fighter->PlayerIndex == 0 ? -TeamSpacing : TeamSpacing;
            }
            FirstTeamInput = RelaySlot1 << TargetSlot;
        }
        if (Frame == 120)
        {
            FirstTeamInput = RelaySlot1 << RouteSlot;
        }

        return {FirstTeamInput, SecondTeamInput};
    }
} // namespace

namespace
{
    struct FRelayProjectionObservation
    {
        FVector2D Pixel;
        bool Projected = false;
        bool BoundsInside = true;
        float BodyMinX = 0;
        float BodyMaxX = 0;
        float BodyMinY = 0;
        float BodyMaxY = 0;
    };

    FRelayProjectionObservation ObserveFighterProjection(APlayerController *Controller,
                                                         APlayerObject *Fighter, int32 Width,
                                                         int32 Height)
    {
        FRelayProjectionObservation Observation;
        Observation.Projected = Controller->ProjectWorldLocationToScreen(
            Fighter->GetActorLocation(), Observation.Pixel);

        const FBox Bounds = Fighter->GetComponentsBoundingBox(true);
        for (int Corner = 0; Corner < 8; ++Corner)
        {
            FVector Point((Corner & 1) ? Bounds.Max.X : Bounds.Min.X,
                          (Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y,
                          (Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z);
            FVector2D ProjectedCorner;
            Observation.BoundsInside &=
                Controller->ProjectWorldLocationToScreen(Point, ProjectedCorner) &&
                ProjectedCorner.X >= 0 && ProjectedCorner.X < Width && ProjectedCorner.Y >= 0 &&
                ProjectedCorner.Y < Height;
        }
        Observation.BodyMinX = Width;
        Observation.BodyMinY = Height;
        if (auto Body = Fighter->FindComponentByClass<UStaticMeshComponent>())
        {
            const FBox Box = Body->Bounds.GetBox();
            for (int Corner = 0; Corner < 8; ++Corner)
            {
                FVector Point((Corner & 1) ? Box.Max.X : Box.Min.X,
                              (Corner & 2) ? Box.Max.Y : Box.Min.Y,
                              (Corner & 4) ? Box.Max.Z : Box.Min.Z);
                FVector2D ProjectedBody;
                if (Controller->ProjectWorldLocationToScreen(Point, ProjectedBody))
                {
                    Observation.BodyMinX = FMath::Min(Observation.BodyMinX, float(ProjectedBody.X));
                    Observation.BodyMaxX = FMath::Max(Observation.BodyMaxX, float(ProjectedBody.X));
                    Observation.BodyMinY = FMath::Min(Observation.BodyMinY, float(ProjectedBody.Y));
                    Observation.BodyMaxY = FMath::Max(Observation.BodyMaxY, float(ProjectedBody.Y));
                }
            }
        }
        return Observation;
    }

    void CaptureProofFrame(UWorld *World, ARelayFixtureBattle *Battle,
                           const FString &ProofDirectory, int32 ProofFrame)
    {
        FString Ledger =
            TEXT("team,slot,identity,main,visible,health,recoverable,cooldown,resource,x,y,width,"
                 "height,projected,bounds_inside,eligible,rejection,throw_locked,pos_y,object_reg1,"
                 "throw_invulnerable,throw_resist,player_flags,attack_flags,direction,left,right,"
                 "top,bottom,invuln,body_min_x,body_max_x,body_min_y,body_max_y\n");
        auto Controller = World->GetFirstPlayerController();
        int32 Width = 0, Height = 0;
        Controller->GetViewportSize(Width, Height);
        for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
        {
            const auto Views = Battle->GetRelaySlots(TeamIndex == 0);
            for (int SlotIndex = 0; SlotIndex < Views.Num(); ++SlotIndex)
            {
                const auto &SlotView = Views[SlotIndex];
                const FRelayProjectionObservation Projection = ObserveFighterProjection(
                    Controller, Battle->Players[TeamIndex * 3 + SlotIndex], Width, Height);
                Ledger += FString::Printf(
                    TEXT("%d,%d,%s,%d,%d,%d,%d,%d,%d,%f,%f,%d,%d,%d,%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%"
                         "d,%d,%d,%d,%d,%d\n"),
                    TeamIndex, SlotView.Slot, *SlotView.Identity, SlotView.Main, SlotView.Visible,
                    SlotView.Health, SlotView.Recoverable, SlotView.Cooldown,
                    Battle->GetRelayStatus(TeamIndex == 0).Resource, Projection.Pixel.X,
                    Projection.Pixel.Y, Width, Height, Projection.Projected,
                    Projection.BoundsInside, SlotView.Eligible,
                    *StaticEnum<ERelayRejection>()->GetNameStringByValue(
                        int64(Battle->GetRelayStatus(TeamIndex == 0).Rejection)),
                    (Battle->Players[TeamIndex * 3 + SlotIndex]->PlayerFlags & PLF_IsThrowLock) !=
                        0,
                    Battle->Players[TeamIndex * 3 + SlotIndex]->PosY,
                    Battle->Players[TeamIndex * 3 + SlotIndex]->ObjectReg1,
                    Battle->Players[TeamIndex * 3 + SlotIndex]->ThrowInvulnerableTimer,
                    Battle->Players[TeamIndex * 3 + SlotIndex]->ThrowResistTimer,
                    int32(Battle->Players[TeamIndex * 3 + SlotIndex]->PlayerFlags),
                    int32(Battle->Players[TeamIndex * 3 + SlotIndex]->AttackFlags),
                    int32(Battle->Players[TeamIndex * 3 + SlotIndex]->Direction),
                    Battle->Players[TeamIndex * 3 + SlotIndex]->L,
                    Battle->Players[TeamIndex * 3 + SlotIndex]->R,
                    Battle->Players[TeamIndex * 3 + SlotIndex]->T,
                    Battle->Players[TeamIndex * 3 + SlotIndex]->B,
                    int32(Battle->Players[TeamIndex * 3 + SlotIndex]->InvulnFlags));
                Ledger.RemoveAt(Ledger.Len() - 1);
                Ledger +=
                    FString::Printf(TEXT(",%f,%f,%f,%f\n"), Projection.BodyMinX,
                                    Projection.BodyMaxX, Projection.BodyMinY, Projection.BodyMaxY);
            }
        }
        FFileHelper::SaveStringToFile(
            Ledger, *(ProofDirectory / FString::Printf(TEXT("frame-%02d.csv"), ProofFrame)));
        // Optional generic text evidence. Custom paint, images, bars, and other
        // widget classes remain valid and use the independent screenshot review.
        TArray<UUserWidget *> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UUserWidget::StaticClass(),
                                                      false);
        TSet<UTextBlock *> Seen;
        TArray<FString> VisibleText;
        for (auto Widget : Widgets)
        {
            if (!Widget || !Widget->IsVisible() || !Widget->WidgetTree)
                continue;
            TArray<UWidget *> Children;
            Widget->WidgetTree->GetAllWidgets(Children);
            for (auto Child : Children)
            {
                auto Text = Cast<UTextBlock>(Child);
                if (Text && !Seen.Contains(Text) && Text->IsVisible() &&
                    Text->GetCachedGeometry().GetLocalSize().X > 0 &&
                    Text->GetCachedGeometry().GetLocalSize().Y > 0)
                {
                    Seen.Add(Text);
                    VisibleText.Add(Text->GetText().ToString());
                }
            }
        }
        if (!VisibleText.IsEmpty())
        {
            const FString Evidence = FString::Printf(TEXT("%d,%d\n"),
                Battle->BattleState.FrameNumber, Battle->BattleState.FrameNumber) +
                FString::Join(VisibleText, TEXT("\n===\n"));
            FFileHelper::SaveStringToFile(Evidence,
                *(ProofDirectory / FString::Printf(TEXT("frame-%02d-hud.txt"), ProofFrame)));
        }
        FScreenshotRequest::RequestScreenshot(
            ProofDirectory / FString::Printf(TEXT("frame-%02d.png"), ProofFrame), true, false);
    }
} // namespace

void ARelayVisualProofGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (ProofDirectory.IsEmpty())
    {
        return;
    }
    auto Battle = GetGameState<ARelayFixtureBattle>();
    if (!Battle || Battle->Players.Num() != 6)
    {
        return;
    }
    Battle->bPauseGame = true;
    if (auto Loading = GetGameInstance()->GetSubsystem<ULoadingScreenManager>();
        Loading && Loading->GetLoadingScreenDisplayStatus())
    {
        return;
    }
    if (ProofWarmup-- > 0)
    {
        return;
    }
    if (ProofWait-- > 0)
    {
        return;
    }
    if (ProofFrame > 139)
    {
        GetGameInstance<UNightSkyGameInstance>()->IsReplay = true;
        FPlatformMisc::RequestExit(false);
        return;
    }
    const bool Capture =
        ProofFrame == 0 || ProofFrame == 6 || ProofFrame == 18 || ProofFrame == 19 ||
        ProofFrame == 31 || ProofFrame == 37 || ProofFrame == 48 || ProofFrame == 51 ||
        ProofFrame == 54 || ProofFrame == 60 || ProofFrame == 83 || ProofFrame == 90 ||
        ProofFrame == 100 || ProofFrame == 102 || ProofFrame == 108 || ProofFrame == 120 ||
        ProofFrame == 121 || ProofFrame == 133 || ProofFrame == 139;
    if (!ProofPending)
    {
        const FRelayProofInputs Inputs = PrepareProofFrame(Battle, ProofFrame);
        Battle->UpdateGameState(Inputs.FirstTeam, Inputs.SecondTeam, false);
        // PlayerCameraManager caches its view later in the world tick. Keep gameplay
        // paused while that view and rendered component transforms settle before
        // sampling public projection values and requesting the matching screenshot.
        if (Capture)
        {
            ProofPending = true;
            ProofWait = 2;
            return;
        }
    }
    if (Capture)
    {
        CaptureProofFrame(GetWorld(), Battle, ProofDirectory, ProofFrame);
        ProofPending = false;
        ProofWait = 4;
    }
    ++ProofFrame;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/RTSAIStrategySubsystem.h"
#include "Camera/RTSCameraPawn.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Presentation/RTSWorldOverlayShowcaseActor.h"
#include "Presentation/RTSWorldOverlaySubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Units/RTSTeams.h"
#include "UnrealClient.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RTSMilestone3OverlayTestPrivate
{
UWorld* FindPIEWorld()
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::PIE)
		{
			return WorldContext.World();
		}
	}
	return nullptr;
}

FRTSWorldOverlayDescriptor MakeCircle(
	const ERTSWorldOverlayMode Mode,
	const int32 StableSourceId,
	const float Radius)
{
	FRTSWorldOverlayDescriptor Descriptor;
	Descriptor.Mode = Mode;
	Descriptor.SourceKind = ERTSWorldOverlaySourceKind::Showcase;
	Descriptor.StableSourceId = StableSourceId;
	Descriptor.Radius = Radius;
	Descriptor.TeamId = RTSTeams::Player;
	return Descriptor;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3OverlayDescriptorTest,
	"Task0169.Headless.Task0161.Overlay.Descriptors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone3OverlayDescriptorTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (World != nullptr && GEngine != nullptr)
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	}
	URTSWorldOverlaySubsystem* Overlays = World != nullptr
		? World->GetSubsystem<URTSWorldOverlaySubsystem>()
		: nullptr;
	if (!TestNotNull(TEXT("A game world owns the overlay renderer"), Overlays))
	{
		if (World != nullptr)
		{
			World->DestroyWorld(false);
		}
		return false;
	}

	const FRTSMilestone2Configuration Configuration = FRTSMilestone2Configuration::Load();
	const FRTSWorldOverlaySubmission BuildArea = Overlays->Submit(
		RTSMilestone3OverlayTestPrivate::MakeCircle(
			ERTSWorldOverlayMode::BuildArea,
			1,
			Configuration.Headquarters.BuildAreaRadius));
	TestTrue(TEXT("The configured HQ radius is accepted"), BuildArea.bAccepted);

	FRTSWorldOverlayDescriptor ValidFootprint;
	ValidFootprint.Mode = ERTSWorldOverlayMode::PlacementFootprint;
	ValidFootprint.SourceKind = ERTSWorldOverlaySourceKind::Showcase;
	ValidFootprint.StableSourceId = 2;
	ValidFootprint.HalfExtent = FVector2D(300.0f, 200.0f);
	ValidFootprint.Validity = ERTSWorldOverlayValidity::Valid;
	ValidFootprint.TeamId = RTSTeams::Player;
	const FRTSWorldOverlaySubmission Valid = Overlays->Submit(ValidFootprint);
	ValidFootprint.StableSourceId = 3;
	ValidFootprint.Validity = ERTSWorldOverlayValidity::Invalid;
	const FRTSWorldOverlaySubmission Invalid = Overlays->Submit(ValidFootprint);
	TestTrue(TEXT("A valid placement footprint is accepted"), Valid.bAccepted);
	TestTrue(TEXT("An invalid placement footprint is accepted"), Invalid.bAccepted);

	const FRTSWorldOverlaySubmission FirstTurret = Overlays->Submit(
		RTSMilestone3OverlayTestPrivate::MakeCircle(
			ERTSWorldOverlayMode::TurretRange,
			4,
			Configuration.DefensiveTurret.AttackRange));
	FRTSWorldOverlayDescriptor AlternateTurret = RTSMilestone3OverlayTestPrivate::MakeCircle(
		ERTSWorldOverlayMode::TurretRange,
		5,
		Configuration.DefensiveTurret.AttackRange * 1.35f);
	const FRTSWorldOverlaySubmission SecondTurret = Overlays->Submit(AlternateTurret);
	TestTrue(TEXT("The first configured turret radius is accepted"), FirstTurret.bAccepted);
	TestTrue(TEXT("A different turret radius uses the same typed mode"), SecondTurret.bAccepted);
	TestEqual(TEXT("All required descriptor instances are live"), Overlays->GetSnapshots().Num(), 5);

	AlternateTurret.Radius = Configuration.DefensiveTurret.AttackRange * 0.8f;
	const FRTSWorldOverlaySubmission UpdatedTurret = Overlays->Submit(AlternateTurret);
	TestEqual(
		TEXT("A tuning update replaces the existing renderer instance"),
		UpdatedTurret.Handle.Value,
		SecondTurret.Handle.Value);
	const TArray<FRTSWorldOverlaySnapshot> UpdatedSnapshots = Overlays->GetSnapshots();
	const FRTSWorldOverlaySnapshot* UpdatedSnapshot = UpdatedSnapshots.FindByPredicate(
		[UpdatedTurret](const FRTSWorldOverlaySnapshot& Snapshot)
		{
			return Snapshot.Handle == UpdatedTurret.Handle;
		});
	TestTrue(
		TEXT("The live descriptor carries the changed radius without a new Material"),
		UpdatedSnapshot != nullptr
			&& FMath::IsNearlyEqual(UpdatedSnapshot->Descriptor.Radius, AlternateTurret.Radius));

	TestTrue(TEXT("Explicit owner removal succeeds"), Overlays->Remove(Invalid.Handle));
	TestEqual(TEXT("Removed overlays leave no stale snapshot"), Overlays->GetSnapshots().Num(), 4);
	TestEqual(
		TEXT("Source expiry removes every mode owned by that source"),
		Overlays->RemoveSource(ERTSWorldOverlaySourceKind::Showcase, 5),
		1);
	TestEqual(TEXT("Expired source state is absent"), Overlays->GetSnapshots().Num(), 3);

	World->DestroyWorld(false);
	if (GEngine != nullptr)
	{
		GEngine->DestroyWorldContext(World);
	}
	return !HasAnyErrors();
}

class FRTSCaptureOverlayShowcaseCommand final : public IAutomationLatentCommand
{
public:
	FRTSCaptureOverlayShowcaseCommand(FAutomationTestBase* InTest, const double InDeadline)
		: Test(InTest), Deadline(InDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = RTSMilestone3OverlayTestPrivate::FindPIEWorld();
		if (World == nullptr)
		{
			return FinishIfTimedOut(TEXT("PIE did not expose the overlay showcase world."));
		}
		if (!bInitialized && !Initialize(*World))
		{
			return FinishIfTimedOut(TEXT("The rendered overlay showcase is incomplete."));
		}
		PinCamera();
		if (bCapturePending)
		{
			if (IFileManager::Get().FileSize(*PendingCapturePath) <= 0)
			{
				return FinishIfTimedOut(TEXT("A rendered overlay screenshot was not written."));
			}
			bCapturePending = false;
			Stage = StageAfterCapture;
			if (Stage == ECaptureStage::ExtremeZoom)
			{
				Camera->AddZoomInput(-100.0f);
			}
			else if (Stage == ECaptureStage::ConfiguredRadius && !ApplyConfiguredRadius())
			{
				return true;
			}
			StageReadyAt = FPlatformTime::Seconds() + 0.8;
		}
		if (FPlatformTime::Seconds() < StageReadyAt)
		{
			return false;
		}

		switch (Stage)
		{
		case ECaptureStage::Normal:
			RequestCapture(TEXT("M3Slice6-Overlay-Normal.png"), ECaptureStage::ExtremeZoom);
			break;
		case ECaptureStage::ExtremeZoom:
			RequestCapture(TEXT("M3Slice6-Overlay-ExtremeZoom.png"), ECaptureStage::ConfiguredRadius);
			break;
		case ECaptureStage::ConfiguredRadius:
			RequestCapture(TEXT("M3Slice6-Overlay-ConfiguredRadius.png"), ECaptureStage::Complete);
			break;
		case ECaptureStage::Complete:
			Test->AddInfo(TEXT("Rendered overlay captures: normal, extreme zoom, and configured-radius update."));
			return true;
		}
		return false;
	}

private:
	enum class ECaptureStage : uint8
	{
		Normal,
		ExtremeZoom,
		ConfiguredRadius,
		Complete
	};

	bool Initialize(UWorld& World)
	{
		APlayerController* Controller = World.GetFirstPlayerController();
		PlayerController = Controller;
		Camera = Controller != nullptr ? Cast<ARTSCameraPawn>(Controller->GetPawn()) : nullptr;
		Overlays = World.GetSubsystem<URTSWorldOverlaySubsystem>();
		TActorIterator<ARTSWorldOverlayShowcaseActor> Iterator(&World);
		if (Iterator)
		{
			Showcase = *Iterator;
		}
		if (!Camera.IsValid() || Overlays == nullptr || !Showcase.IsValid())
		{
			return false;
		}
		if (URTSAIStrategySubsystem* Strategy = World.GetSubsystem<URTSAIStrategySubsystem>())
		{
			Strategy->StopFaction(RTSTeams::Enemy);
		}
		Showcase->ActivateShowcase();
		CameraLocation = Showcase->GetActorLocation();
		CameraLocation.Z = 100.0f;
		Camera->AddZoomInput(100.0f);
		StageReadyAt = FPlatformTime::Seconds() + 1.5;
		bInitialized = true;
		return true;
	}

	void PinCamera()
	{
		if (Camera.IsValid())
		{
			Camera->SetKeyboardPanInput(FVector2D::ZeroVector);
			Camera->SetEdgePanInput(FVector2D::ZeroVector);
			Camera->SetActorLocation(CameraLocation);
			if (!bCameraCentered && PlayerController.IsValid() && Showcase.IsValid())
			{
				int32 ViewportWidth = 0;
				int32 ViewportHeight = 0;
				PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
				FVector RayOrigin;
				FVector RayDirection;
				if (ViewportWidth > 0
					&& ViewportHeight > 0
					&& PlayerController->DeprojectScreenPositionToWorld(
						ViewportWidth * 0.5f,
						ViewportHeight * 0.5f,
						RayOrigin,
						RayDirection)
					&& RayDirection.Z < -KINDA_SMALL_NUMBER)
				{
					const FVector GroundCenter = RayOrigin + RayDirection * (-RayOrigin.Z / RayDirection.Z);
					const FVector Adjustment = Showcase->GetActorLocation() - GroundCenter;
					CameraLocation.X += Adjustment.X;
					CameraLocation.Y += Adjustment.Y;
					Camera->SetActorLocation(CameraLocation);
					bCameraCentered = true;
				}
			}
		}
	}

	void RequestCapture(const TCHAR* Filename, const ECaptureStage NextStage)
	{
		PendingCapturePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/RTS"), Filename);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PendingCapturePath), true);
		IFileManager::Get().Delete(*PendingCapturePath, false, true);
		FScreenshotRequest::RequestScreenshot(PendingCapturePath, true, false);
		StageAfterCapture = NextStage;
		bCapturePending = true;
	}

	bool ApplyConfiguredRadius()
	{
		FRTSWorldOverlayDescriptor Changed = RTSMilestone3OverlayTestPrivate::MakeCircle(
			ERTSWorldOverlayMode::TurretRange,
			5,
			FRTSMilestone2Configuration::Load().DefensiveTurret.AttackRange);
		Changed.WorldTransform.SetLocation(
			Showcase->GetActorLocation() + FVector(500.0f, 600.0f, 0.0f));
		Changed.TeamId = RTSTeams::Enemy;
		if (Overlays->Submit(Changed).bAccepted)
		{
			return true;
		}
		Test->AddError(TEXT("The configured-radius recapture could not update its descriptor."));
		return false;
	}

	bool FinishIfTimedOut(const TCHAR* Failure)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		Test->AddError(Failure);
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	double Deadline = 0.0;
	double StageReadyAt = 0.0;
	bool bInitialized = false;
	bool bCapturePending = false;
	bool bCameraCentered = false;
	ECaptureStage Stage = ECaptureStage::Normal;
	ECaptureStage StageAfterCapture = ECaptureStage::Normal;
	FString PendingCapturePath;
	FVector CameraLocation = FVector::ZeroVector;
	TWeakObjectPtr<ARTSCameraPawn> Camera;
	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<ARTSWorldOverlayShowcaseActor> Showcase;
	URTSWorldOverlaySubsystem* Overlays = nullptr;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3OverlayShowcaseTest,
	"Task0169.Rendered.Task0161.Overlay.Showcase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone3OverlayShowcaseTest::RunTest(const FString& Parameters)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
	{
		AddError(TEXT("This task requires real RHI; NullRHI is not valid rendered evidence."));
		return false;
	}
	if (!TestTrue(
		TEXT("The skirmish opens before rendered overlay verification"),
		AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSCaptureOverlayShowcaseCommand(
		this,
		FPlatformTime::Seconds() + 60.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

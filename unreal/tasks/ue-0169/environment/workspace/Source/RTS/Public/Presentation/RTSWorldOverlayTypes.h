// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "RTSWorldOverlayTypes.generated.h"

UENUM()
enum class ERTSWorldOverlayMode : uint8
{
	BuildArea,
	PlacementFootprint,
	TurretRange
};

UENUM()
enum class ERTSWorldOverlayValidity : uint8
{
	Neutral,
	Valid,
	Invalid
};

UENUM()
enum class ERTSWorldOverlaySourceKind : uint8
{
	Structure,
	DeploymentPreview,
	PlacementPreview,
	Showcase
};

USTRUCT()
struct RTS_API FRTSWorldOverlayHandle
{
	GENERATED_BODY()

	int32 Value = INDEX_NONE;

	bool IsValid() const { return Value != INDEX_NONE; }
	bool operator==(const FRTSWorldOverlayHandle& Other) const { return Value == Other.Value; }
};

USTRUCT()
struct RTS_API FRTSWorldOverlayDescriptor
{
	GENERATED_BODY()

	ERTSWorldOverlayMode Mode = ERTSWorldOverlayMode::BuildArea;
	ERTSWorldOverlaySourceKind SourceKind = ERTSWorldOverlaySourceKind::Structure;
	int32 StableSourceId = INDEX_NONE;
	FTransform WorldTransform = FTransform::Identity;
	FVector2D HalfExtent = FVector2D::ZeroVector;
	float Radius = 0.0f;
	ERTSWorldOverlayValidity Validity = ERTSWorldOverlayValidity::Neutral;
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	float AnimationPhaseSeconds = 0.0f;
	float LifetimeSeconds = 0.0f;
};

USTRUCT()
struct RTS_API FRTSWorldOverlaySubmission
{
	GENERATED_BODY()

	bool bAccepted = false;
	FRTSWorldOverlayHandle Handle;
};

USTRUCT()
struct RTS_API FRTSWorldOverlaySnapshot
{
	GENERATED_BODY()

	FRTSWorldOverlayHandle Handle;
	FRTSWorldOverlayDescriptor Descriptor;
};

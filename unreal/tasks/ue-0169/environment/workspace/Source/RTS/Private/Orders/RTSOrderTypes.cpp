// Copyright Epic Games, Inc. All Rights Reserved.

#include "Orders/RTSOrderTypes.h"

#include "Misc/ConfigCacheIni.h"
#include "Reclaim/RTSWreckage.h"
#include "Structures/RTSStructure.h"

FRTSMovementTuning FRTSMovementTuning::Load()
{
	FRTSMovementTuning Tuning;
	constexpr TCHAR Section[] = TEXT("RTS.Milestone1.Movement");
	GConfig->GetFloat(Section, TEXT("UnitSpeed"), Tuning.UnitSpeed, GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("TurnRateDegreesPerSecond"),
		Tuning.TurnRateDegreesPerSecond,
		GGameIni);
	GConfig->GetFloat(Section, TEXT("ArrivalTolerance"), Tuning.ArrivalTolerance, GGameIni);
	GConfig->GetFloat(Section, TEXT("FormationGap"), Tuning.FormationGap, GGameIni);
	GConfig->GetFloat(Section, TEXT("RetryDelaySeconds"), Tuning.RetryDelaySeconds, GGameIni);
	GConfig->GetInt(
		Section,
		TEXT("MaximumFormationExpansionRings"),
		Tuning.MaximumFormationExpansionRings,
		GGameIni);

	Tuning.UnitSpeed = FMath::Max(1.0f, Tuning.UnitSpeed);
	Tuning.TurnRateDegreesPerSecond = FMath::Max(1.0f, Tuning.TurnRateDegreesPerSecond);
	Tuning.ArrivalTolerance = FMath::Max(1.0f, Tuning.ArrivalTolerance);
	Tuning.FormationGap = FMath::Max(0.0f, Tuning.FormationGap);
	Tuning.RetryDelaySeconds = FMath::Max(0.01f, Tuning.RetryDelaySeconds);
	Tuning.MaximumFormationExpansionRings = FMath::Clamp(
		Tuning.MaximumFormationExpansionRings,
		0,
		8);
	return Tuning;
}

FRTSOrderRequest FRTSOrderRequest::MakeMove(
	const int64 NewGroupCommandId,
	const FVector& NewDestination,
	const float NewAcceptanceRadius)
{
	FRTSOrderRequest Request;
	Request.Kind = ERTSOrderKind::Move;
	Request.GroupCommandId = NewGroupCommandId;
	Request.Destination = NewDestination;
	Request.AcceptanceRadius = NewAcceptanceRadius;
	return Request;
}

FRTSOrderRequest FRTSOrderRequest::MakeAttack(
	const int64 NewGroupCommandId,
	ARTSCombatUnit& NewTarget,
	const bool bNewAutomatic)
{
	FRTSOrderRequest Request;
	Request.Kind = ERTSOrderKind::Attack;
	Request.GroupCommandId = NewGroupCommandId;
	Request.Target = &NewTarget;
	Request.bAutomatic = bNewAutomatic;
	return Request;
}

FRTSOrderRequest FRTSOrderRequest::MakeAttack(
	const int64 NewGroupCommandId,
	ARTSStructure& NewTarget)
{
	FRTSOrderRequest Request;
	Request.Kind = ERTSOrderKind::Attack;
	Request.GroupCommandId = NewGroupCommandId;
	Request.TargetStructure = &NewTarget;
	return Request;
}

FRTSOrderRequest FRTSOrderRequest::MakeReclaim(
	const int64 NewGroupCommandId,
	ARTSWreckage& NewTarget)
{
	FRTSOrderRequest Request;
	Request.Kind = ERTSOrderKind::Reclaim;
	Request.GroupCommandId = NewGroupCommandId;
	Request.WreckageTarget = &NewTarget;
	return Request;
}

int32 FRTSCommandResult::GetAcceptedCount() const
{
	int32 AcceptedCount = 0;
	for (const FRTSUnitCommandOutcome& Outcome : Outcomes)
	{
		AcceptedCount += Outcome.bAccepted ? 1 : 0;
	}
	return AcceptedCount;
}

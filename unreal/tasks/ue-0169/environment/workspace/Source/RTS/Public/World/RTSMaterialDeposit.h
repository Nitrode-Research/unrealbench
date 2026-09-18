// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSMaterialDeposit.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/** Fixed, typed extraction anchor; claiming and income begin in the structure slice. */
UCLASS()
class RTS_API ARTSMaterialDeposit final : public AActor
{
	GENERATED_BODY()

public:
	ARTSMaterialDeposit();

	void SetStableDepositId(int32 NewStableDepositId);
	int32 GetStableDepositId() const;

protected:
	virtual void BeginPlay() override;

private:
	void UpdatePresentation();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> DepositMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> DepositMaterialParent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DepositMaterialInstance;

	UPROPERTY(VisibleInstanceOnly, Category = "RTS Material Deposit")
	int32 StableDepositId = INDEX_NONE;
};

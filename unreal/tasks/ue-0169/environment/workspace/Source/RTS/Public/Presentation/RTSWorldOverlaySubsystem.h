// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Presentation/RTSWorldOverlayTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSWorldOverlaySubsystem.generated.h"

class ARTSStructure;
class ARTSWorldOverlayActor;
struct FRTSMatchSnapshot;
struct FRTSStructureSnapshot;

/** Owns typed world-overlay replacement, rendering, and expiry without owning gameplay legality. */
UCLASS()
class RTS_API URTSWorldOverlaySubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	FRTSWorldOverlaySubmission Submit(const FRTSWorldOverlayDescriptor& Descriptor);
	bool Remove(FRTSWorldOverlayHandle Handle);
	int32 RemoveSource(ERTSWorldOverlaySourceKind SourceKind, int32 StableSourceId);
	TArray<FRTSWorldOverlaySnapshot> GetSnapshots();

private:
	struct FOverlayEntry
	{
		FRTSWorldOverlayDescriptor Descriptor;
		TWeakObjectPtr<ARTSWorldOverlayActor> Actor;
		uint64 SourceKey = 0;
	};

	static uint64 MakeSourceKey(const FRTSWorldOverlayDescriptor& Descriptor);
	void PruneExpired();
	void HandleStructureChanged(const FRTSStructureSnapshot& Snapshot);
	void HandleStructureDestroyed(const FRTSStructureSnapshot& Snapshot);
	void HandleMatchResolved(const FRTSMatchSnapshot& Snapshot);
	void SubmitStructureOverlays(ARTSStructure& Structure);

	int32 NextHandle = 1;
	TMap<int32, FOverlayEntry> EntriesByHandle;
	TMap<uint64, int32> HandleBySource;
};

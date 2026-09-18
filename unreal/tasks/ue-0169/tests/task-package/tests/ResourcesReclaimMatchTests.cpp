// Hidden, behavior-only UE-0160 verifier. Injected after solving.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Combat/RTSHealthComponent.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/WorldInitializationValues.h"
#include "GameFramework/WorldSettings.h"
#include "Match/RTSMatchSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Reclaim/RTSReclaimComponent.h"
#include "Reclaim/RTSWreckage.h"
#include "Reclaim/RTSWreckageSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

namespace GE160
{
constexpr auto Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
// No navigation, viewport, ticking AI or synthetic gameplay implementation. Scenarios drive
// transaction APIs, real actor damage and the actor-local reclaim advancement interface.
struct FWorldFixture
{
    UWorld* World = nullptr;
    URTSEconomySubsystem* Economy = nullptr;
    URTSWreckageSubsystem* Wrecks = nullptr;
    URTSMatchSubsystem* Match = nullptr;
    int32 NextId = 100;
    FWorldFixture()
    {
        if (!GEngine) return;
        FWorldInitializationValues Values;
        Values.SetDefaultGameMode(nullptr).CreatePhysicsScene(true);
        World = UWorld::CreateWorld(EWorldType::Game, false,
            MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GE160")),
            GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values, true);
        if (!World) return;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitWorld(Values);
        World->InitializeActorsForPlay(FURL());
        World->BeginPlay();
        World->GetWorldSettings()->NotifyBeginPlay();
        Economy = World->GetSubsystem<URTSEconomySubsystem>();
        Wrecks = World->GetSubsystem<URTSWreckageSubsystem>();
        Match = World->GetSubsystem<URTSMatchSubsystem>();
    }
    ~FWorldFixture()
    {
        if (!World) return;
        World->EndPlay(EEndPlayReason::Quit);
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    }
    bool Ready(FAutomationTestBase& T)
    {
        return T.TestNotNull(TEXT("Native world"), World)
            && T.TestNotNull(TEXT("Ledger"), Economy)
            && T.TestNotNull(TEXT("Wreck registry"), Wrecks)
            && T.TestNotNull(TEXT("Match owner"), Match)
            && T.TestTrue(TEXT("BeginPlay dispatched"), World->HasBegunPlay());
    }
    ARTSCombatUnit* Unit(ERTSUnitType Type = ERTSUnitType::InfantrySquad,
        FGenericTeamId Team = RTSTeams::Player)
    {
        const FTransform Transform(FVector(++NextId * 500.f, 0.f, 100.f));
        auto* A = World->SpawnActorDeferred<ARTSCombatUnit>(ARTSCombatUnit::StaticClass(),
            Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (A) { A->ConfigureMilestone2Unit(Type, NextId, Team); A->FinishSpawning(Transform); }
        return A;
    }
    ARTSStructure* Structure(ERTSStructureType Type, FGenericTeamId Team)
    {
        const auto Config = FRTSMilestone2Configuration::Load();
        const auto* Definition = Config.FindStructure(Type);
        if (!Definition) return nullptr;
        const FTransform Transform(FVector(++NextId * 500.f, 4000.f, 0.f));
        auto* A = World->SpawnActorDeferred<ARTSStructure>(ARTSStructure::StaticClass(),
            Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (A) { A->Configure(Type, NextId, Team, *Definition, Config.World.PlacementCellSize); A->FinishSpawning(Transform); }
        return A;
    }
    ARTSWreckage* Wreck(FVector Location, int32 Value = 73)
    {
        FActorSpawnParameters P;
        P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* A = World->SpawnActor<ARTSWreckage>(Location, FRotator::ZeroRotator, P);
        if (A) A->ConfigureFromUnit(++NextId, ERTSUnitType::LightVehicle, RTSTeams::Enemy, Value);
        return A;
    }
    int64 Commit(const FRTSEconomyDelta& Delta, FGenericTeamId Team = RTSTeams::Player)
    {
        const int64 Id = Economy->AllocateTransactionId();
        return Economy->TryCommit(Id, Team, Delta).bAccepted ? Id : 0;
    }
};

bool EqualTotals(FAutomationTestBase& T, const FRTSEconomySnapshot& A, const FRTSEconomySnapshot& B)
{
    T.TestEqual(TEXT("Materials unchanged"), A.Materials, B.Materials);
    T.TestEqual(TEXT("Generation unchanged"), A.PowerGeneration, B.PowerGeneration);
    T.TestEqual(TEXT("Demand unchanged"), A.PowerDemand, B.PowerDemand);
    T.TestEqual(TEXT("Living supply unchanged"), A.SupplyUsed, B.SupplyUsed);
    T.TestEqual(TEXT("Reserved supply unchanged"), A.SupplyReserved, B.SupplyReserved);
    T.TestEqual(TEXT("Capacity unchanged"), A.SupplyCapacity, B.SupplyCapacity);
    return !T.HasAnyErrors();
}
}

// REQUIRED: all-or-nothing commit, receipts, refusal and transaction retirement.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE160Atomic, "Task0169.Headless.GameEngineBench.UE0160.AtomicTransactions", GE160::Flags)
bool FGE160Atomic::RunTest(const FString&)
{
    GE160::FWorldFixture F; if (!F.Ready(*this)) return false;
    auto* E = F.Economy;
    const auto Initial = E->GetSnapshot(RTSTeams::Player);
    TArray<FRTSEconomyTransactionReceipt> Receipts;
    int32 Changed = 0;
    const auto RH = E->OnTransactionApplied().AddLambda([&](const auto& R) { Receipts.Add(R); });
    const auto CH = E->OnEconomyChanged().AddLambda([&](const auto&) { ++Changed; });
    FRTSEconomyDelta D; D.Materials = -37; D.PowerGeneration = 90; D.SupplyCapacity = 8;
    const int64 Id = E->AllocateTransactionId();
    const auto Accepted = E->TryCommit(Id, RTSTeams::Player, D);
    TestTrue(TEXT("Multi-resource commit accepted"), Accepted.bAccepted);
    TestEqual(TEXT("Result carries transaction ID"), Accepted.TransactionId, Id);
    TestEqual(TEXT("Cost paid"), Accepted.Snapshot.Materials, Initial.Materials - 37);
    TestEqual(TEXT("Provider installed"), Accepted.Snapshot.PowerGeneration, 90);
    TestEqual(TEXT("Capacity installed"), Accepted.Snapshot.SupplyCapacity, 8);
    TestEqual(TEXT("Duplicate explicit"), E->TryCommit(Id, RTSTeams::Enemy, D).Refusal, ERTSEconomyRefusal::AlreadyApplied);
    TestEqual(TEXT("Enemy unaffected"), E->GetSnapshot(RTSTeams::Enemy).Materials, Initial.Materials);
    TestTrue(TEXT("Rollback accepted once"), E->TryRollback(Id));
    TestFalse(TEXT("Rollback cannot repeat"), E->TryRollback(Id));
    TestEqual(TEXT("Retired ID cannot be reused"), E->TryCommit(Id, RTSTeams::Player, D).Refusal, ERTSEconomyRefusal::AlreadyApplied);
    GE160::EqualTotals(*this, Initial, E->GetSnapshot(RTSTeams::Player));
    TestEqual(TEXT("Only committed changes notify"), Changed, 2);
    TestEqual(TEXT("Two receipts"), Receipts.Num(), 2);
    if (Receipts.Num() == 2)
    {
        TestEqual(TEXT("Commit kind"), Receipts[0].Kind, ERTSEconomyTransactionKind::Commit);
        TestEqual(TEXT("Rollback kind"), Receipts[1].Kind, ERTSEconomyTransactionKind::Rollback);
        TestEqual(TEXT("Rollback signed delta"), Receipts[1].Delta.Materials, 37);
    }
    E->OnTransactionApplied().Remove(RH); E->OnEconomyChanged().Remove(CH);
    return !HasAnyErrors();
}

// REQUIRED: refusals do not partially mutate or consume a retryable transaction ID.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE160Refusals, "Task0169.Headless.GameEngineBench.UE0160.RefusalAndCapacity", GE160::Flags)
bool FGE160Refusals::RunTest(const FString&)
{
    GE160::FWorldFixture F; if (!F.Ready(*this)) return false;
    auto* E = F.Economy;
    const auto Initial = E->GetSnapshot(RTSTeams::Player);
    FRTSEconomyDelta D; D.Materials = -Initial.Materials - 1; D.PowerGeneration = 71;
    const int64 Retry = E->AllocateTransactionId();
    TestEqual(TEXT("Insufficient funds"), E->TryCommit(Retry, RTSTeams::Player, D).Refusal, ERTSEconomyRefusal::InsufficientMaterials);
    GE160::EqualTotals(*this, Initial, E->GetSnapshot(RTSTeams::Player));
    D = {}; D.SupplyCapacity = 5;
    TestTrue(TEXT("Rejected ID can retry"), E->TryCommit(Retry, RTSTeams::Player, D).bAccepted);
    D = {}; D.SupplyUsed = 3; D.SupplyReserved = 2;
    TestTrue(TEXT("Usage plus reservations exactly at cap accepted"), F.Commit(D) > 0);
    auto AtCap = E->GetSnapshot(RTSTeams::Player);
    D = {}; D.SupplyReserved = 1; D.Materials = -9;
    TestEqual(TEXT("No partial payment beyond cap"), E->TryCommit(E->AllocateTransactionId(), RTSTeams::Player, D).Refusal, ERTSEconomyRefusal::InsufficientSupply);
    GE160::EqualTotals(*this, AtCap, E->GetSnapshot(RTSTeams::Player));
    D = {}; D.SupplyCapacity = -2;
    TestTrue(TEXT("Capacity may fall below existing army"), F.Commit(D) > 0);
    TestEqual(TEXT("Existing army preserved"), E->GetSnapshot(RTSTeams::Player).SupplyUsed, 3);
    D = {}; D.PowerDemand = 1;
    TestTrue(TEXT("Brownout is legal ledger state"), F.Commit(D) > 0);
    TestTrue(TEXT("Demand beyond generation observed"), E->GetSnapshot(RTSTeams::Player).IsInBrownout());
    const auto Before = E->GetSnapshot(RTSTeams::Player);
    D = {}; D.PowerGeneration = -1; D.Materials = 99;
    TestEqual(TEXT("Negative totals refused"), E->TryCommit(E->AllocateTransactionId(), RTSTeams::Player, D).Refusal, ERTSEconomyRefusal::WouldCreateNegativeTotal);
    TestEqual(TEXT("Invalid transaction refused"), E->TryCommit(0, RTSTeams::Player, D).Refusal, ERTSEconomyRefusal::InvalidTransaction);
    TestEqual(TEXT("Unsupported team refused"), E->TryCommit(E->AllocateTransactionId(), FGenericTeamId::NoTeam, D).Refusal, ERTSEconomyRefusal::InvalidTeam);
    GE160::EqualTotals(*this, Before, E->GetSnapshot(RTSTeams::Player));
    return !HasAnyErrors();
}

// REQUIRED: separate paid reservation, living usage and cancellation lifetimes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE160Reservations, "Task0169.Headless.GameEngineBench.UE0160.ReservationLifecycle", GE160::Flags)
bool FGE160Reservations::RunTest(const FString&)
{
    GE160::FWorldFixture F; if (!F.Ready(*this)) return false;
    auto* E = F.Economy; const int32 Money = E->GetSnapshot(RTSTeams::Player).Materials;
    FRTSEconomyDelta D; D.SupplyCapacity = 10; TestTrue(TEXT("Cap setup"), F.Commit(D) > 0);
    D = {}; D.SupplyReserved = 4; D.Materials = -53;
    const int64 Reservation = F.Commit(D);
    const int64 Usage = E->AllocateTransactionId();
    TArray<FRTSEconomyTransactionReceipt> R;
    const auto H = E->OnTransactionApplied().AddLambda([&](const auto& Receipt) { R.Add(Receipt); });
    TestFalse(TEXT("Conversion needs valid usage ID"), E->TryConvertSupplyReservation(Reservation, 0));
    TestTrue(TEXT("Reservation converts"), E->TryConvertSupplyReservation(Reservation, Usage));
    TestFalse(TEXT("Conversion cannot repeat"), E->TryConvertSupplyReservation(Reservation, E->AllocateTransactionId()));
    TestFalse(TEXT("Converted reservation cannot be released"), E->TryReleaseSupplyReservation(Reservation));
    auto S = E->GetSnapshot(RTSTeams::Player);
    TestEqual(TEXT("Reserved supply drained"), S.SupplyReserved, 0);
    TestEqual(TEXT("Living supply assigned"), S.SupplyUsed, 4);
    TestEqual(TEXT("Payment is not refunded by conversion"), S.Materials, Money - 53);
    TestTrue(TEXT("Unit destruction rolls usage back"), E->TryRollback(Usage));
    TestEqual(TEXT("Death removes living supply"), E->GetSnapshot(RTSTeams::Player).SupplyUsed, 0);
    TestTrue(TEXT("Original payment rollback independent"), E->TryRollback(Reservation));
    TestEqual(TEXT("Explicit payment rollback refunds"), E->GetSnapshot(RTSTeams::Player).Materials, Money);
    D.SupplyReserved = 2; D.Materials = -19;
    const int64 Cancelled = F.Commit(D);
    TestTrue(TEXT("Release cancellation"), E->TryReleaseSupplyReservation(Cancelled));
    TestFalse(TEXT("Release cannot repeat"), E->TryReleaseSupplyReservation(Cancelled));
    TestFalse(TEXT("Released reservation cannot convert"), E->TryConvertSupplyReservation(Cancelled, E->AllocateTransactionId()));
    TestEqual(TEXT("Release alone keeps payment"), E->GetSnapshot(RTSTeams::Player).Materials, Money - 19);
    TestEqual(TEXT("No reserved supply left"), E->GetSnapshot(RTSTeams::Player).SupplyReserved, 0);
    TestTrue(TEXT("Conversion receipt exists"), R.Num() > 0);
    if (R.Num())
    {
        TestEqual(TEXT("Conversion receipt kind"), R[0].Kind, ERTSEconomyTransactionKind::SupplyConversion);
        TestEqual(TEXT("Conversion links reservation"), R[0].RelatedTransactionId, Reservation);
        TestEqual(TEXT("Conversion signed reserve delta"), R[0].Delta.SupplyReserved, -4);
        TestEqual(TEXT("Conversion signed usage delta"), R[0].Delta.SupplyUsed, 4);
    }
    E->OnTransactionApplied().Remove(H);
    return !HasAnyErrors();
}

// REQUIRED: real death producers, configured value snapshots, ordered registry and destruction cleanup.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE160Wrecks, "Task0169.Headless.GameEngineBench.UE0160.DeathAndWreckRegistry", GE160::Flags)
bool FGE160Wrecks::RunTest(const FString&)
{
    GE160::FWorldFixture F; if (!F.Ready(*this)) return false;
    auto* Unit = F.Unit(ERTSUnitType::LightVehicle, RTSTeams::Enemy);
    auto* Structure = F.Structure(ERTSStructureType::PowerGenerator, RTSTeams::Player);
    if (!TestNotNull(TEXT("Real unit"), Unit) || !TestNotNull(TEXT("Real structure"), Structure)) return false;
    TestEqual(TEXT("No initial wrecks"), F.Wrecks->QueryAvailable().Num(), 0);
    TestTrue(TEXT("Unit dies through health"), Unit->GetHealthComponent()->ApplyDamage(100000.f).bKilled);
    TestFalse(TEXT("Repeated lethal hit cannot kill again"), Unit->GetHealthComponent()->ApplyDamage(100000.f).bAccepted);
    TestTrue(TEXT("Structure dies through public damage"), Structure->ApplyDamage(100000.f).bKilled);
    TestFalse(TEXT("Structure death cannot repeat"), Structure->ApplyDamage(100000.f).bAccepted);
    auto Found = F.Wrecks->QueryAvailable();
    if (!TestEqual(TEXT("Exactly two wrecks"), Found.Num(), 2)) return false;
    const auto Config = FRTSMilestone2Configuration::Load();
    const auto A = Found[0]->GetSnapshot(), B = Found[1]->GetSnapshot();
    TestTrue(TEXT("Stable ascending IDs"), A.StableWreckageId < B.StableWreckageId);
    TestEqual(TEXT("Unit source identity"), A.SourceKind, ERTSWreckageSourceKind::Unit);
    TestEqual(TEXT("Structure source identity"), B.SourceKind, ERTSWreckageSourceKind::Structure);
    TestEqual(TEXT("Unit configured salvage"), A.ReclaimMaterialValue, FMath::RoundToInt(Config.FindUnit(ERTSUnitType::LightVehicle)->MaterialCost * Config.Reclaim.MaterialFraction));
    TestEqual(TEXT("Structure configured salvage"), B.ReclaimMaterialValue, FMath::RoundToInt(Config.FindStructure(ERTSStructureType::PowerGenerator)->MaterialCost * Config.Reclaim.MaterialFraction));
    TestTrue(TEXT("Original faction snapshot"), A.OriginalTeamId == RTSTeams::Enemy);
    TestEqual(TEXT("Snapshot query closure"), F.Wrecks->QueryAvailableSnapshots().Num(), 2);
    TestTrue(TEXT("Lookup returns exact actor"), F.Wrecks->Find(A.StableWreckageId) == Found[0]);
    Found[0]->Destroy();
    TestNull(TEXT("Destroyed entry removed"), F.Wrecks->Find(A.StableWreckageId));
    TestEqual(TEXT("Availability drops dead weak entries"), F.Wrecks->QueryAvailable().Num(), 1);
    return !HasAnyErrors();
}

// REQUIRED: exclusive lease owner, safe release, wrong-world and dead-owner refusal.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE160Leases, "Task0169.Headless.GameEngineBench.UE0160.LeaseOwnership", GE160::Flags)
bool FGE160Leases::RunTest(const FString&)
{
    GE160::FWorldFixture F; if (!F.Ready(*this)) return false;
    auto* A = F.Unit(); auto* B = F.Unit();
    if (!TestNotNull(TEXT("A"), A) || !TestNotNull(TEXT("B"), B)) return false;
    auto* W = F.Wreck(A->GetActorLocation()); if (!TestNotNull(TEXT("Wreck"), W)) return false;
    TestTrue(TEXT("Initial lease"), W->TryAcquireLease(*A).bAccepted);
    TestTrue(TEXT("Same owner repeat is idempotent"), W->TryAcquireLease(*A).bAccepted);
    TestEqual(TEXT("Contender refused"), W->TryAcquireLease(*B).Refusal, ERTSReclaimRefusal::AlreadyLeased);
    W->ReleaseLease(*B);
    TestEqual(TEXT("Non-owner cannot release"), W->GetSnapshot().LeaseHolderStableUnitId, A->GetStableUnitId());
    TestFalse(TEXT("Non-owner cannot collect"), W->TryCompleteReclaim(*B).bAccepted);
    W->ReleaseLease(*A);
    TestTrue(TEXT("Released lease transfers"), W->TryAcquireLease(*B).bAccepted);
    W->ReleaseLease(*B);
    A->GetHealthComponent()->ApplyDamage(100000.f);
    TestEqual(TEXT("Dead reclaimer refused"), W->TryAcquireLease(*A).Refusal, ERTSReclaimRefusal::ReclaimerUnavailable);
    GE160::FWorldFixture Other; if (!Other.Ready(*this)) return false;
    auto* Foreign = Other.Unit(); if (!TestNotNull(TEXT("Foreign actor"), Foreign)) return false;
    TestEqual(TEXT("Cross-world lease refused"), W->TryAcquireLease(*Foreign).Refusal, ERTSReclaimRefusal::WrongWorld);
    return !HasAnyErrors();
}

// REQUIRED: progress/time/range guards and restart after explicit interruption.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE160Progress, "Task0169.Headless.GameEngineBench.UE0160.ReclaimProgressAndInterrupt", GE160::Flags)
bool FGE160Progress::RunTest(const FString&)
{
    GE160::FWorldFixture F; if (!F.Ready(*this)) return false;
    auto* U = F.Unit(); if (!TestNotNull(TEXT("Infantry"), U)) return false;
    auto* C = U->GetReclaimComponent(); auto* W = F.Wreck(U->GetActorLocation());
    if (!TestNotNull(TEXT("Reclaim module"), C) || !TestNotNull(TEXT("Wreck"), W)) return false;
    const auto Tune = FRTSMilestone2Configuration::Load().Reclaim;
    TestTrue(TEXT("Begin accepted"), C->Begin(*W).bAccepted);
    TestEqual(TEXT("Approaching before advancement"), C->GetSnapshot().State, ERTSReclaimState::Approaching);
    TestTrue(TEXT("Negative time accepted as zero"), C->Advance(-50.f).bAccepted);
    TestEqual(TEXT("Negative time no progress"), C->GetSnapshot().Progress, 0.f);
    TestTrue(TEXT("Quarter duration accepted"), C->Advance(Tune.DurationSeconds * .25f).bAccepted);
    TestTrue(TEXT("Quarter progress"), FMath::IsNearlyEqual(C->GetSnapshot().Progress, .25f));
    C->MarkApproaching();
    TestEqual(TEXT("Repath state"), C->GetSnapshot().State, ERTSReclaimState::Approaching);
    TestTrue(TEXT("Repath preserves progress"), FMath::IsNearlyEqual(C->GetSnapshot().Progress, .25f));
    U->SetActorLocation(W->GetActorLocation() + FVector(Tune.Range + 100.f, 0, 0));
    TestEqual(TEXT("Out of range interrupts"), C->Advance(1.f).Refusal, ERTSReclaimRefusal::OutOfRange);
    TestNull(TEXT("Target cleared"), C->GetSnapshot().Target.Get());
    TestEqual(TEXT("Progress reset"), C->GetSnapshot().Progress, 0.f);
    TestEqual(TEXT("Lease released"), W->GetSnapshot().LeaseHolderStableUnitId, INDEX_NONE);
    U->SetActorLocation(W->GetActorLocation());
    TestTrue(TEXT("Restart allowed"), C->Begin(*W).bAccepted);
    C->Advance(Tune.DurationSeconds * .25f);
    U->GetOrderComponent()->Cancel();
    TestNull(TEXT("Order cancellation clears reclaim"), C->GetSnapshot().Target.Get());
    TestEqual(TEXT("Cancellation releases lease"), W->GetSnapshot().LeaseHolderStableUnitId, INDEX_NONE);
    TestTrue(TEXT("Unconsumed wreck survives cancellation"), W->IsAvailable());
    auto* Vehicle = F.Unit(ERTSUnitType::LightVehicle);
    if (!TestNotNull(TEXT("Non-infantry fixture"), Vehicle)) return false;
    auto* WrongCapability = NewObject<URTSReclaimComponent>(Vehicle);
    Vehicle->AddInstanceComponent(WrongCapability); WrongCapability->RegisterComponent();
    TestEqual(TEXT("Component rejects non-infantry owner"), WrongCapability->Begin(*W).Refusal, ERTSReclaimRefusal::NotInfantry);

    // A genuine uninterrupted attempt must accumulate frames, not just compare one frame
    // against the full duration. Keep the earlier single-step and cancellation scenarios.
    auto* SplitWreck = F.Wreck(U->GetActorLocation(), 91);
    if (!TestNotNull(TEXT("Split-duration wreck"), SplitWreck)) return false;
    const int32 BeforeSplit = F.Economy->GetSnapshot(RTSTeams::Player).Materials;
    if (!TestTrue(TEXT("Split-duration attempt acquires lease"), C->Begin(*SplitWreck).bAccepted)) return false;
    TestEqual(TEXT("Split attempt has actual owner"), SplitWreck->GetSnapshot().LeaseHolderStableUnitId, U->GetStableUnitId());
    TestTrue(TEXT("First sub-duration step accepted"), C->Advance(Tune.DurationSeconds * .125f).bAccepted);
    TestTrue(TEXT("First cumulative progress"), FMath::IsNearlyEqual(C->GetSnapshot().Progress, .125f));
    TestTrue(TEXT("Second sub-duration step accepted"), C->Advance(Tune.DurationSeconds * .375f).bAccepted);
    TestTrue(TEXT("Unequal frames accumulate to half"), FMath::IsNearlyEqual(C->GetSnapshot().Progress, .5f));
    C->Advance(-Tune.DurationSeconds);
    TestTrue(TEXT("Negative frame preserves existing progress"), FMath::IsNearlyEqual(C->GetSnapshot().Progress, .5f));
    const auto BeforeLast = C->Advance(Tune.DurationSeconds * .25f);
    TestTrue(TEXT("Third sub-duration step accepted"), BeforeLast.bAccepted);
    TestFalse(TEXT("Three-quarter attempt not complete"), BeforeLast.bCompleted);
    TestTrue(TEXT("Three-quarter accumulated progress"), FMath::IsNearlyEqual(C->GetSnapshot().Progress, .75f));
    TestEqual(TEXT("Accumulated partial work still unpaid"), F.Economy->GetSnapshot(RTSTeams::Player).Materials, BeforeSplit);
    TestTrue(TEXT("Final quarter completes accumulated duration"), C->Advance(Tune.DurationSeconds * .25f).bCompleted);
    TestEqual(TEXT("Split completion state"), C->GetSnapshot().State, ERTSReclaimState::Completed);
    TestNull(TEXT("Split completion clears target"), C->GetSnapshot().Target.Get());
    TestEqual(TEXT("Split completion pays exactly once"), F.Economy->GetSnapshot(RTSTeams::Player).Materials, BeforeSplit + 91);
    TestFalse(TEXT("Extra split step cannot complete twice"), C->Advance(Tune.DurationSeconds * .25f).bCompleted);
    TestEqual(TEXT("Extra step cannot repeat split payout"), F.Economy->GetSnapshot(RTSTeams::Player).Materials, BeforeSplit + 91);
    return !HasAnyErrors();
}

// REQUIRED integrated sequence: death -> salvage -> partial attempt -> reclaimer death -> payout once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE160Payout, "Task0169.Headless.GameEngineBench.UE0160.DeathToReclaimPayout", GE160::Flags)
bool FGE160Payout::RunTest(const FString&)
{
    GE160::FWorldFixture F; if (!F.Ready(*this)) return false;
    auto* Victim = F.Unit(ERTSUnitType::HeavyVehicle, RTSTeams::Enemy);
    auto* A = F.Unit(); auto* B = F.Unit();
    if (!TestNotNull(TEXT("Victim"), Victim) || !TestNotNull(TEXT("First infantry"), A) || !TestNotNull(TEXT("Second infantry"), B)) return false;
    Victim->GetHealthComponent()->ApplyDamage(100000.f);
    auto List = F.Wrecks->QueryAvailable();
    if (!TestEqual(TEXT("One target wreck"), List.Num(), 1)) return false;
    auto* W = List[0]; const auto Saved = W->GetSnapshot();
    A->SetActorLocation(W->GetActorLocation()); B->SetActorLocation(W->GetActorLocation());
    const int32 Before = F.Economy->GetSnapshot(RTSTeams::Player).Materials;
    const auto Tune = FRTSMilestone2Configuration::Load().Reclaim;
    TestTrue(TEXT("First infantry begins"), A->GetReclaimComponent()->Begin(*W).bAccepted);
    A->GetReclaimComponent()->Advance(Tune.DurationSeconds * .5f);
    TestEqual(TEXT("Partial work not paid"), F.Economy->GetSnapshot(RTSTeams::Player).Materials, Before);
    A->GetHealthComponent()->ApplyDamage(100000.f);
    TestEqual(TEXT("Death releases lease"), W->GetSnapshot().LeaseHolderStableUnitId, INDEX_NONE);
    TestTrue(TEXT("Second infantry begins afresh"), B->GetReclaimComponent()->Begin(*W).bAccepted);
    TestEqual(TEXT("No progress inheritance"), B->GetReclaimComponent()->GetSnapshot().Progress, 0.f);
    TestTrue(TEXT("Full duration completes"), B->GetReclaimComponent()->Advance(Tune.DurationSeconds).bCompleted);
    TestEqual(TEXT("Payout to reclaimer faction"), F.Economy->GetSnapshot(RTSTeams::Player).Materials, Before + Saved.ReclaimMaterialValue);
    TestEqual(TEXT("Completion state"), B->GetReclaimComponent()->GetSnapshot().State, ERTSReclaimState::Completed);
    TestNull(TEXT("Consumed wreck unregistered"), F.Wrecks->Find(Saved.StableWreckageId));
    TestFalse(TEXT("Later advance cannot pay twice"), B->GetReclaimComponent()->Advance(Tune.DurationSeconds).bCompleted);
    TestEqual(TEXT("Stable once-only payout"), F.Economy->GetSnapshot(RTSTeams::Player).Materials, Before + Saved.ReclaimMaterialValue);
    TestEqual(TEXT("Result stats include salvage"), F.Match->GetSnapshot().PlayerStatistics.MaterialsReclaimed, Saved.ReclaimMaterialValue);
    return !HasAnyErrors();
}

// REQUIRED: typed team/kind/source deduplication and exact event receipt sequences.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE160Statistics, "Task0169.Headless.GameEngineBench.UE0160.MatchStatistics", GE160::Flags)
bool FGE160Statistics::RunTest(const FString&)
{
    GE160::FWorldFixture F; if (!F.Ready(*this)) return false;
    auto* M = F.Match; int32 Published = 0;
    const auto H = M->OnMatchEventRecorded().AddLambda([&](const auto&) { ++Published; });
    auto R = M->ReportUnitProduced(RTSTeams::Player, 77);
    TestTrue(TEXT("Produced recorded"), R.bAccepted); TestEqual(TEXT("First sequence"), R.EventSequence, 1);
    TestFalse(TEXT("Duplicate produced refused"), M->ReportUnitProduced(RTSTeams::Player, 77).bAccepted);
    TestTrue(TEXT("Same source different kind allowed"), M->ReportUnitLost(RTSTeams::Player, 77).bAccepted);
    TestTrue(TEXT("Same kind/source different team allowed"), M->ReportUnitProduced(RTSTeams::Enemy, 77).bAccepted);
    TestTrue(TEXT("Salvage recorded"), M->ReportMaterialsReclaimed(RTSTeams::Player, 77, 37).bAccepted);
    TestFalse(TEXT("Changed amount cannot replay event"), M->ReportMaterialsReclaimed(RTSTeams::Player, 77, 500).bAccepted);
    TestFalse(TEXT("Unsupported team"), M->ReportUnitProduced(FGenericTeamId::NoTeam, 1).bAccepted);
    TestFalse(TEXT("Invalid source ID"), M->ReportUnitLost(RTSTeams::Player, INDEX_NONE).bAccepted);
    TestFalse(TEXT("Negative salvage"), M->ReportMaterialsReclaimed(RTSTeams::Enemy, 9, -1).bAccepted);
    R = M->ReportMaterialsReclaimed(RTSTeams::Enemy, 9, 0);
    TestTrue(TEXT("Zero salvage legal"), R.bAccepted); TestEqual(TEXT("Refusals do not consume sequence"), R.EventSequence, 5);
    auto S = M->GetSnapshot();
    TestEqual(TEXT("Player produced count"), S.PlayerStatistics.UnitsProduced, 1);
    TestEqual(TEXT("Player lost count"), S.PlayerStatistics.UnitsLost, 1);
    TestEqual(TEXT("Player material total"), S.PlayerStatistics.MaterialsReclaimed, 37);
    TestEqual(TEXT("Enemy produced independent"), S.EnemyStatistics.UnitsProduced, 1);
    TestEqual(TEXT("Only accepted events published"), Published, 5);
    M->OnMatchEventRecorded().Remove(H);
    return !HasAnyErrors();
}

// REQUIRED: mobile command identity and immutable once-only match outcome through real death.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE160Mobile, "Task0169.Headless.GameEngineBench.UE0160.MobileCommandResolution", GE160::Flags)
bool FGE160Mobile::RunTest(const FString&)
{
    GE160::FWorldFixture F; if (!F.Ready(*this)) return false;
    auto* Player = F.Unit(ERTSUnitType::CommandVehicle); auto* Enemy = F.Unit(ERTSUnitType::CommandVehicle, RTSTeams::Enemy);
    auto* Infantry = F.Unit();
    if (!TestNotNull(TEXT("Player command"), Player) || !TestNotNull(TEXT("Enemy command"), Enemy) || !TestNotNull(TEXT("Infantry"), Infantry)) return false;
    TestTrue(TEXT("Identity registered on spawn"), F.Match->GetCommandVehicle(RTSTeams::Enemy) == Enemy);
    TestFalse(TEXT("Infantry cannot register as command"), F.Match->RegisterCommandVehicle(*Infantry));
    int32 Resolutions = 0;
    const auto H = F.Match->OnMatchResolved().AddLambda([&](const auto&) { ++Resolutions; });
    Enemy->GetHealthComponent()->ApplyDamage(100000.f);
    auto S = F.Match->GetSnapshot();
    TestTrue(TEXT("Match resolved"), F.Match->IsResolved());
    TestEqual(TEXT("Mobile reason"), S.Resolution, ERTSMatchResolution::CommandVehicleDestroyed);
    TestTrue(TEXT("Player wins"), S.WinningTeamId == RTSTeams::Player);
    TestTrue(TEXT("Enemy loses"), S.LosingTeamId == RTSTeams::Enemy);
    TestEqual(TEXT("Single resolution sequence"), S.ResolutionSequence, 1);
    TestTrue(TEXT("Nonnegative duration"), S.ActiveDurationSeconds >= 0.0);
    TestFalse(TEXT("Repeated loss report refused"), F.Match->ReportCommandVehicleDestroyed(*Enemy).bAccepted);
    Player->GetHealthComponent()->ApplyDamage(100000.f);
    TestTrue(TEXT("Winner immutable after opposing death"), F.Match->GetSnapshot().WinningTeamId == RTSTeams::Player);
    TestEqual(TEXT("One result publication"), Resolutions, 1);
    auto* W = F.Wreck(Infantry->GetActorLocation());
    if (TestNotNull(TEXT("Post-match wreck"), W))
        TestEqual(TEXT("No new reclaim after match"), Infantry->GetReclaimComponent()->Begin(*W).Refusal, ERTSReclaimRefusal::MatchResolved);
    F.Match->OnMatchResolved().Remove(H);
    return !HasAnyErrors();
}

// REQUIRED: promotion identity transfer, rejection, reversal, obsolete-vehicle immunity and HQ loss.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGE160HQ, "Task0169.Headless.GameEngineBench.UE0160.HeadquartersPromotion", GE160::Flags)
bool FGE160HQ::RunTest(const FString&)
{
    GE160::FWorldFixture F; if (!F.Ready(*this)) return false;
    auto* CV = F.Unit(ERTSUnitType::CommandVehicle);
    auto* HQ = F.Structure(ERTSStructureType::Headquarters, RTSTeams::Player);
    auto* Wrong = F.Structure(ERTSStructureType::PowerGenerator, RTSTeams::Player);
    auto* EnemyHQ = F.Structure(ERTSStructureType::Headquarters, RTSTeams::Enemy);
    if (!TestNotNull(TEXT("CV"), CV) || !TestNotNull(TEXT("HQ"), HQ) || !TestNotNull(TEXT("Other structure"), Wrong) || !TestNotNull(TEXT("Foreign HQ"), EnemyHQ)) return false;
    auto* M = F.Match;
    TestFalse(TEXT("Wrong structure kind cannot promote"), M->PromoteCommandVehicleToHeadquarters(*CV, *Wrong));
    TestFalse(TEXT("Wrong faction cannot promote"), M->PromoteCommandVehicleToHeadquarters(*CV, *EnemyHQ));
    TestTrue(TEXT("Promotion accepted"), M->PromoteCommandVehicleToHeadquarters(*CV, *HQ));
    TestNull(TEXT("Mobile identity removed"), M->GetCommandVehicle(RTSTeams::Player));
    TestTrue(TEXT("HQ identity installed"), M->GetHeadquarters(RTSTeams::Player) == HQ);
    TestFalse(TEXT("Promotion cannot repeat"), M->PromoteCommandVehicleToHeadquarters(*CV, *HQ));
    TestFalse(TEXT("New mobile registration cannot replace HQ"), M->RegisterCommandVehicle(*CV));
    TestFalse(TEXT("Unrelated HQ cannot reverse"), M->RevertHeadquartersPromotion(*CV, *EnemyHQ));
    TestTrue(TEXT("Matching promotion can be reverted"), M->RevertHeadquartersPromotion(*CV, *HQ));
    TestTrue(TEXT("Mobile restored"), M->GetCommandVehicle(RTSTeams::Player) == CV);
    TestNull(TEXT("HQ removed after reversal"), M->GetHeadquarters(RTSTeams::Player));
    TestFalse(TEXT("Reversal cannot repeat"), M->RevertHeadquartersPromotion(*CV, *HQ));
    TestTrue(TEXT("Promotion may be retried after reversal"), M->PromoteCommandVehicleToHeadquarters(*CV, *HQ));
    CV->GetHealthComponent()->ApplyDamage(100000.f);
    TestFalse(TEXT("Obsolete CV destruction cannot end match"), M->IsResolved());
    HQ->ApplyDamage(100000.f);
    TestTrue(TEXT("HQ death resolves"), M->IsResolved());
    TestEqual(TEXT("HQ reason"), M->GetSnapshot().Resolution, ERTSMatchResolution::HeadquartersDestroyed);
    TestTrue(TEXT("Enemy wins HQ loss"), M->GetSnapshot().WinningTeamId == RTSTeams::Enemy);
    TestFalse(TEXT("Repeated HQ report rejected"), M->ReportHeadquartersDestroyed(*HQ).bAccepted);
    return !HasAnyErrors();
}
#endif

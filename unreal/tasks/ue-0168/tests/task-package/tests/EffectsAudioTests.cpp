// Source-derived behavioral verifier. No source-text matching or private access.
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "Combat/RTSTurretCombatComponent.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Configuration/RTSMilestone3Configuration.h"
#include "Match/RTSMatchSubsystem.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Presentation/RTSPresentationSubsystem.h"
#include "Presentation/RTSTransientEffect.h"
#include "Selection/RTSSelectionSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSUnitRegistrySubsystem.h"
#include "Units/RTSTeams.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace EffectsAudio162
{
// Same transient Game-world pattern as the upstream presentation tests. No PIE/map needed.
struct FFixture
{
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 URTSPresentationSubsystem* Presentation = nullptr;
 explicit FFixture(FAutomationTestBase& Test)
 {
  if (World && GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
  Test.TestNotNull(TEXT("Nonempty world fixture"), World);
  if (World) Presentation = World->GetSubsystem<URTSPresentationSubsystem>();
  Test.TestNotNull(TEXT("Presentation subsystem exists"), Presentation);
 }
 ~FFixture()
 {
  if (!World) return;
  World->DestroyWorld(false);
  if (GEngine) GEngine->DestroyWorldContext(World);
 }
 ARTSCombatUnit* Unit(int32 Id, FGenericTeamId Team, FVector Position, ERTSUnitType Type = ERTSUnitType::LightVehicle)
 {
  auto* Unit = World->SpawnActorDeferred<ARTSCombatUnit>(ARTSCombatUnit::StaticClass(), FTransform(Position), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if (Unit) { Unit->ConfigureMilestone2Unit(Type, Id, Team); Unit->FinishSpawning(FTransform(Position)); }
  return Unit;
 }
 ARTSTransientEffect* Effect(int32 Id)
 {
  for (TActorIterator<ARTSTransientEffect> It(World); It; ++It)
   if (IsValid(*It) && It->GetRequest().StableSourceId == Id) return *It;
  return nullptr;
 }
};
FRTSPresentationRequest Request(int32 Id = 91000)
{
 FRTSPresentationRequest R;
 R.Kind = ERTSPresentationEventKind::WeaponImpact;
 R.SourceKind = ERTSPresentationSourceKind::UnitWeapon;
 R.TeamId = RTSTeams::Player;
 R.StableSourceId = Id; R.StableTargetId = Id + 10000; R.SourceEventSequence = 1;
 R.SourceWorldLocation = FVector(40, 70, 20); R.TargetWorldLocation = FVector(340, 470, 20); R.Magnitude = 17;
 return R;
}
int32 Count(const FRTSPresentationSnapshot& S, ERTSPresentationEventKind Kind)
{
 int32 N = 0; for (const auto& R : S.RecentReceipts) if (R.bAccepted && R.Request.Kind == Kind) ++N; return N;
}
}
using namespace EffectsAudio162;

// REQUIRED: malformed data is retained diagnostically but cannot become effects/events.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEA162Validation, "Task0168.Headless.GameEngineBench.UE0162.Validation", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FEA162Validation::RunTest(const FString&)
{
 FFixture F(*this); if (!F.Presentation) return false;
 int32 Broadcasts = 0;
 auto Handle = F.Presentation->OnPresentationRequested().AddLambda([&](const FRTSPresentationReceipt&) { ++Broadcasts; });
 for (int32 Case = 0; Case < 5; ++Case)
 {
  auto R = Request();
  if (Case == 0) R.StableSourceId = INDEX_NONE;
  if (Case == 1) R.SourceEventSequence = 0;
  if (Case == 2) R.SourceWorldLocation.X = std::numeric_limits<double>::quiet_NaN();
  if (Case == 3) R.TargetWorldLocation.Y = std::numeric_limits<double>::infinity();
  if (Case == 4) R.Magnitude = std::numeric_limits<float>::infinity();
  const auto Rejected = F.Presentation->Present(R);
  TestFalse(TEXT("Invalid request rejected"), Rejected.bAccepted);
  TestFalse(TEXT("Invalid is not duplicate"), Rejected.bDuplicate);
 }
 auto S = F.Presentation->GetSnapshot();
 TestEqual(TEXT("All invalid receipts retained"), S.RecentReceipts.Num(), 5);
 TestEqual(TEXT("No invalid event identity tracked"), S.TrackedEventCount, 0);
 TestEqual(TEXT("No invalid emitter"), S.ActiveEffectCount, 0);
 TestEqual(TEXT("No invalid broadcast"), Broadcasts, 0);
 auto Good = F.Presentation->Present(Request());
 TestTrue(TEXT("Invalid predecessor does not poison key"), Good.bAccepted);
 TestEqual(TEXT("First accepted sequence"), Good.ReceiptSequence, 1);
 TestEqual(TEXT("One accepted broadcast"), Broadcasts, 1);
 F.Presentation->OnPresentationRequested().Remove(Handle);
 return !HasAnyErrors();
}

// REQUIRED: identity uses five fields, not location/target/magnitude.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEA162Identity, "Task0168.Headless.GameEngineBench.UE0162.Identity", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FEA162Identity::RunTest(const FString&)
{
 FFixture F(*this); if (!F.Presentation) return false;
 auto Base = Request(); TestTrue(TEXT("First key accepted"), F.Presentation->Present(Base).bAccepted);
 auto ChangedPayload = Base; ChangedPayload.StableTargetId++; ChangedPayload.Magnitude = 30; ChangedPayload.SourceWorldLocation.Z += 5;
 TestTrue(TEXT("Payload does not change event identity"), F.Presentation->Present(ChangedPayload).bDuplicate);
 for (int32 Field = 0; Field < 5; ++Field)
 {
  auto R = Base;
  if (Field == 0) R.Kind = ERTSPresentationEventKind::Destruction;
  if (Field == 1) R.SourceKind = ERTSPresentationSourceKind::TurretWeapon;
  if (Field == 2) R.TeamId = RTSTeams::Enemy;
  if (Field == 3) ++R.StableSourceId;
  if (Field == 4) ++R.SourceEventSequence;
  auto Accepted = F.Presentation->Present(R);
  TestTrue(TEXT("Each key field independently distinguishes identity"), Accepted.bAccepted);
  TestEqual(TEXT("Accepted sequences are contiguous"), Accepted.ReceiptSequence, Field + 2);
 }
 auto S = F.Presentation->GetSnapshot();
 TestEqual(TEXT("Accepted count"), S.AcceptedRequestCount, 6);
 TestEqual(TEXT("Duplicate count"), S.DuplicateRequestCount, 1);
 TestEqual(TEXT("Duplicate spawned no extra emitter"), S.ActiveEffectCount, 6);
 return !HasAnyErrors();
}

// REQUIRED: flood beyond each bound, not merely assert a never-reached cap.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEA162Bounds, "Task0168.Headless.GameEngineBench.UE0162.Bounds", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FEA162Bounds::RunTest(const FString&)
{
 FFixture F(*this); if (!F.Presentation) return false;
 const auto Before = F.Presentation->GetSnapshot();
 if (!TestTrue(TEXT("Configured positive bounded capacities"), Before.MaximumActiveEffects > 0 && Before.MaximumRetainedReceipts > 0 && Before.MaximumTrackedEvents >= Before.MaximumRetainedReceipts)) return false;
 const int32 N = Before.MaximumTrackedEvents + 3;
 if (!TestTrue(TEXT("Fixture capacity remains reasonable"), N < 20000)) return false;
 TWeakObjectPtr<ARTSTransientEffect> FirstEffect;
 for (int32 I = 0; I < N; ++I)
 {
  F.Presentation->Present(Request(100000 + I));
  if (I == 0)
  {
   FirstEffect = F.Effect(100000);
   if (!TestNotNull(TEXT("Overflow fixture begins with a real effect"), FirstEffect.Get())) return false;
  }
 }
 auto S = F.Presentation->GetSnapshot();
 TestEqual(TEXT("Key capacity actually reached"), S.TrackedEventCount, Before.MaximumTrackedEvents);
 TestEqual(TEXT("Receipt capacity actually reached"), S.RecentReceipts.Num(), Before.MaximumRetainedReceipts);
 TestEqual(TEXT("Emitter capacity actually reached"), S.ActiveEffectCount, Before.MaximumActiveEffects);
 TestEqual(TEXT("Overflow evicts effects"), S.EvictedEffectCount, N - Before.MaximumActiveEffects);
 if (S.RecentReceipts.Num()) TestEqual(TEXT("FIFO receipt retains newest event"), S.RecentReceipts.Last().Request.StableSourceId, 100000 + N - 1);
 bool bNewestReceiptWindow = S.RecentReceipts.Num() == Before.MaximumRetainedReceipts;
 for (int32 I = 0; I < S.RecentReceipts.Num(); ++I)
  bNewestReceiptWindow &= S.RecentReceipts[I].Request.StableSourceId == 100000 + N - Before.MaximumRetainedReceipts + I;
 TestTrue(TEXT("All retained receipts form the newest chronological window"), bNewestReceiptWindow);
 TestTrue(TEXT("Overflow destroys the evicted actor rather than merely hiding it"), !FirstEffect.IsValid() || FirstEffect->IsActorBeingDestroyed());
 int32 LiveEffects = 0;
 int32 RegisteredAudioComponents = 0;
 for (TActorIterator<ARTSTransientEffect> It(F.World); It; ++It)
 {
  if (!IsValid(*It) || It->IsActorBeingDestroyed()) continue;
  ++LiveEffects;
  TArray<UAudioComponent*> Components;
  It->GetComponents(Components);
  for (auto* Component : Components) if (IsValid(Component) && Component->IsRegistered()) ++RegisteredAudioComponents;
 }
 TestTrue(TEXT("Physical overflow audit observes nonempty live effects"), LiveEffects > 0);
 TestEqual(TEXT("Tracked cap matches actual live actors including hidden ones"), LiveEffects, S.ActiveEffectCount);
 TestEqual(TEXT("One registered audio component per live effect; no hidden overflow emitters"), RegisteredAudioComponents, S.ActiveEffectCount);
 TestTrue(TEXT("Evicted oldest identity eligible again"), F.Presentation->Present(Request(100000)).bAccepted);
 TestTrue(TEXT("Newest identity still deduplicated"), F.Presentation->Present(Request(100000 + N - 1)).bDuplicate);
 return !HasAnyErrors();
}

// REQUIRED: real native actor and audio component, not receipt booleans alone.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEA162Cues, "Task0168.Headless.GameEngineBench.UE0162.Cues", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FEA162Cues::RunTest(const FString&)
{
 FFixture F(*this); if (!F.Presentation) return false;
 const auto T = FRTSMilestone3Configuration::Load().Presentation;
 TSet<USoundBase*> Sounds;
 const TCHAR* ExpectedSounds[] = {TEXT("S_Selection"), TEXT("S_Move"), TEXT("S_Attack"), TEXT("S_WeaponImpact"), TEXT("S_Destruction"), TEXT("S_ConstructionComplete"), TEXT("S_ProductionComplete"), TEXT("S_ReclaimComplete"), TEXT("S_Victory"), TEXT("S_Defeat")};
 for (int32 I = 0; I <= static_cast<int32>(ERTSPresentationEventKind::Defeat); ++I)
 {
  auto R = Request(200000 + I); R.Kind = static_cast<ERTSPresentationEventKind>(I);
  auto Receipt = F.Presentation->Present(R);
  TestTrue(TEXT("Every cue accepted with real visual/audio"), Receipt.bAccepted && Receipt.bAudioAvailable && Receipt.bVisualSpawned);
  float Expected = I <= 2 ? T.AcknowledgementLifetimeSeconds : I == 3 ? T.WeaponLifetimeSeconds : I == 4 ? T.DestructionLifetimeSeconds : I <= 7 ? T.CompletionLifetimeSeconds : T.MatchLifetimeSeconds;
  auto* Effect = F.Effect(R.StableSourceId);
  if (!TestNotNull(TEXT("Receipt has matching actual actor"), Effect)) continue;
  TestTrue(TEXT("Actual cue lifetime resolves its configured default"), FMath::IsNearlyEqual(Effect->GetRequest().LifetimeSeconds, FMath::Max(.05f, Expected)) && FMath::IsNearlyEqual(Effect->GetLifeSpan(), FMath::Max(.05f, Expected)));
  auto* Audio = Effect->FindComponentByClass<UAudioComponent>();
  if (TestNotNull(TEXT("Native audio component"), Audio) && TestNotNull(TEXT("Real resolved sound"), Audio->Sound.Get()))
  { Sounds.Add(Audio->Sound.Get()); TestEqual(TEXT("Correct raw sound for typed cue"), Audio->Sound->GetName(), FString(ExpectedSounds[I])); TestTrue(TEXT("Configured audio volume"), FMath::IsNearlyEqual(Audio->VolumeMultiplier, FMath::Clamp(T.VolumeMultiplier,0.f,1.f))); }
  TestTrue(TEXT("Acknowledgements use target, other effects source"), Effect->GetActorLocation().Equals(I == 1 || I == 2 ? R.TargetWorldLocation : R.SourceWorldLocation));
  TestFalse(TEXT("Presentation has no collision authority"), Effect->GetActorEnableCollision());
 }
 TestEqual(TEXT("Ten distinct raw cue assets"), Sounds.Num(), 10);
 auto Long = Request(210000); Long.LifetimeSeconds = 100;
 F.Presentation->Present(Long);
 auto* LongEffect = F.Effect(Long.StableSourceId);
 if (TestNotNull(TEXT("Long request has a real effect"), LongEffect)) TestTrue(TEXT("Actual explicit lifetime is clamped to match cap"), FMath::IsNearlyEqual(LongEffect->GetLifeSpan(), FMath::Max(.05f, T.MatchLifetimeSeconds)));
 return !HasAnyErrors();
}

// REQUIRED: real mesh configuration is inspected; this is not rendered/audible certification.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEA162Geometry, "Task0168.Headless.GameEngineBench.UE0162.Geometry", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FEA162Geometry::RunTest(const FString&)
{
 FFixture F(*this); if (!F.Presentation) return false;
 auto R = Request(220000); F.Presentation->Present(R); auto* Effect = F.Effect(R.StableSourceId);
 if (!TestNotNull(TEXT("Beam actor"), Effect)) return false;
 TArray<UStaticMeshComponent*> Meshes; Effect->GetComponents(Meshes);
 TestEqual(TEXT("Three native cue mesh components"), Meshes.Num(), 3);
 bool FoundBeam = false; bool FoundImpact = false;
 for (auto* Mesh : Meshes)
 {
  TestNotNull(TEXT("Genuine engine mesh bound"), Mesh->GetStaticMesh().Get());
  TestFalse(TEXT("Effect cannot obstruct navigation"), Mesh->CanEverAffectNavigation());
  TestEqual(TEXT("Effect has no mesh collision"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
  TestNotNull(TEXT("Cue dynamic color material"), Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)));
  if (Mesh->GetRelativeLocation().Equals((R.TargetWorldLocation-R.SourceWorldLocation)*.5f + FVector(0,0,70)))
  {
   FoundBeam = true;
   TestTrue(TEXT("Beam spans source to target"), FMath::IsNearlyEqual(Mesh->GetRelativeScale3D().X, 5.0));
   const FVector ExpectedAxis = (R.TargetWorldLocation - R.SourceWorldLocation).GetSafeNormal();
   const FVector ActualAxis = Mesh->GetRelativeRotation().Vector().GetSafeNormal();
   TestTrue(TEXT("Beam fixture is not aligned with a world axis"), FMath::Abs(ExpectedAxis.X) > .1 && FMath::Abs(ExpectedAxis.Y) > .1);
   TestTrue(TEXT("Beam long axis aligns with source-target displacement"), FMath::Abs(FVector::DotProduct(ExpectedAxis, ActualAxis)) > .9999);
  }
  if (Mesh->GetRelativeLocation().Equals(R.TargetWorldLocation-R.SourceWorldLocation + FVector(0,0,65))) FoundImpact = true;
 }
 TestTrue(TEXT("Source-target beam placement"), FoundBeam); TestTrue(TEXT("Impact at target"), FoundImpact);
 // Reuse an actor to prove old beam transforms/visibility are reset, not accumulated.
 R.Kind = ERTSPresentationEventKind::MoveAcknowledged; Effect->Configure(R, nullptr, .4f);
 int32 Visible = 0; for (auto* Mesh : Meshes) if (Mesh->IsVisible()) ++Visible;
 TestEqual(TEXT("Move cue hides the old third beam component"), Visible, 2);
 TestTrue(TEXT("Reuse updates stored typed request"), Effect->GetRequest().Kind == R.Kind);
 return !HasAnyErrors();
}

// Timer callbacks must advance on distinct engine frames (FTimerManager guards repeat ticks).
class FEA162ExpiryCommand final : public IAutomationLatentCommand
{
 FAutomationTestBase& Test;
 TUniquePtr<FFixture> F;
 TWeakObjectPtr<ARTSTransientEffect> Effect;
 int32 Frames = 0;
public:
 explicit FEA162ExpiryCommand(FAutomationTestBase& InTest) : Test(InTest), F(MakeUnique<FFixture>(Test))
 {
  if (!F->Presentation) return;
  auto R = Request(230000); R.LifetimeSeconds = .01f; F->Presentation->Present(R);
  Effect = F->Effect(R.StableSourceId);
  if (Test.TestNotNull(TEXT("Expiring effect"), Effect.Get()))
   Test.TestTrue(TEXT("Actor minimum lifespan"), FMath::IsNearlyEqual(Effect->GetLifeSpan(), .05f, .005f));
 }
 virtual bool Update() override
 {
  if (!F->Presentation) return true;
  F->World->GetTimerManager().Tick(.1f);
  if (++Frames < 8) return false;
  Test.TestTrue(TEXT("Actor actually expires"), !Effect.IsValid() || Effect->IsActorBeingDestroyed());
  Test.TestEqual(TEXT("Expired weak entries pruned"), F->Presentation->GetSnapshot().ActiveEffectCount, 0);
  F->Presentation->Present(Request(230001)); auto* Next = F->Effect(230001);
  if (Test.TestNotNull(TEXT("Second effect"), Next)) Next->Destroy();
  Test.TestEqual(TEXT("External actor destruction also pruned"), F->Presentation->GetSnapshot().ActiveEffectCount, 0);
  Test.TestEqual(TEXT("Expiry does not count as overflow eviction"), F->Presentation->GetSnapshot().EvictedEffectCount, 0);
  F.Reset(); return true;
 }
};
// REQUIRED: lifespan/invalid-actor pruning frees capacity without fake eviction.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEA162Expiry, "Task0168.Headless.GameEngineBench.UE0162.Expiry", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FEA162Expiry::RunTest(const FString&)
{
 ADD_LATENT_AUTOMATION_COMMAND(FEA162ExpiryCommand(*this));
 return true;
}

// REQUIRED: repeated observation is idempotent and switching sources detaches old selection.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEA162Selection, "Task0168.Headless.GameEngineBench.UE0162.Selection", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FEA162Selection::RunTest(const FString&)
{
 FFixture F(*this); if (!F.Presentation) return false;
 if (!TestNotNull(TEXT("Engine owns the local player fixture"), GEngine)) return false;
 auto* PlayerA = NewObject<ULocalPlayer>(GEngine); auto* PlayerB = NewObject<ULocalPlayer>(GEngine);
 auto* A = NewObject<URTSSelectionSubsystem>(PlayerA); auto* B = NewObject<URTSSelectionSubsystem>(PlayerB);
 auto* Unit = F.Unit(240000, RTSTeams::Player, FVector(100, 50, 0));
 if (!TestNotNull(TEXT("Selectable unit"), Unit)) return false;
 ARTSCombatUnit* Units[] = {Unit};
 F.Presentation->ObserveSelection(*A); F.Presentation->ObserveSelection(*A);
 A->ReplaceWith(Units);
 TestEqual(TEXT("One selection after repeated binding"), Count(F.Presentation->GetSnapshot(), ERTSPresentationEventKind::SelectionAcknowledged), 1);
 F.Presentation->ObserveSelection(*B);
 A->Clear(); A->ReplaceWith(Units);
 TestEqual(TEXT("Old selection detached"), Count(F.Presentation->GetSnapshot(), ERTSPresentationEventKind::SelectionAcknowledged), 1);
 B->ReplaceWith(Units);
 TestEqual(TEXT("Old selection source remains nonempty for replacement check"), A->GetLivingUnits().Num(), 1);
 TestEqual(TEXT("New selection source is populated before stale-source events"), B->GetLivingUnits().Num(), 1);
 A->Clear(); A->ReplaceWith(Units);
 TestEqual(TEXT("Populated replacement ignores old-source events"), Count(F.Presentation->GetSnapshot(), ERTSPresentationEventKind::SelectionAcknowledged), 2);
 B->Clear();
 TestEqual(TEXT("New source once; empty selection silent"), Count(F.Presentation->GetSnapshot(), ERTSPresentationEventKind::SelectionAcknowledged), 2);
 A->Deinitialize(); B->Deinitialize();
 return !HasAnyErrors();
}

// REQUIRED: real command transaction and weapon tick plus exactly-once match reports compose.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEA162GameplaySequence, "Task0168.Headless.GameEngineBench.UE0162.GameplaySequence", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FEA162GameplaySequence::RunTest(const FString&)
{
 FFixture F(*this); if (!F.Presentation) return false;
 auto* Attacker = F.Unit(250000, RTSTeams::Player, FVector::ZeroVector);
 auto* Target = F.Unit(250001, RTSTeams::Enemy, FVector(250,0,0));
 if (!TestNotNull(TEXT("Attacker"), Attacker) || !TestNotNull(TEXT("Target"), Target)) return false;
 F.Presentation->ObserveCombatUnit(*Attacker); F.Presentation->ObserveCombatUnit(*Attacker);
 ARTSCombatUnit* Units[] = {Attacker};
 auto* Commands = F.World->GetSubsystem<URTSCommandSubsystem>();
 TestEqual(TEXT("Real command accepted"), Commands->IssueAttack(Units, *Target).GetAcceptedCount(), 1);
 Attacker->GetOrderComponent()->TickComponent(.1f, LEVELTICK_All, nullptr);
 auto* Match = F.World->GetSubsystem<URTSMatchSubsystem>();
 TestTrue(TEXT("Production report accepted"), Match->ReportUnitProduced(RTSTeams::Player, 250002).bAccepted);
 TestFalse(TEXT("Duplicate producer rejected upstream"), Match->ReportUnitProduced(RTSTeams::Player, 250002).bAccepted);
 Match->ReportUnitLost(RTSTeams::Enemy, 250003);
 const auto Reclaimed = Match->ReportMaterialsReclaimed(RTSTeams::Player, 250004, 125);
 TestTrue(TEXT("Positive reclaim report is accepted by real match owner"), Reclaimed.bAccepted);
 TestEqual(TEXT("Accepted reclaim report retains requested amount"), Reclaimed.Amount, 125);
 ARTSCombatUnit* EnemyUnits[] = {Target};
 const auto EnemyAttack = Commands->IssueAttack(EnemyUnits, *Attacker);
 TestEqual(TEXT("Enemy command fixture reaches accepted hostile operation"), EnemyAttack.GetAcceptedCount(), 1);
 auto S = F.Presentation->GetSnapshot();
 for (auto Kind : {ERTSPresentationEventKind::AttackAcknowledged, ERTSPresentationEventKind::WeaponImpact, ERTSPresentationEventKind::ProductionCompleted, ERTSPresentationEventKind::Destruction, ERTSPresentationEventKind::ReclaimCompleted})
  TestEqual(TEXT("Composed authoritative event presented exactly once"), Count(S, Kind), 1);
 for (const auto& Receipt : S.RecentReceipts)
  if (Receipt.Request.Kind == ERTSPresentationEventKind::WeaponImpact)
  { TestEqual(TEXT("Weapon identity"), Receipt.Request.StableSourceId, 250000); TestEqual(TEXT("Weapon target"), Receipt.Request.StableTargetId, 250001); TestTrue(TEXT("Positive actual damage magnitude"), Receipt.Request.Magnitude > 0); }
 for (const auto& Receipt : S.RecentReceipts)
 {
  if (!Receipt.bAccepted) continue;
  if (Receipt.Request.SourceKind == ERTSPresentationSourceKind::Command)
   TestTrue(TEXT("Only player commands receive acknowledgement"), Receipt.Request.TeamId == RTSTeams::Player);
  if (Receipt.Request.Kind == ERTSPresentationEventKind::ReclaimCompleted)
  {
   TestEqual(TEXT("Reclaim cue keeps source identity"), Receipt.Request.StableSourceId, Reclaimed.StableSourceId);
   TestEqual(TEXT("Reclaim cue keeps accepted source event sequence"), Receipt.Request.SourceEventSequence, static_cast<int64>(Reclaimed.EventSequence));
   TestTrue(TEXT("Reclaim cue preserves the accepted material amount"), FMath::IsNearlyEqual(Receipt.Request.Magnitude, static_cast<float>(Reclaimed.Amount)));
  }
 }
 return !HasAnyErrors();
}

// REQUIRED: typed producer adapters, structure location and losing-team outcome mapping.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEA162ProducerAdapters, "Task0168.Headless.GameEngineBench.UE0162.ProducerAdapters", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FEA162ProducerAdapters::RunTest(const FString&)
{
 FFixture F(*this); if (!F.Presentation) return false;
 auto* Structures = F.World->GetSubsystem<URTSStructureSubsystem>();
 FRTSStructureSnapshot Structure; Structure.StableStructureId = 260000; Structure.TeamId = RTSTeams::Enemy; Structure.WorldLocation = FVector(500,200,30);
 // Adapter injection exercises public typed event seams; legality remains supplied and is not scored here.
 Structures->OnConstructionCompleted().Broadcast(Structure); Structures->OnConstructionCompleted().Broadcast(Structure);
 Structures->OnStructureDestroyed().Broadcast(Structure);
 FRTSMatchEventReceipt End; End.bAccepted = true; End.Kind = ERTSMatchEventKind::MatchResolved; End.TeamId = RTSTeams::Enemy; End.StableSourceId = 260000; End.EventSequence = 50;
 auto* Match = F.World->GetSubsystem<URTSMatchSubsystem>(); Match->OnMatchEventRecorded().Broadcast(End);
 auto S = F.Presentation->GetSnapshot();
 TestEqual(TEXT("Construction deduplicated"), Count(S, ERTSPresentationEventKind::ConstructionCompleted), 1);
 TestEqual(TEXT("Enemy defeat produces player victory"), Count(S, ERTSPresentationEventKind::Victory), 1);
 for (const auto& Receipt : S.RecentReceipts) if (Receipt.bAccepted && Receipt.Request.Kind == ERTSPresentationEventKind::Victory)
  TestTrue(TEXT("Outcome anchored to latest destruction for losing faction"), Receipt.Request.SourceWorldLocation.Equals(Structure.WorldLocation));
 End.TeamId = RTSTeams::Player; End.EventSequence++; Match->OnMatchEventRecorded().Broadcast(End);
 TestEqual(TEXT("Player loss produces defeat"), Count(F.Presentation->GetSnapshot(), ERTSPresentationEventKind::Defeat), 1);
 // Turret observation uses actual producer component, without requiring a second combat simulation.
 auto* Turret = F.World->SpawnActor<ARTSStructure>();
 FRTSStructureDefinition Definition; Definition.Type = ERTSStructureType::DefensiveTurret;
 if (TestNotNull(TEXT("Turret fixture"), Turret))
 {
  Turret->Configure(ERTSStructureType::DefensiveTurret, 260002, RTSTeams::Player, Definition, 100);
  auto* Combat = Turret->GetTurretCombatComponent();
  if (TestNotNull(TEXT("Native turret weapon producer"), Combat))
  {
   F.Presentation->ObserveStructure(*Turret); F.Presentation->ObserveStructure(*Turret);
   FRTSWeaponEvent Shot; Shot.SourceKind = ERTSWeaponSourceKind::DefensiveTurret; Shot.SourceTeamId = RTSTeams::Player; Shot.StableSourceId = 260002; Shot.SourceShotSequence = 3; Shot.AppliedDamage = 19;
   Combat->OnWeaponFired().Broadcast(Shot);
   auto After = F.Presentation->GetSnapshot();
   TestEqual(TEXT("Turret observed once"), Count(After, ERTSPresentationEventKind::WeaponImpact), 1);
   if (TestTrue(TEXT("Turret receipt retained"), !After.RecentReceipts.IsEmpty())) TestTrue(TEXT("Turret source preserved"), After.RecentReceipts.Last().Request.SourceKind == ERTSPresentationSourceKind::TurretWeapon);
  }
 }
 return !HasAnyErrors();
}

// REQUIRED: only the teardown guarantees implemented by the source oracle are asserted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEA162Teardown, "Task0168.Headless.GameEngineBench.UE0162.Teardown", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FEA162Teardown::RunTest(const FString&)
{
 FFixture F(*this); if (!F.Presentation) return false;
 auto* Match = F.World->GetSubsystem<URTSMatchSubsystem>();
 Match->ReportUnitProduced(RTSTeams::Player, 270000);
 TestEqual(TEXT("Subscription works before teardown"), F.Presentation->GetSnapshot().AcceptedRequestCount, 1);
 F.Presentation->Deinitialize();
 auto Cleared = F.Presentation->GetSnapshot();
 TestEqual(TEXT("Teardown clears retained receipts"), Cleared.RecentReceipts.Num(), 0);
 TestEqual(TEXT("Teardown clears keys"), Cleared.TrackedEventCount, 0);
 TestEqual(TEXT("Teardown clears effect tracking"), Cleared.ActiveEffectCount, 0);
 Match->ReportUnitProduced(RTSTeams::Player, 270001);
 FRTSStructureSnapshot Structure; Structure.StableStructureId = 270002; Structure.TeamId = RTSTeams::Player;
 F.World->GetSubsystem<URTSStructureSubsystem>()->OnConstructionCompleted().Broadcast(Structure);
 TestEqual(TEXT("Global producers detached after teardown"), F.Presentation->GetSnapshot().RecentReceipts.Num(), 0);
 return !HasAnyErrors();
}
#endif

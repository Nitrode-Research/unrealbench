#include "Combat/RTSHealthComponent.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
// Independent component gate: connected match failures must not obscure missing health behavior.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSTripletHealth, "Task0168.Headless.RTS.Triplet.Health.Lifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRTSTripletHealth::RunTest(const FString&)
{
    URTSHealthComponent* Health = NewObject<URTSHealthComponent>();
    if (!TestNotNull(TEXT("Native health fixture exists"), Health)) return false;
    Health->AddToRoot();
    int32 Changes = 0, Deaths = 0;
    Health->OnHealthChanged().AddLambda([&](const FRTSHealthSnapshot&) { ++Changes; });
    Health->OnDeath().AddLambda([&](ARTSCombatUnit*) { ++Deaths; });
    Health->ConfigureMaximumHealth(240.f, true);
    TestEqual(TEXT("Configured health restores"), Health->GetSnapshot().CurrentHealth, 240.f);
    TestFalse(TEXT("Zero damage is rejected"), Health->ApplyDamage(0).bAccepted);
    TestFalse(TEXT("Negative damage is rejected"), Health->ApplyDamage(-5).bAccepted);
    TestEqual(TEXT("Rejected damage is silent"), Changes, 0);
    const auto First = Health->ApplyDamage(40);
    TestTrue(TEXT("Positive damage uses real health"), First.bAccepted && !First.bKilled && First.AppliedDamage == 40 && First.RemainingHealth == 200);
    Health->ConfigureMaximumHealth(150.f, false);
    TestEqual(TEXT("Nonrestoring reconfigure clamps current health"), Health->GetSnapshot().CurrentHealth, 150.f);
    const auto Lethal = Health->ApplyDamage(1000);
    TestTrue(TEXT("Overkill applies remaining health only"), Lethal.bKilled && Lethal.AppliedDamage == 150 && Lethal.RemainingHealth == 0);
    TestEqual(TEXT("Death emitted exactly once"), Deaths, 1);
    TestEqual(TEXT("Exactly two accepted changes emitted"), Changes, 2);
    TestFalse(TEXT("Dead component rejects repeated damage"), Health->ApplyDamage(3).bAccepted);
    Health->ConfigureMaximumHealth(300.f, true);
    TestTrue(TEXT("Reconfiguration cannot revive the dead component"), Health->GetSnapshot().bDead && Health->GetSnapshot().CurrentHealth == 0);
    TestEqual(TEXT("Repeat damage does not reemit death"), Deaths, 1);
    Health->OnHealthChanged().Clear(); Health->OnDeath().Clear(); Health->RemoveFromRoot();
    return !HasAnyErrors();
}
#endif

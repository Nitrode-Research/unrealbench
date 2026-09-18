# Delta50 test-only diagnosis

The root-authorized packet is `/tmp/ub-repair-runtime-121-130-20260915/recovery-20260916/test-only-123-delta50-first-failures`. All89 manifest member hashes and sizes were independently verified. Only frozen verifier code, already recorded public command/event fields, and curated stock engine logs were inspected. No candidate source or private stack was accessed; no local Unreal run occurred.

## Confirmed organizer-seat test overconstraint

GGPOPair stops after its correlated host observe, before any final ready/start command. The host explicitly accepted the organizer's offered seat0 and selected a character with that assignment. Its subsequent observation retains the same membership and nonempty assignment while reporting Role=organizer. The new role check incorrectly requires fighter even for this organizer-owned seat. The native manual-control transition has the same label restriction.

The separate `organizer-seat-role-review-v1.zip` corrects only this compatibility point. SHA256 `4975dbcdb697da4a1436f2d2aaa1133ce6e1b9448809ec59abe6e20d358f8234`. The known organizer may retain organizer label with current owned assignment; ordinary remote participants still require fighter. Current memberships, distinct assignments, inactivity and active/new-match witnesses remain. Independent constraints review approved this exact freeze; protected runtime remains pending.

## FreshOfflineReplay failure occurs before replay execution

The former repeated fixture loop is gone: the leaf completes in17.012 seconds. The child starts with unrelated seed7 and its only command, inspect-replay, returns successfully. The recorded artifact has the expected interrupted identity, two participants and seed9009, but only one stored frame. The existing check requires241 frames (frame0 plus240 confirmed pairs). The child never issues replay or replay-step.

This is evidence that the originating run exported frame0 only. It is not evidence that fresh replay initialization or playback is broken. The native authority test submits240 input request pairs; its generic accepted-input helper relies on subsequent native/history evidence and does not independently prove each pair confirmed. The packet does not contain native per-input assignment/membership/acceptance observations sufficient to identify why no confirmed history was exported.

## CompletedOutcome still lacks a valid live-battle completion witness

The recorded diagnostics are initial/final native frame0, timer60, phase0, trainingfalse, runner0, zero wins, both health10000 and no damage after600 submitted request pairs. Configuration remains one second, one round, zero start delay. The selected actor remains in the game instance's current world. The test consequently fails its unchanged actual round timer/winner lifecycle completion assertion.

The stock log records `ProcessServerTravel: VSInfo_PL` at engine frame43. The test then tears down its original `/Temp/Untitled_1` and reports failure at frame44; no intervening VSInfo map-load completion is present. The frozen continuation submits all600 pairs and calls the cached actor's Tick directly within one latent phase, without an ordinary world tick or game-thread yield. This exposes a concrete scheduling limitation: pending ordinary travel is not awaited before manual gameplay checks. Public contract permits live battle-world setup to remain pending after successful start has already established reproducible frame0.

The common native helper chooses the current world's ANightSkyGameState, otherwise the first ANightSkyGameState with two players. Neither that selection rule nor same-world equality proves the actor is the current room's authoritative simulation. Native tests also retain prestart assignment strings. These are test strategy risks; the packet does not prove actor replacement or token refresh occurred in CompletedOutcome. Choosing a different actor because it happens to advance, rewriting native state, treating600 submitted requests as600 confirmed pairs, or relaxing the winner/ledger checks would be unjustified.

## CombinedLifecycleFuzz and FuzzMetamorphicCampaign remain inactive

Both process drivers reach successful fixture association at native frame0/seed9009 and a fresh inactive snapshot. Their actual start replies remain inactive with a nonempty initialization diagnostic. The helper's prepared-match predicate changes from true to false at start; the recorded actor frame, selected native fighter class, seed and current worker world remain unchanged. That predicate compares several unrecorded configuration/identity conditions, so its false value does not identify which condition changed.

CombinedLifecycle records528 fresh start-observation attempts over30.031 seconds and fails at49 assertions/52.406 seconds. Fuzz records557 attempts over30.000 seconds. All recorded authority events remain active=false and manual_battle_match stays empty. The v4 manual tick suppression therefore cannot have fired in these runs. Ordinary worker callbacks and command responses continue; this is not evidence of a process hang. Combined's final ready commands use their freshly observed distinct assignments, and their correlated replies acknowledge those same assignments. The proposed stale-token risk is not observed at that boundary.

The stock logs show transient Entry map initialization with RoomWorkerGameMode and teardown at start, while the original worker world remains alive. The test-owned RoomWorkerGameMode is lobby-only (AGameStateBase); ordinary fixture battle construction is a separate path. The packet does not establish why this transient initialization failed to establish public active state, and no candidate sequence is inferred from it.

## Next evidence boundary

Only the organizer-seat defect is corrected. The other outcomes remain failures with unknown cause. A subsequent native test-only change should first separate public successful frame0 from pending live-world scheduling, refresh positive current owner observations without altering stale-owner negatives, and obtain a public/fixture-grounded association for any directly driven native actor. Failure-only diagnostics can record pending travel URLs, world/game-state/selected-actor identity relationships, current public fighter identities and actual confirmed frames. Any such change requires its own exact freeze and independent constraints review; existing assertions, input histories, outcome rules and bounds must remain substantive.

## Unresolved-case ledger after the native proposal

The final authorized native packet records 6 passes and 11 failures across exactly 17 registrations in498.047 seconds, without an outer timeout. The table records the existing failed run; neither an approved test correction nor this new native proposal is runtime success evidence.

| Failed registration | Recorded evidence and current status |
| --- | --- |
| CombinedLifecycleFuzz |49 assertions then requested-start deadline; all recorded public events inactive. Cause remains unknown. Native-only patch does not change this process driver. |
| CompletedOutcome |Actual timer/winner completion fails; cached native frame0/timer60 unchanged after600 requests. Stock travel was requested immediately before the synchronous loop. Native scheduling/association repair is proposed; actual completion remains unvalidated. |
| FreshOfflineReplay |Originating native/control checks fail and export has1 frame instead of241; child reaches inspection only. Setup loop is gone. Native scheduling repair is proposed; fresh playback correctness remains untested by this failed run. |
| FuzzMetamorphicCampaign |Requested-start deadline with public activity remaining false. Cause remains unknown; process driver is untouched by this native proposal. |
| GGPOPair |Confirmed organizer-owned-seat role overconstraint before final ready/start. Separate exact organizer correction was independently approved; protected runtime remains pending. |
| LifecycleAndSeatTransfer |Native controller records failed separate-process exit and missing valid scenario/assertion evidence. This final packet supplies no deeper causal witness for this leaf; unresolved. |
| RealBattleDelayedHistory |First native position remains-150000 versus independent-149000; confirmed edge remains0 rather than1. Native scheduling/association repair is proposed; all direct/control/history checks remain. |
| RenderedDVR |Original fighter/control ledger comparisons fail before rendered replay evidence can establish correctness. Shares the native240-pair fixture. Rendering/playback is not declared repaired. |
| SixProcessAuthority |Failed separate-process exit and missing valid scenario/assertion evidence in final packet. Deeper cause not established from authorized evidence; native proposal does not change this process driver. |
| WidgetControls |No native damage; seek20 returns0 and later UI/history checks fail. Native40-pair scheduling and weak widget reconnection are proposed; UI behavior remains unvalidated. |
| ZeroInputSuccessfulStartInterruption |The exported zero-input frame fails public-interface decoding. Immediate success/interruption/ordinary release witness is independent of the gameplay travel repair; its body remains unchanged and this failure remains open. |

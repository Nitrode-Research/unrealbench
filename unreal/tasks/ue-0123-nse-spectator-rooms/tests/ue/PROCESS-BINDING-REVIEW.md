# Process fixture binding lifetime correction

This is a test-owned fixture correction, separate from native travel, the compile-only name change, and diagnostics. No causal claim is made about delta50's inactive starts.

`FinishBattlePreparation` previously called public `BindBattle` while preparation remained pending, with no reentrancy guard. A permitted synchronous loading callback can call `BeginBattle`, whose same-actor pending path calls `FinishBattlePreparation` again, repeating that binding recursively. A callback can also replace the prepared actor/world or begin a new explicit preparation before the outer call returns; the old call then wrote completion/configuration flags for the newer setup without validating its lifetime.

The correction guards only the in-flight fixture binding, captures weak actor/world handles and the explicit preparation serial before dispatch, and revalidates the current prepared actor/world/serial afterward. Same-actor reentry returns without rebinding; a newer setup is left pending for the existing ordinary tick to finish. It neither suppresses initialization ticks nor resets the battle/room/configuration/history. `PreparedBattleMatches`, all Python and native assertions, decoder checks, start gates, pair counts and bounds are byte-identical.

The exact `PreparedBattleMatches` predicate still needs diagnostic evidence to identify which condition changed in delta50. This lifetime correction does not presume that it was exercised in that run. Host C++ parsing and exact unaffected-method comparison are validation only; protected UE compile/runtime remains required.

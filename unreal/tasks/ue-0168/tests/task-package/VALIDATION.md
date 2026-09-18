# Validation status

Windows Unreal 5.8 native diagnostic baselines completed on 2026-09-15 with one frozen task-owned verifier:

- Reference: build succeeded; all 68 headless groups passed, including all five retained full-match seeds.
- Stripped start: build succeeded; all 68 groups completed with 29 passes and 39 behavioral failures. Native automation exit 255 and its explicit failure termination were correctly classified as complete execution.
- Both runs matched the exact log and exported JSON identities without crashes, missing tests, duplicates, or infrastructure reasons. Reward remains null and reward_valid remains false for this headless subset.

Frozen package: `C:/ub/gr-20260915-02/t168v1`. Reference evidence: `C:/ub/gr-20260915-02/168v1r`. Start evidence: `C:/ub/gr-20260915-02/168v1s`. Hash-pinned coordinator receipt: `C:/ub/gr-20260915-02/168v1.json`. The earlier EnhancedInput linker failure remains preserved at `C:/ub/gv-20260915-01/168r`.

The evaluator now links EnhancedInput explicitly. The wrapper writes a native lane log separately from console output, exports automation JSON, and checks exact terminal records plus explicit normal/failing automation termination. Production fixture setup has a bounded missing-dependency failure, and its brownout observation requires a nonempty queue before indexing. Existing gameplay assertions, all 68 headless plus 5 rendered groups, and all start/solution gameplay bytes are unchanged. Twelve CPU wrapper regression cases pass.

The start failures include direct economy transactions and supply reservations, ownership/reclaim and match-state behavior, deployment and production prerequisites, and all strategy/full-match drivers. Supplied complementary gameplay accounts for the 29 passes. This establishes the composed stripped-start negative control; it does not establish isolated mutation sensitivity.

Use the task-owned entrypoint, not legacy runtime-module injection:

```
python verify.py --project <candidate-or-solution> --engine <UE-root> --output <fresh-output>
```

CPU-only diagnostic: add `--lane headless`; reward remains null. Render on a compatible host, optionally supplying platform arguments with `--render-arg=-dx12 --render-arg=-sm6`. No automatic GPU skip. Both lanes and exact nonempty manifests are mandatory. Compilation failures remain unattributed/incomplete until oracle and candidate failures are separated.

Outstanding gates: all five rendered groups on a compatible graphics host, targeted cross-system mutations, repeated reference timing, and actual Linux/Harbor injection. This host exposes only basic display adapters. No rendered execution, full reward, mutation campaign, Harbor result, or normal-human-match pacing acceptance is claimed by these repair baselines.

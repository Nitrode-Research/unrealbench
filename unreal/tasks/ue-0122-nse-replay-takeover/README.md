# Replay takeover with independent branch recording

Website task **122**, sourced from `ue_task_0122_nse_replay_takeover` at UnrealBench commit `1ff51f9695b8bc0220296bd0f861f3e6f80c988d`.
The package name identifies the task independently of its runtime platform.

Run from the repository root:

```sh
uv run --locked harbor run -p unreal/tasks/ue-0122-nse-replay-takeover --agent oracle -e docker
```

The Docker environment uses Unreal Engine 5.8.2. Hidden tests and fixture overlays
are injected after the agent phase; the oracle follows the source editable paths and protected-path exclusions.
The verifier requires all 15 declared tests across 1 suite(s).
Reward is the fraction passing only after every suite completes with its exact
test inventory. A compilation failure earns zero; incomplete execution or an
infrastructure failure does not emit a valid reward.

Rendering required: **false**. Rendered tasks need a working Vulkan
device/driver exposed to the container; this package does not provision one.
See `QC.md` for the validation boundary and `SOURCE_RECEIPT.json` for provenance.

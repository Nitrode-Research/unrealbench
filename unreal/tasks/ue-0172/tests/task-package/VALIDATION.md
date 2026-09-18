# v3 validation

Valid synthetic story and initialized session fixtures; typed save-corruption probes do not require the private players wire key. Source preservation remains one of 45 checks, HUD is excluded. Existing grades retain their historical evaluator revisions.

Native results are recorded separately with platform/image receipts; the records below remain historical.

# Current scoring policy

Scoring revision `ue-0172-scored-preservation-v2`: 45 equally weighted checks (5 preparation, 39 headless, and 1 supplied-code preservation check). Changes to protected C++ source or supplied functions, including formatting/comments, fail only the preservation check and do not block behavioral testing. Immutable data, scripts, build settings, and execution validity remain required. HUD rendering is excluded. Existing 44-check saved-report grades remain historical; rejected submissions require native reevaluation.

No new native run is claimed. The following validation records are historical.

# Current scoring revision

Revision `ue-0172-no-hud-v1` excludes the rendered HUD check and scores 44 preparation/headless checks. The runner and wrapper accept completed partial credit and reject incomplete, crashed, or invalid reports. The validation records below describe the earlier scoring policy and are preserved as historical evidence; this revision does not claim a new Unreal Engine run.

# Validation status — UE0172

Authored package, not released or difficulty-calibrated.

| Gate | Evidence / status |
|---|---|
| Coherent reference |5833ed10 through frozen0163; all triplet reference file hashes identical |
| Exact half partition | Passed: A intersection B empty; A union B equals full174 hunks |
| Static package / Python syntax / test mapping | Passed;39 headless+1rendered registrations and5CPU checks |
| Immutable content/functions | Passed against both snapshots; nonempty manifests, counterpart methods protected |
| LFS/no-leak | No pointer assets, raw Characters/animations, completed source-map outputs or packaged smoke driver in starts |
| CPU preparation reference |5/5 on the common identical reference, task-local clean driver |
| CPU preparation this start |5/5; supplied A is expected to pass for B-only task |
| Native reference | PASS: clean compile, fresh generation, 5/5 preparation and 39/39 headless groups; 44/44 diagnostic checks |
| Native stripped start compiles / fails | PASS: clean compile and fresh generation; 5/5 preparation, 20/39 headless pass and 19 expected failures; 25/44 diagnostic checks |
| New composed mutation campaign | NOT RUN; old component campaigns are not inherited |
| Rendered HUD | NOT RUN; requires compatible real renderer |
| Actual Linux / Harbor | NOT RUN; current task-local wrapper explicitly Windows installedUE |

All CPU diagnostic reports keep reward=null, reward_valid=false. They are not
native baseline evidence. Native test bodies/API composition may still need
fixture repairs; do not hide such repairs as model failures or alter the gameplay
reference simply to satisfy an expectation. UI readback is bounded evidence, not
complete presentation coverage or full source parity.

## Coordinated native handoff

Use a new SHORT output path outside the submission, e.g. a coordinator-owned
C:/ub/validation directory. Run verification/verify.py with --project solution,
--output <new path>, --engine <pinnedUEroot>. Default includes generation and both
lanes. --headless-only helps isolate CPU/gameplay errors but is never full reward.
Then run the exact start. Do not start another UBT/editor while an existing native
coordinator owns the machine. No native process was launched during authoring.

First validate reference compilation, generation and all39headless groups; review
timeouts/ensures/registration incompleteness rather than converting them to0.
Validate rendered HUD separately on a compatible renderer. Only then freeze a
targeted mutation set spanning world import, pair orchestration, story ordering,
inventory transfer, continuous travel/save and real UI refresh. Run actual Harbor
oracle/start controls before any paid difficulty claim.

Public action semantics are in ACTION_CONTRACT.md/instruction.md. Do not compare
exact feedback copy. Source/previous diagnostics are modular; the continuous
two-area test is mandatory and uses actual submitted components throughout.

## Authoring evidence

Reproducible local scripts: build_triplet.py, finalize_packages.py, audit.py and
finish_handoff.py in mint/.task-worktrees/game-triplets/foundations. Original
source, tasks152–163, shared runners and registries were not modified. The removed
inherited packaged diagnostic copies are recoverable in excluded-diagnostics/
outside all task packages. No commits or pushes.
# Reconciled input seal — 2026-09-15

This revision retains the intentional canonical source/evaluator edits and adds
the task-local wrapper repairs documented in PROVENANCE.md. Clean CPU preparation
passes 5/5 on both start and solution. Protected file/function guards pass on both
sides; the 89 B removal records and all 45 check identities are unchanged.

The shared serial coordinator validated this exact combined revision at
`C:/ub/gr-20260915-02/t172v1`. Reference evidence is in `172v1r` and stripped-start
evidence in `172v1s`, with controller hashes in `172v1.json`. Both runs compiled,
generated the fresh map, and completed all 44 preparation/headless checks with
no infrastructure errors or blocked stages. Reference passed 44/44; start passed
25/44 (5 preparation plus 20 headless) and failed 19 headless groups.

Start failures cover stripped story, interactions, progression, inventory/save,
the continuous two-area run, and story-driven generated composition. Its supplied
world preparation is expected to pass. Every native group remains registered;
no generation, integrity check, or assertion was bypassed.

Rendered validation remains unavailable on this host. Both reports retain reward
null, reward_valid false, and diagnostic-only status. Headless completion is not
full coverage of the 45 required groups. Mutation campaigns, repeated timing,
actual Linux/Harbor, and cross-Python comparison certification remain unrun.
Older authoring/baseline evidence remains scoped to its recorded input hashes.

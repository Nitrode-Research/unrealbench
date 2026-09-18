# Validation status — UE0171

## Reconciled revision, 2026-09-15

This candidate preserves the user's supplied `ALinkSceneLayout::Find` null-world
guard and `.Get()` test conversions. Current protected hashes and shifted removal
ranges pass independent reconstruction; all 85 removal records and 1,273 removed
implementation LOC remain intact. All three current triplet solution trees are
identical and their 174-record partition remains exact and disjoint.

The user's matrix-only absolute tolerance `1e-12` is explicitly accepted as a
new evaluator revision, retaining exact counts, IDs, record shape and nonmatrix
policy values. The original captured fixture is preserved. This supersedes the
earlier local exact-fixture correction; it is not a cross-runtime certification.
Local checks use the UE 5.8 bundled Windows Python 3.11.8.

The reconciled native pair is recorded at `C:/ub/gr-20260915-02/171v2r` and
`171v2s`, using frozen package `t171v2` and controller receipt `171v2.json`.

| Reconciled baseline | Preparation | Headless | Execution evidence |
|---|---:|---:|---|
| Reference | 5/5 passed | 39/39 passed | Compiled, freshly generated scene, all 44 diagnostic records |
| Stripped start | 0/5 passed | 10 passed, 29 failed | Compiled, all 44 diagnostic records; generation explicitly blocked by failed submitted preparation |

Both native processes completed without crash/ensure markers or infrastructure
errors. The start's preparation scripts raise the expected `NotImplementedError`;
missing source geometry, paired runtime, camera/navigation and dependent game
flows fail ordinary assertions or terminate at their bounded timeouts. Supplied
interaction, story and save-rule tests remain among its ten passing groups.
No failed test was removed or relaxed to obtain these results.

Canonical inputs and frozen source/evaluator hashes remained unchanged throughout
this pair. Both runs remain diagnostics with `reward=null`, `reward_valid=false`
and incomplete full-required coverage. The start's blocked generation is not a
successful pipeline stage. Rendered HUD, the new mutation campaign, repeated
timing trials and actual Linux/Harbor validation remain outstanding. Full reward
is not certified. Earlier `171v1r` used different source/evaluator bytes; its
44/44 result is retained as history and is not the basis for this revision.

Focused local checks additionally cover eleven numerical-policy boundaries,
native engine/console log routing, and the blocked/headless reward guards.

The original authoring status below is retained as historical context.

Authored package, not released or difficulty-calibrated.

| Gate | Evidence / status |
|---|---|
| Coherent reference |5833ed10 through frozen0163; all triplet reference file hashes identical |
| Exact half partition | Passed: A intersection B empty; A union B equals full174 hunks |
| Static package / Python syntax / test mapping | Passed;39 headless+1rendered registrations and5CPU checks |
| Immutable content/functions | Passed against both snapshots; nonempty manifests, counterpart methods protected |
| LFS/no-leak | No pointer assets, raw Characters/animations, completed source-map outputs or packaged smoke driver in starts |
| CPU preparation reference |5/5 on the common identical reference, task-local clean driver |
| CPU preparation this start |0/5; scene restoration is absent, so all five fail |
| Native reference | NOT RUN |
| Native stripped start compiles / fails | NOT RUN |
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

# Release validation

All 14 agreed tasks are included: 121-125, 131-134, 164, 168, 169, 171 and 172.
No task is silently removed to make a smaller release pass.
Task 123 is deferred at the owner's request. Its source remains available, but
it is excluded from current qualification; work continues on the other 13 tasks.

This is a candidate, not a certified benchmark release. RELEASE_REPORT.json
contains the resource/image inventory and marks all task execution evidence
as not certified. Structural checks do not establish that tasks are solvable
or that their reward functions match their instructions.

Before publishing the first stable version, record for each exact task hash:

1. Reference-solution run reaching its specified full score.
2. No-op run reaching its independently reviewed expected baseline.
3. Full agent execution and verifier completion, with provider/model/version.
4. Regrade of the captured output with unchanged source-job bytes.
5. Repeated-run checks, and rendered/multiplayer verification where required.
6. Host requirements, image digests, runtime and evidence file hashes.

The omitted reference files for tasks 121-125 have been restored from their
pinned upstream sources, and all task engine images are pinned by digest.
The wrappers for tasks 168, 169 and 171 now request both native lanes.
Task 172 uses v0.5.0 (`ue-0172-valid-fixtures-v3`): 45 required checks,
corrected fixtures, and no HUD pixel scoring. Its complete scoring path runs
headlessly; the scoring receipt preserves prior evaluator revisions.
Task 172's native oracle and regrade each passed 45/45 checks. Its unchanged
starter passed 26/45, and the regrade preserved the source job. The
[receipt](validation/task-172-v0.5.0.json) records all tested task-file hashes
and native report hashes. No model-agent difficulty calibration is claimed.
Qualification of the remaining tasks is ongoing; absence of a listed defect
is not a pass.

Tasks 122, 131, 132, 133, 134 and 164 each reached reward 1 for the reference
solution and regrade, and reward 0 for the unchanged starter. Each regrade
preserved its source job. The [CPU-task receipt](validation/cpu-tasks.json)
includes tested hashes and the subsequent documentation-only corrections.

Tasks 121 and 125 reached oracle/regrade reward 1 and unchanged-starter reward
0 for their 11 and 23 scored checks respectively. Task 124 v0.1.2 reached
oracle/regrade reward 1 for all 36 checks, including its peer and rendered
suites, and unchanged-starter reward 0. A separate native regrade against
the exact final v0.1.3 package also reached reward 1. All of these regrades
preserved the source jobs. The [receipt](validation/tasks-121-124-125.json)
records tested hashes and package differences; task 125 has only a subsequent
QC documentation correction. Task 121's separate visual acceptance and
task 125's optional integration checks are not certified by these results.

Tasks 168 and 169 v0.2.1 repair a UE5.8 fixture compilation incompatibility.
Task 171 v0.2.1 corrects the native automation exit protocol; its reference
solution passed all 45 checks with reward 1. Task 168 passed all 68 headless
checks but its Vulkan renderer crashed during startup, before any of the five
rendered checks ran. It produced no valid reward. The allocation failure does
not establish that the GPU lacks physical VRAM. Task 169 and native follow-up
checks remain in progress. The [receipt](validation/tasks-168-169-171.json)
records exact tested packages and current limitations. Task 123 remains
deferred, including its known incomplete oracle result.

The portable installation/regrade fixture passed with a fresh HOME and Python
environment on an existing Linux Docker host. No Nitrode or model credentials
were configured for the test, and the smoke requires no AWS access.
Oracle and regrade rewards were 1, and the
source job remained unchanged. The [installation receipt](validation/installation-smoke.json)
identifies the tested source revision. This does not certify all Unreal tasks
or installation on a fresh operating system.

The first public UnrealBench snapshot also passed its Ubuntu 24.04 GitHub
Actions workflow: locked installation, all 14 package checks, Docker host
validation, and oracle/regrade smoke with rewards 1 and unchanged source.
A complete anonymous Git download and unauthenticated README access were
verified. The [public-installation receipt](validation/public-installation.json)
links the successful workflow and records the exact helper hashes. This smoke
does not execute Unreal tasks or require AWS/model credentials.

Some visual checks have additional grading requirements: the deferred task 123's room UI
and rendered checks require independent image observations, and task 124 has
a review fallback for alternative HUD layouts. Task 125's optional integration
tests also use an independent reviewer; they are outside its declared 23-check
score. Task 124 v0.1.3 withholds reward when its independent-review deadline
expires; its [manual workflow](unreal/tasks/ue-0124-nse-relay-combat/PUBLIC_REVIEW.md)
documents the remaining transport and timing limits. No automated reviewer is
provided. Missing review infrastructure must not be interpreted as evidence
that a submission is incorrect.

Apache-2.0 has been selected and included for Nitrode-owned code and task content;
existing third-party terms remain in effect. The local file inventory has been
reviewed and the portable installation smoke passed. The repository is public
and anonymous download has been verified. The immutable release-candidate tag
will follow the remaining native validation receipts.

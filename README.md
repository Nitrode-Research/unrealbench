# UnrealBench

Fourteen Unreal Engine coding tasks for agents, packaged for Harbor. Run on
your own Linux/Docker infrastructure with your own model credentials.

All 14 task sources are included. The [operationally certified](CERTIFICATION.md)
13-task profile excludes task 123, whose source remains available but deferred.
Certification means that each included native verifier starts, executes all of
its declared automated tests, and produces a valid result. It does not require
full model-agent runs or claim model pass rates. See [validation status](VALIDATION.md)
and the generated [task catalog](CATALOG.md).

## Install

Use Linux x86-64 with Git, Docker Engine, Docker Compose, and Python with venv.
Your user must be able to run `docker info`. The installation smoke needs
2 CPUs, 4 GiB RAM and 10 GiB free disk; Unreal tasks require substantially more.
The task checkout occupies approximately 5.2 GiB, separate from engine images,
build caches and job artifacts.

```sh
python3 -m venv "$HOME/.local/share/unrealbench-uv"
"$HOME/.local/share/unrealbench-uv/bin/pip" install 'uv==0.12.13'
export PATH="$HOME/.local/share/unrealbench-uv/bin:$PATH"
git clone --branch v0.1.0 https://github.com/Nitrode-Research/unrealbench.git
cd "$(basename Nitrode-Research/unrealbench)"
uv sync --locked
uv run --locked python -m scripts.harbor_local check
uv run --locked python -m scripts.harbor_local smoke
```

For an unpublished local export, enter its directory and start at `uv sync`.
Python 3.14.7 and Harbor 0.23.0 are pinned. The smoke uses a public Ubuntu image,
requires no Epic or model credentials, and should report oracle reward 1,
regrade reward 1, and `source_unchanged: true`.
The included GitHub Actions workflow runs these package and smoke checks
without engine or model credentials. It does not run the Unreal task suite.

## Run a task

Obtain access to Epic's Unreal Engine container images through your own
Epic-linked GitHub account. Follow the
[official container setup](https://dev.epicgames.com/documentation/unreal-engine/quick-start-guide-for-using-container-images-in-unreal-engine)
and authenticate Docker as the same user that runs Harbor. Engine binaries
and image layers are not distributed in this repository.

Read the selected task's `instruction.md`, `task.toml`, any included `QC.md`, and the
[catalog](CATALOG.md). Rendering tasks require an NVIDIA GPU, compatible Linux
drivers and the [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html).
Their Compose definitions request GPU access for both the agent and separate
verifier, and mount the host driver's Vulkan manifest read-only. The default
path is `/usr/share/vulkan/icd.d/nvidia_icd.json`; if your driver installs it
elsewhere, export `NVIDIA_VULKAN_ICD` with its absolute host path before running
Harbor. The manifest must come from your installed NVIDIA driver. This avoids
the stale manifest bundled in the engine image; a missing file stops startup.
Use a dedicated GPU worker with one concurrent task; the Compose
configuration exposes all of its GPUs. Full verification paths are still being
qualified. The declared task storage limit does not include
all host image/build-cache storage. Measured host requirements are pending.

GPU validation uses a Linux host with 16 vCPUs, 64 GiB RAM, an NVIDIA T4,
and a 300 GiB disk. Minimum host requirements have not been established.
Initial rendered runs can spend many minutes compiling shaders before their
checks begin; inspect the verifier logs under `jobs/` for progress.

Task 172 v0.5.0 scores 45 checks with corrected fixtures and excludes HUD pixel
scoring. Its complete verifier runs headlessly and does not require a GPU.
Task 124 can require independent HUD observations for alternative layouts;
follow its [review setup](unreal/tasks/ue-0124-nse-relay-combat/PUBLIC_REVIEW.md)
before running it. An expired review produces no valid reward.

For a reference-solution run (no model API calls):

```sh
uv run --locked harbor run -p unreal/tasks/ue-0172 --agent oracle -e docker --jobs-dir jobs --job-name oracle-172
```

Task 172's reference solution and native regrade each passed 45/45 checks;
the unchanged starter passed 26/45. See the [evidence](validation/task-172-v0.5.0.json).
The 13-task operational profile is certified; task 123 remains uncertified.
To use your own agent, configure its provider credentials and replace
`--agent oracle` with `--agent AGENT --model PROVIDER/MODEL` using an agent
supported by the pinned Harbor version. Keep credentials out of Git.

## Inspect and regrade

Harbor writes job summaries, per-trial results, verifier logs and recorded
project artifacts beneath `jobs/`. Infrastructure exceptions are not valid
zero-score outcomes. Keep the complete job directory for regrading.

Check a saved job without launching containers, then regrade it:

```sh
uv run --locked python -m scripts.regrade jobs/oracle-172 -p unreal/tasks/ue-0172
uv run --locked python -m scripts.regrade jobs/oracle-172 -p unreal/tasks/ue-0172 --execute --jobs-dir jobs --job-name regrade-172 -n 1
```

Use a new job name for each replay. Regrading restores the recorded submission
into a separate verifier and does not rerun the agent. Missing artifacts and
mismatched task identities fail preflight. Historical jobs for different tasks
with the same numeric ID are not interchangeable with these packages.

## What is public

Instructions, starter projects, verifiers and reference solutions are visible
to benchmark operators. Normal agent environments receive the task instruction
and environment contents; solutions and verifier tests are outside the agent
Docker build context. Oracle runs intentionally use the reference solution.

No Nitrode account, AWS account, shared AMI, S3 bucket, or private development
repository access is required. You supply compute and any model/engine access.

See [provenance](PROVENANCE.md), [third-party notices](THIRD-PARTY.md), and
[release status](VALIDATION.md). Nitrode-owned code and task content are licensed
under [Apache-2.0](LICENSE). Existing third-party licenses remain in effect;
see [NOTICE](NOTICE). Certification scope and limits are defined in
[CERTIFICATION.md](CERTIFICATION.md).

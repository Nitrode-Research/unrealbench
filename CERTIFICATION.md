# Operational certification

UnrealBench `v0.1.0` certifies the **operational-13-v1** profile. The profile
contains every published task except `ue-0123-nse-spectator-rooms`, which remains
available but is explicitly deferred and uncertified.

For this profile, “certified” means that each exact public package passes the
static Harbor checks, its native verifier starts, all declared automated tests
execute, and the verifier produces a valid result. Reference solutions or saved
reference submissions are used to exercise the verifier. A full model-agent run
is not required.

The machine-readable [certificate](CERTIFICATION.json) binds the claim to all
13 package manifests and canonical hashes of the public JSON evidence receipts.
The canonical hashes are stable across checkout line-ending settings. Anyone
can check the certificate with:

```sh
uv run --locked python scripts/check_website_tasks.py
uv run --locked python scripts/check_operational_certification.py
```

This certification does not claim model pass rates, task difficulty, repeated
reliability across every GPU/driver combination, or completion of optional
manual visual review. Task 168 had one invalid Vulkan startup followed by a
successful unchanged-package replay of all 73 tests; both outcomes remain in
the public evidence. Task 123 must complete the same gate before it can join a
future 14-task certified profile.

Some linked evidence receipts were generated before this operational scope was
chosen and therefore describe themselves as candidates or `certified: false`
under the earlier, broader standard. `CERTIFICATION.json` is the authoritative
scope decision and validates the immutable evidence hashes before accepting it.

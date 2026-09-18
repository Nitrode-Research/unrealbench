# Validation status: authored, not released

Static partition/reference/hash/mapping checks are reproducible with the authoring
audit. Native builds/editor runs, reference-pass, compiling-start-fail, targeted
mutations, authored-level smoke, graphics checks and actual Harbor validation have
NOT been executed for these new composed packages. Do not inherit old baselines.

Run the package's `verify.py` from an environment where the GEB Python package and
UE engine are installed, with root scheduling to avoid concurrent native builds:

```
python TASK/verify.py --source solution --output-dir NEW_REFERENCE_EVIDENCE
python TASK/verify.py --source start --output-dir NEW_START_EVIDENCE
python TASK/verify.py --source ABSOLUTE_CANDIDATE_SNAPSHOT --output-dir NEW_EVIDENCE
```

This wrapper audits protected paths before invoking `unrealbench.src.verify_suites`.
Use this wrapper rather than assuming the legacy runner enforces protected manifests
or binary scoring. Engine build/transient directories are excluded; additional
submitted source/config/build files are rejected. A contract violation currently
invalidates the submission instead of silently restoring protected files.

The required behavioral lane uses NullRHI. All diagnostic groups and end-to-end
groups are mandatory. Missing, duplicate or extra results, crashes and platform
failures yield null/invalid reward; complete behavior passes yield binary1, complete
behavior failures binary0. `diagnostic_fraction` is not a playable-game success label.

Freeze source/spec/test/protection manifests with the run. Preserve the immutable
pre-injection model workspace and full trajectory for regrading. Run tampered-header,
Build.cs/test-suppression, alternate implementation and manufactured-log probes before
RLVR release. A protected manifest is not proof against adversarial gameplay code
spoofing the shared runner's log parser; that remains an explicit release gate.

Authored Blueprint assets may carry presentation/action wiring, which is supplied
by design. A full authored-level smoke and Blueprint dependency audit are still
needed to prove they do not implement removed native behavior through alternate
graphs. The full task is a complete implemented-native-game restoration candidate,
not a claim that every unfinished content path is playable or pixel-verified.

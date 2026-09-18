# Validation status

Authored and statically audited; no new native build, oracle, start-fail, mutation, rendered or Harbor result is claimed.

Use the task-owned entrypoint, not legacy runtime-module injection:

```
python verify.py --project <candidate-or-solution> --engine <UE-root> --output <fresh-output>
```

CPU-only diagnostic: add `--lane headless`; reward remains null. Render on a compatible host, optionally supplying platform arguments with `--render-arg=-dx12 --render-arg=-sm6`. No automatic GPU skip. Both lanes and exact nonempty manifests are mandatory. Compilation failures remain unattributed/incomplete until oracle and candidate failures are separated.

Required next gates: reference build/all tests, safe compiling stripped start with complete failure records, targeted cross-system mutations, repeated reference timing, actual Linux/Harbor injection and rendered host. Existing component baselines do not certify this composition.

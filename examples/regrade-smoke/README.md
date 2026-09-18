# Regrade installation check

This synthetic task checks Harbor installation, Docker, artifact collection and
separate verification. It uses a public Ubuntu image and needs no Unreal or
model credentials. It is an infrastructure fixture, not a benchmark task.

The starter contains the wrong answer. The oracle writes the correct answer;
the separate verifier must recover that answer from the recorded artifact.
`scripts/smoke_regrade_on_host.sh` requires both the original and replay to score
1, verifies native regrade provenance, and checks that replay leaves the source
job byte-for-byte unchanged. Passing does not certify any Unreal task.

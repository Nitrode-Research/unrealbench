#!/bin/bash
set -uo pipefail
LOGS=/logs/verifier
mkdir -p "$LOGS"
rm -f "$LOGS/reward.txt"
( cd /project && tar -czf "$LOGS/final-workspace.tar.gz" gdd.md Source Config Content Scripts Shaders SourceData SourceArt foundations_ue.uproject 2>/dev/null ) || exit 1
sha256sum "$LOGS/final-workspace.tar.gz" > "$LOGS/final-workspace.sha256"
python3 /tests/task-package/verification/verify_linux.py --project /project --engine /home/ue4/UnrealEngine --output "$LOGS/native"
RC=$?
exec python3 /tests/emit_reward.py "$LOGS/native/report.json" "$LOGS/reward.txt" "$RC"

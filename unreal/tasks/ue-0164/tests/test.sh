#!/bin/bash
set -uo pipefail
UE=/home/ue4/UnrealEngine
PROJ=/project
LOGS=/logs/verifier
INJECT="$PROJ/Source/ScifiSimEscape/Tests"
mkdir -p "$LOGS"
rm -f "$LOGS/reward.txt" "$LOGS/score-diagnostic.json"
python3 /tests/scorer-tests/test_score_automation.py > "$LOGS/scorer-unit.log" 2>&1 || { cat "$LOGS/scorer-unit.log"; echo "VERDICT: scorer self-tests failed; reward invalid"; exit 1; }
(
  cd "$PROJ" || exit 1
  snapshot=()
  for path in Source Config Content Plugins Scripts Shaders SourceData SourceArt *.uproject *.md; do
    [ -e "$path" ] && snapshot+=("$path")
  done
  tar -czf "$LOGS/final-workspace.tar.gz" "${snapshot[@]}"
  sha256sum "$LOGS/final-workspace.tar.gz" > "$LOGS/final-workspace.sha256"
) || { echo "VERDICT: could not preserve final candidate workspace"; exit 1; }
python3 /tests/audit_sse.py "$PROJ" /protected-baseline /tests/task-package "$LOGS/workspace-audit.json" || exit 1
if [ -f /protected-baseline/.protected_paths.list ]; then
  while IFS= read -r rel; do
    [ -z "$rel" ] && continue
    [ -e "/protected-baseline/$rel" ] || { echo "VERDICT: protected baseline missing $rel"; exit 1; }
    mkdir -p "$PROJ/$(dirname "$rel")"
    rm -rf "$PROJ/$rel"
    cp -a "/protected-baseline/$rel" "$PROJ/$rel"
  done < /protected-baseline/.protected_paths.list
fi
rm -rf "$INJECT"
mkdir -p "$INJECT"
for ext in cpp h inl py json; do cp /tests/ue/*."$ext" "$INJECT/" 2>/dev/null || true; done
ls "$INJECT"/*.cpp >/dev/null 2>&1 || { echo "VERDICT: hidden test injection failed"; exit 1; }
"$UE/Engine/Build/BatchFiles/Linux/Build.sh" ScifiSimEscapeEditor Linux Development -project="$PROJ/ScifiSimEscape.uproject" -NoUBA -MaxParallelActions=4 > "$LOGS/build.log" 2>&1
if [ $? -ne 0 ]; then echo "VERDICT: compile failed"; grep -E 'error:|Error:' "$LOGS/build.log" | head -20 || true; exit 1; fi
"$UE/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PROJ/ScifiSimEscape.uproject" -ExecCmds="Automation RunTests GameEngineBench.UE0164; Quit" -TestExit="Automation Test Queue Empty" -Unattended -CrashForUAT -NullRHI -NoSplash -NumClients=0 -ListenServer -nosound -nop4 -log -stdout -FullStdOutLogOutput > "$LOGS/automation.log" 2>&1
UE_RC=$?
if grep -qE 'Signal [0-9]+ caught|SIGSEGV|SIGABRT|Unhandled Exception|Fatal error:|Assertion failed' "$LOGS/automation.log"; then echo "VERDICT: editor terminated abnormally; reward invalid"; exit 1; fi
if [ "$UE_RC" -ne 0 ] && ! grep -q 'TEST COMPLETE. EXIT CODE' "$LOGS/automation.log"; then echo "VERDICT: automation incomplete; reward invalid"; exit 1; fi
python3 /tests/score_automation.py --log "$LOGS/automation.log" --expected /tests/expected_reward_tests.list --reward "$LOGS/reward.txt" --diagnostic "$LOGS/score-diagnostic.json"

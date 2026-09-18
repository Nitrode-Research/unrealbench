#!/bin/bash
set -uo pipefail
LOGS=/logs/verifier
mkdir -p "$LOGS"
rm -f "$LOGS/reward.txt"
( cd /project && tar -czf "$LOGS/final-workspace.tar.gz" gdd.md Source Config Content Scripts Shaders SourceData SourceArt foundations_ue.uproject 2>/dev/null ) || exit 1
sha256sum "$LOGS/final-workspace.tar.gz" > "$LOGS/final-workspace.sha256" || exit 1
python3 /tests/task-package/verification/verify_linux.py --project /project --engine /home/ue4/UnrealEngine --output "$LOGS/native" --headless-only
RC=$?
# A completed partial-credit run exits 1. Invalid/incomplete runs never emit a reward.
python3 - "$LOGS/native/report.json" "$LOGS/reward.txt" "$RC" /tests/task-package/verification/checks.json <<'PY'
import json, math, sys
from pathlib import Path
p=json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
manifest=json.loads(Path(sys.argv[4]).read_text(encoding='utf-8'))
revision=manifest.get('scoring_revision')
total={'ue-0172-no-hud-v1':44,'ue-0172-scored-preservation-v2':45,'ue-0172-valid-fixtures-v3':45}[revision]
submission=manifest.get('submission',[])
assert submission==(['Task0172.Submission.SuppliedCodePreservation'] if total==45 else [])
expected=submission+manifest['preparation']+manifest['headless']
assert manifest['rendered']==[] and len(expected)==len(set(expected))==total
assert p.get('task')=='ue_task_0172' and p.get('scoring_revision')==revision
assert p.get('reward_valid') is True and not p.get('diagnostic_only')
assert all(p.get(key)==[] for key in ('infrastructure_errors','submission_failures','blocked_stages'))
checks=p['checks']
assert sorted(row['name'] for row in checks)==sorted(expected)
assert all(row['status'] in ('passed','failed') for row in checks)
passed=sum(row['status']=='passed' for row in checks)
failed=len(checks)-passed
assert p.get('passed')==passed and p.get('failed')==failed
for name in ('prepare_entry_terrain.py','prepare_entry_composition.py','compile','generate','headless'):
    stages=[stage for stage in p['stages'] if stage['name']==name]
    assert len(stages)==1
    headless_failed=any(row['name'] in manifest['headless'] and row['status']=='failed' for row in checks)
    expected_failure=255 if revision=='ue-0172-valid-fixtures-v3' else 1
    assert stages[0]['exit_code']==(expected_failure*int(headless_failed) if name=='headless' else 0)
    if name=='headless' and revision=='ue-0172-valid-fixtures-v3':assert stages[0].get('automation_exit_code')==(-1 if headless_failed else 0)
reward=p.get('reward')
assert type(reward) in (int,float) and math.isfinite(reward) and reward==passed/total
assert p.get('strict_success') is (failed==0) and int(sys.argv[3])==int(failed>0)
Path(sys.argv[2]).write_text(str(reward)+'\n',encoding='utf-8')
print(f'VERDICT: {passed}/{total} required checks passed; reward={reward}; rendered HUD scoring excluded')
PY
exit $?

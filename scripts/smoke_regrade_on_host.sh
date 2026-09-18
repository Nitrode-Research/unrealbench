#!/usr/bin/env bash
# Run on an existing Harbor Docker host. No model/provider credentials are used.
set -euo pipefail
if [[ $# -ne 2 ]]; then
  echo "usage: smoke_regrade_on_host.sh TASK_DIRECTORY NEW_OUTPUT_DIRECTORY" >&2
  exit 2
fi
task=$(realpath "$1")
output=$(realpath -m "$2")
if [[ -e "$output" ]]; then
  echo "Output already exists; choose a new directory: $output" >&2
  exit 2
fi
test -f "$task/task.toml"
command -v harbor >/dev/null
command -v python3 >/dev/null
docker info >/dev/null
mkdir -p "$output"
harbor --version > "$output/harbor-version.txt"
docker version > "$output/docker-version.txt"
harbor run -p "$task" --agent oracle -e docker -n 1 \
  --job-name source --jobs-dir "$output" > "$output/source.log" 2>&1

python3 - "$output/source" "$output/source-before.json" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
trials = list(root.glob('*/result.json'))
if len(trials) != 1:
    raise SystemExit(f'Expected one source trial, found {len(trials)}')
result = json.loads(trials[0].read_text())
if result.get('exception_info'):
    raise SystemExit('Source had infrastructure/verifier error; inspect its result.json')
if (result.get('verifier_result') or {}).get('rewards', {}).get('reward') != 1:
    raise SystemExit('Source oracle did not earn full reward; inspect its result.json')
hashes = {}
for path in sorted(root.rglob('*')):
    if path.is_file():
        with path.open('rb') as stream:
            hashes[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
pathlib.Path(sys.argv[2]).write_text(json.dumps(hashes, sort_keys=True), encoding='utf-8')
PY

harbor job regrade "$output/source" -p "$task" -e docker -n 1 \
  --job-name replay --jobs-dir "$output" > "$output/replay.log" 2>&1

python3 - "$output" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
def trial(job):
    paths = [p for p in job.iterdir() if p.is_dir() and (p/'result.json').is_file()]
    if len(paths) != 1:
        raise SystemExit(f'Expected one trial in {job}, found {len(paths)}')
    return json.loads((paths[0]/'result.json').read_text()), paths[0]
source, source_path = trial(root/'source')
replay, replay_path = trial(root/'replay')
for label, result in [('source', source), ('replay', replay)]:
    if result.get('exception_info'):
        raise SystemExit(f'{label} had infrastructure/verifier error; inspect its result.json')
    reward = (result.get('verifier_result') or {}).get('rewards', {}).get('reward')
    if reward != 1:
        raise SystemExit(f'{label} expected full oracle reward, found {reward!r}')
config = json.loads((replay_path/'config.json').read_text())
if (config.get('source_trial') or {}).get('action') != 'regrade':
    raise SystemExit('Replay is not recorded as a native regrade trial')
before = json.loads((root/'source-before.json').read_text())
after = {}
for path in sorted((root/'source').rglob('*')):
    if path.is_file():
        with path.open('rb') as stream:
            after[path.relative_to(root/'source').as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
if before != after:
    raise SystemExit('Regrade changed the source job record')
report = dict(passed=True, source_reward=1, regrade_reward=1, source_unchanged=True,
              source_trial=str(source_path), regrade_trial=str(replay_path),
              harbor_version=(root/'harbor-version.txt').read_text().strip())
(root/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print(json.dumps(report, indent=2))
PY

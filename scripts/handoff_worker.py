"""Run the handoff payload on an existing Linux Harbor host (no AWS SDK needed)."""
from __future__ import annotations

import hashlib
from importlib.metadata import version
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import traceback


def digest_tree(root: Path) -> dict[str, str]:
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_file():
            with path.open('rb') as stream:
                result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
    return result


def doctor() -> dict:
    from importlib.metadata import PackageNotFoundError
    try:
        harbor = version('harbor')
    except PackageNotFoundError:
        raise RuntimeError('Harbor is not installed in this Python environment; run uv sync --locked') from None
    if harbor != '0.23.0':
        raise RuntimeError(f'Harbor 0.23.0 required; this host has {harbor}')
    if sys.platform != 'linux':
        raise RuntimeError('The runner must be Linux')
    docker = subprocess.run(['docker', 'info', '--format', '{{.OSType}}'], check=True, capture_output=True, text=True).stdout.strip()
    if docker != 'linux':
        raise RuntimeError('Docker must run Linux containers')
    compose = subprocess.run(['docker', 'compose', 'version', '--short'], check=True, capture_output=True, text=True).stdout.strip()
    return {'harbor':harbor, 'python':sys.version.split()[0], 'docker_os':docker, 'compose':compose,
            'free_disk_gib':round(shutil.disk_usage(Path.cwd()).free / 2**30, 1)}


def run(payload: Path) -> int:
    os.chdir(payload)
    spec = json.loads((payload/'handoff.json').read_text())
    output = payload.parent/'output'
    output.mkdir(exist_ok=False)
    # Ensure shell smoke commands use the same pinned Harbor interpreter.
    os.environ['PATH'] = str(Path(sys.executable).parent) + os.pathsep + os.environ.get('PATH','')
    report = {'mode':spec['mode'], 'passed':False}
    rc = 1
    try:
        report['host'] = doctor()
        tasks = [payload/p for p in spec.get('tasks',[])]
        if spec['mode'] == 'smoke':
            with (output/'smoke.log').open('w') as log:
                subprocess.run(['bash',str(payload/'scripts/smoke_regrade_on_host.sh'),str(tasks[0]),str(output/'smoke')], stdout=log, stderr=subprocess.STDOUT, check=True)
            report['smoke'] = json.loads((output/'smoke/validation.json').read_text())
        elif spec['mode'] == 'regrade':
            try:
                from scripts.regrade import check_job
            except ModuleNotFoundError:
                from regrade import check_job
            source = payload/'source'
            before = digest_tree(source)
            preflight = check_job(source,tasks)
            (output/'preflight.json').write_text(json.dumps(preflight,indent=2),encoding='utf-8')
            if not preflight['ready']:
                raise RuntimeError('Recorded inputs are incompatible; see preflight.json')
            command=[sys.executable,'-m','harbor.cli.main','job','regrade',str(source),'-e','docker','-n',str(spec['concurrency']),'--jobs-dir',str(output/'jobs'),'--job-name','replay']
            for task in tasks:
                command.extend(['-p',str(task)])
            try:
                with (output/'regrade.log').open('w') as log:
                    subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
            finally:
                report['source_unchanged'] = before == digest_tree(source)
            if not report['source_unchanged']:
                raise RuntimeError('Source job changed during replay')
            results = list((output/'jobs/replay').glob('*/result.json'))
            if len(results) != len(preflight['trials']):
                raise RuntimeError('Regrade did not produce all expected trial results')
            report['trials'] = []
            for path in results:
                data = json.loads(path.read_text())
                config = json.loads((path.parent/'config.json').read_text())
                if (config.get('source_trial') or {}).get('action') != 'regrade':
                    raise RuntimeError('Missing native regrade provenance')
                trial={'name':path.parent.name,'rewards':(data.get('verifier_result') or {}).get('rewards'), 'exception_type':(data.get('exception_info') or {}).get('exception_type')}
                report['trials'].append(trial)
                if data.get('exception_info') or not trial['rewards']:
                    raise RuntimeError('A trial failed to produce a verifier reward; inspect collected results')
            # A legitimate zero reward is a completed regrade, not a tool failure.
        elif spec['mode'] != 'check':
            raise ValueError('Unknown operation')
        report['passed'] = True
        rc = 0
    except Exception as error:
        report['error'] = str(error)
        (output/'error.txt').write_text(traceback.format_exc(),encoding='utf-8')
    finally:
        (output/'handoff-result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        with tarfile.open(payload.parent/'results.tar.gz','w:gz') as archive:
            archive.add(output,arcname='results')
    print(json.dumps(report),flush=True)
    return rc


if __name__ == '__main__':
    raise SystemExit(run(Path(sys.argv[1]).resolve()))

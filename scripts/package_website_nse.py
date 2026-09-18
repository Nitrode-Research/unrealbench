"""Import the website's NightSkyEngine tasks from a pinned UnrealBench checkout.

Requires PyYAML (available with Harbor). Refuses to overwrite existing tasks.
Native verification is a separate step; packaging is not certification.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import shutil
import subprocess

import yaml

try:
    from scripts.standardize_harbor import configure_task
except ModuleNotFoundError:
    from standardize_harbor import configure_task

IDS = (121, 122, 123, 124, 125, 131, 132, 133, 134)
EXCLUDED = {'.git', 'Binaries', 'Intermediate', 'Saved', 'DerivedDataCache', '__pycache__'}
IMAGE = 'ghcr.io/epicgames/unreal-engine:dev-slim-5.8.2@sha256:6840106f40dfa6655d6959838aee0006f1bd693cac442ecd7753a9dbf267d649'
MODIFIER_MODULE = 'Plugins/NightSkyEngine/Source/NightSkyEngine/'
MODIFIER_FIXTURE_DUPLICATES = {
    MODIFIER_MODULE + 'TestSupport/NightSkyEngine/Fixtures/' + name:
    MODIFIER_MODULE + 'Fixtures/' + name
    for name in ('ModifierArenaProbe.h', 'ModifierArenaProbe.cpp', 'ModifierCapture.h',
                 'ModifierScenarioWorker.h', 'ModifierScenarioWorker.cpp')
}


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(value, encoding='utf-8', newline='\n')


def copy(source, destination):
    if source.is_dir():
        shutil.copytree(source, destination, ignore=lambda _, names: set(names) & EXCLUDED)
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def instruction_text(instruction, editable):
    files = '\n'.join(f'- `{path}`' for path in editable)
    return f'{instruction}\n\n## Files to edit\n\nYou must only change the following files:\n\n{files}\n'


def public_instruction(spec):
    if not spec.get('editable_paths'):
        return instruction_text(spec['instruction'].strip(), spec['files_to_edit'])
    editable = '\n'.join(f'- `{path}`' for path in spec['editable_paths'])
    protected = '\n'.join(f'- `{path}`' for path in spec.get('protected_paths', []))
    return (f"{spec['instruction'].strip()}\n\n## Editable paths\n\n"
            f"You may add or modify files within these paths:\n\n{editable}\n\n"
            f"## Protected paths\n\nDo not change these paths, including when nested within an editable path:\n\n{protected}\n")


def reference_changes(source, commit, task_name, spec):
    """Return files, out-of-scope omissions, and receipted duplicate fixtures."""
    source = source.resolve()
    command = ['git', '-c', 'safe.directory=' + source.as_posix(), '-C', str(source)]
    prefix = 'tasks_unreal/' + task_name
    def git(*args):
        return subprocess.check_output([*command, *args])
    changed = git('diff', '--no-renames', '--name-status', '-z',
                  commit + ':' + prefix + '/start', commit + ':' + prefix + '/solution').decode().split('\0')
    allowed = spec.get('editable_paths') or spec['files_to_edit']
    protected = spec.get('protected_paths', [])
    files, omitted, duplicate_fixtures = {}, [], {}
    changes = list(zip(changed[0:-1:2], changed[1:-1:2]))
    changed_paths = {relative for _, relative in changes}
    changes.extend(('M', relative) for relative in spec['files_to_edit'] if relative not in changed_paths)
    for status, relative in changes:
        path = PurePosixPath(relative)
        if path.is_absolute() or '..' in path.parts or '\\' in relative or ':' in relative:
            raise ValueError('Unsafe reference path')
        within = lambda roots: any(relative == root or relative.startswith(root + '/') for root in roots)
        if not within(allowed) or within(protected):
            omitted.append(relative)
            continue
        if status not in {'A', 'M'}:
            raise ValueError('Reference deletion/type change requires explicit packaging: ' + relative)
        content = git('show', commit + ':' + prefix + '/solution/' + relative)
        # These upstream additions duplicate verifier-owned UCLASS definitions.
        # Only omit the audited paths, when the protected overlay supplies the
        # same bytes modulo CRLF; never discard differing reference behavior.
        fixture = MODIFIER_FIXTURE_DUPLICATES.get(relative)
        if (task_name == 'ue_task_0125_nse_match_modifiers' and status == 'A'
                and fixture and any(fixture == root or fixture.startswith(root + '/')
                                    for root in protected)):
            fixture_source = 'tests_fixture/' + fixture
            fixture_content = git('show', commit + ':' + prefix + '/' + fixture_source)
            if content.replace(b'\r\n', b'\n') == fixture_content.replace(b'\r\n', b'\n'):
                duplicate_fixtures[relative] = {
                    'oracle_sha256': hashlib.sha256(content).hexdigest(),
                    'fixture_source_path': fixture_source,
                    'fixture_source_sha256': hashlib.sha256(fixture_content).hexdigest(),
                    'fixture_package_path': 'tests/fixture_overlay/' + fixture,
                    'comparison': 'Exact bytes after CRLF-to-LF normalization',
                }
                continue
        if content.startswith(b'version https://git-lfs.github.com/spec/v1\n'):
            fields = dict(line.split(' ', 1) for line in content.decode().splitlines())
            algorithm, oid = fields['oid'].split(':')
            if algorithm != 'sha256' or len(oid) != 64 or any(c not in '0123456789abcdef' for c in oid):
                raise ValueError('Invalid LFS object ID')
            common = Path(git('rev-parse', '--git-common-dir').decode().strip())
            if not common.is_absolute():
                common = source / common
            content = (common / 'lfs/objects' / oid[:2] / oid[2:4] / oid).read_bytes()
            if hashlib.sha256(content).hexdigest() != oid or len(content) != int(fields['size']):
                raise ValueError('LFS object failed integrity check: ' + relative)
        files[relative] = content
    if not files:
        raise ValueError('Reference delta is empty')
    return files, omitted, duplicate_fixtures


def suites(spec):
    tests = spec['tests']
    expected = set(spec['test_requirements'])
    definitions = tests.get('suites') or [dict(name='behavior', filter=tests['filter'], command_line=tests.get('command_line', {}))]
    assigned = set()
    result = []
    for definition in definitions:
        prefix = definition['filter']
        members = sorted(name for name in expected if name == prefix or name.startswith(prefix + '.'))
        if not members or assigned.intersection(members):
            raise ValueError('Empty or overlapping suite: ' + prefix)
        assigned.update(members)
        overrides = definition.get('command_line', {})
        flags = ['-Unattended', '-CrashForUAT', '-NullRHI', '-NoSplash', f"-NumClients={tests.get('num_clients', 0)}", '-ListenServer']
        flags = [flag for flag in flags if flag not in overrides.get('remove_args', [])]
        # D3D is Windows-only; the Linux renderer uses Vulkan. Keep shader model,
        # offscreen and deterministic audio requirements from the source contract.
        additions = overrides.get('add_args', [])
        flags += [flag for flag in additions if flag.lower() not in {'-d3d11', '-d3d12', '-dx11', '-dx12'}]
        if '-NullRHI' not in flags:
            flags.append('-vulkan')
        if not any(flag.lower() in {'-deterministicaudio', '-nosound'} for flag in flags):
            flags.append('-nosound')
        result.append(dict(name=definition['name'], filter=prefix, expected=members, flags=flags))
    if assigned != expected:
        raise ValueError('Not every required test belongs to a suite')
    return result


def write_verifier_entrypoint(target, number, task124_review_wrapper=None):
    entrypoint = 'verify.py'
    if number == 124:
        if task124_review_wrapper is None:
            raise ValueError('Task124 requires the trusted task124_verify.py wrapper')
        (target / 'tests/task124_verify.py').write_bytes(task124_review_wrapper)
        entrypoint = 'task124_verify.py'
    write(target / 'tests/test.sh', f'#!/bin/bash\nset -euo pipefail\nexec python3 /tests/{entrypoint}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--unrealbench', type=Path, required=True)
    parser.add_argument('--ids', type=int, nargs='+', default=IDS, choices=IDS)
    parser.add_argument('--task124-review-wrapper', type=Path,
                        help='Trusted task124_verify.py from the public package; required when importing task124')
    args = parser.parse_args()
    if 124 in args.ids and args.task124_review_wrapper is None:
        parser.error('Importing task124 requires --task124-review-wrapper PATH to preserve its review guard')
    task124_review_wrapper = (args.task124_review_wrapper.read_bytes()
                              if args.task124_review_wrapper is not None else None)
    repo = Path(__file__).resolve().parents[1]
    source = args.unrealbench.resolve()
    commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    for number in args.ids:
        task = next((source / 'tasks_unreal').glob(f'ue_task_{number:04d}_*'))
        spec = yaml.safe_load((task / 'spec.yaml').read_text(encoding='utf-8'))
        slug = task.name.replace('ue_task_', 'ue-', 1).replace('_', '-')
        target = repo / 'unreal/tasks' / slug
        if target.exists():
            raise FileExistsError(target)
        plan = suites(spec)
        editable = spec['files_to_edit']
        if len(editable) != len(set(editable)) or not editable:
            raise ValueError('Invalid editable manifest')
        # Check before any copying; never publish pointer files as playable assets.
        for path in (task / 'start').rglob('*'):
            if path.is_file() and path.stat().st_size < 200 and path.read_bytes().startswith(b'version https://git-lfs.github.com/spec/v1'):
                raise ValueError('Unresolved LFS asset: ' + str(path))
        workspace = target / 'environment/workspace'
        copy(task / 'start', workspace)
        protected = target / 'environment/protected'
        protected.mkdir(parents=True)
        for rel in spec.get('protected_paths', []):
            if not (task / 'start' / rel).exists():
                raise FileNotFoundError('Missing protected path: ' + rel)
            copy(task / 'start' / rel, protected / rel)
        reference, omitted, duplicate_fixtures = reference_changes(source, commit, task.name, spec)
        for rel, content in reference.items():
            destination = target / 'solution/files' / rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(content)
        copy(task / 'tests', target / 'tests/ue')
        if (task / 'tests_fixture').is_dir():
            copy(task / 'tests_fixture', target / 'tests/fixture_overlay')
        copy(task / 'spec.yaml', target / 'tests/source-spec.yaml')
        for name in ('score_automation.py', 'capture_submission.py'):
            copy(source / 'harbor' / name, target / 'tests' / name)
        copy(repo / 'scripts/unreal_website_verifier.py', target / 'tests/verify.py')
        config = dict(project='NightSkyEngine', target='NightSkyEngineEditor', module='NightSkyEngineDemo',
                      protected=spec.get('protected_paths', []), suites=plan,
                      original_tests=[p.relative_to(workspace).as_posix() for p in workspace.rglob('*.cpp')
                                      if any(part.lower() in {'test', 'tests'} for part in p.relative_to(workspace).parts)])
        write(target / 'tests/config.json', json.dumps(config, indent=2) + '\n')
        subprocess.run([__import__('sys').executable, str(target / 'tests/capture_submission.py'), 'baseline',
                        '--project', str(workspace), '--baseline', str(protected / '.submission-baseline.json')], check=True)
        instruction = spec['instruction'].strip()
        if len(instruction) < 50:
            raise ValueError('Missing public instruction')
        write(target / 'instruction.md', public_instruction(spec))
        write(target / '.gitattributes', 'environment/workspace/** -text\nenvironment/protected/** -text\nsolution/files/** -text\ntests/source-spec.yaml -text\ntests/ue/** -text\ntests/fixture_overlay/** -text\ntests/verify.py text eol=lf\n')
        write(target / 'environment/Dockerfile', f'FROM {IMAGE}\nENV UV_PYTHON=3.11\nCOPY --chown=ue4:ue4 workspace/ /project/\nCOPY --chown=ue4:ue4 protected/ /protected-baseline/\nWORKDIR /project\n')
        write(target / 'solution/solve.sh', '#!/bin/bash\nset -euo pipefail\ncp -a /solution/files/. /project/\n')
        write_verifier_entrypoint(target, number, task124_review_wrapper)
        rendered = any('-NullRHI' not in lane['flags'] for lane in plan)
        description = json.dumps(spec['title'])
        write(target / 'task.toml', f'''schema_version = "1.4"
artifacts = []
[task]
name = "Nitrode/{slug}"
version = "0.1.0"
description = {description}
[[task.authors]]
name = "Nitrode"
email = "hello@nitrode.com"
[metadata]
benchmark = "UnrealBench"
website_task_id = "ue-{number:04d}"
source_task = "{task.name}"
source_commit = "{commit}"
requires_rendering = {str(rendered).lower()}
[verifier]
timeout_sec = 14400.0
collect = []
[agent]
timeout_sec = 14400.0
[environment]
os = "linux"
build_timeout_sec = 2400.0
cpus = 8
memory_mb = 16384
storage_mb = 40960
''')
        receipt = dict(repository='https://github.com/brianyla/unrealbench', commit=commit,
                       source_task=task.name, source_files={}, adaptations=['Linux Vulkan flags replace Windows D3D flags', 'Harbor verifier runs every declared suite'])
        receipt['oracle_files'] = {name: hashlib.sha256(content).hexdigest() for name, content in reference.items()}
        receipt['oracle_omitted_outside_editable_scope'] = omitted
        if duplicate_fixtures:
            for omission in duplicate_fixtures.values():
                omission['fixture_package_sha256'] = hashlib.sha256(
                    (target / omission['fixture_package_path']).read_bytes()).hexdigest()
            receipt['oracle_omitted_duplicate_fixtures'] = duplicate_fixtures
            receipt['adaptations'].append(
                'Omit duplicate task125 reference fixture helpers; retain identical verifier-owned fixtures')
        for path in [task / 'spec.yaml', *(task / 'tests').rglob('*'), *(task / 'tests_fixture').rglob('*')]:
            if path.is_file():
                receipt['source_files'][path.relative_to(task).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
        write(target / 'SOURCE_RECEIPT.json', json.dumps(receipt, indent=2) + '\n')
        write(target / 'README.md', f'''# {spec['title']}

Website task **{number}**, sourced from `{task.name}` at UnrealBench commit `{commit}`.
The package name identifies the task independently of its runtime platform.

Run from the repository root:

```sh
uv run --locked harbor run -p unreal/tasks/{slug} --agent oracle -e docker
```

The Docker environment uses Unreal Engine 5.8.2. Hidden tests and fixture overlays
are injected after the agent phase; the oracle follows the source's editable paths
and protected-path exclusions. Its file hashes are recorded in the source receipt.
The verifier requires all {len(spec['test_requirements'])} declared tests across {len(plan)} suite(s).
Reward is the fraction passing only after every suite completes with its exact
test inventory. A compilation failure earns zero; incomplete execution or an
infrastructure failure does not emit a valid reward.

Rendering required: **{str(rendered).lower()}**. Rendered tasks need a working Vulkan
device/driver exposed to the container; this package does not provision one.
See `QC.md` for the validation boundary and `SOURCE_RECEIPT.json` for provenance.
''')
        write(target / 'PROVENANCE.md', f'# Provenance\n\nImported from private UnrealBench `{task.name}` at `{commit}`.\nThe project includes NightSkyEngine and its bundled dependencies/assets; their\noriginal license and notice files are retained in the workspace. This import\ndoes not establish new ownership or licensing rights. Upstream licensing and\nthird-party provenance review remain required before external delivery.\n')
        write(target / 'QC.md', '# Validation status\n\nPackage structure, public instructions, oracle edit scope, exact test inventories,\nand source receipts are checked by `scripts/check_website_tasks.py` (static checks only).\nNative Docker oracle/NOP runs, rendered/multiplayer execution, determinism,\nmutation and hack probes require separate runtime qualification.\nStatic checks do not certify task solvability or scoring.\n')
        write(target / 'CALIBRATION.md', '# Calibration\n\nHistorical website results used earlier task snapshots and harnesses. They are\nnot calibration evidence for this new package checksum. Fresh calibration pending.\n')
        configure_task(target)
        print(f'Imported {slug}: {len(spec["test_requirements"])} tests, {len(plan)} suites', flush=True)


if __name__ == '__main__':
    main()

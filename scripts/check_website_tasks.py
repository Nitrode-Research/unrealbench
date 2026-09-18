"""Check website identity, Harbor packages, GDD integrity and imported inventories.

This is static packaging validation, not a substitute for native oracle/NOP runs.
"""
from pathlib import Path
import hashlib
import json
import sys
import tomllib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from harbor.models.task.config import TaskConfig
import yaml

from scripts.package_website_nse import instruction_text, public_instruction
from scripts.standardize_harbor import PROJECT_ARTIFACTS, validate_verifier_build, verifier_compose, GPU_AGENT_COMPOSE

IDS = {121, 122, 123, 124, 125, 131, 132, 133, 134, 164, 168, 169, 171, 172}
GDD_IDS = {164, 168, 169, 171, 172}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    root = Path(__file__).resolve().parents[1]
    entries = json.loads((root / 'unreal/website-tasks.json').read_text())
    assert len(entries) == len(IDS)
    assert {int(e['website_id'].split('-')[1]) for e in entries} == IDS
    assert {p.name for p in (root / 'unreal/tasks').iterdir() if p.is_dir()} == {e['name'] for e in entries}
    registry = next(d for d in json.loads((root / 'registry.json').read_text())['datasets'] if d['name'] == 'unreal')
    assert {t['path'] for t in registry['tasks']} == {e['path'] for e in entries}
    assert len(registry['tasks']) == len(IDS)
    for entry in entries:
        task = root / entry['path']
        number = int(entry['website_id'].split('-')[1])
        assert task.name == entry['name'] and not task.name.endswith('-linux')
        config = tomllib.loads((task / 'task.toml').read_text(encoding='utf-8'))
        platform_receipt = task / 'PLATFORM_RECEIPT.json'
        platform_changes = {}
        if platform_receipt.exists():
            platform_data = json.loads(platform_receipt.read_text())
            assert platform_data['task_version'] == config['task']['version']
            platform_changes = {row['path']: row for row in platform_data['changes']}
            for rel, row in platform_changes.items():
                assert digest(task / rel) == row['after'], (task.name, rel)

        def source_digest_matches(path, expected):
            adaptation = platform_changes.get(path.relative_to(task).as_posix())
            return digest(path) == (adaptation['after'] if adaptation else expected) and (not adaptation or adaptation['before'] == expected)

        TaskConfig.model_validate(config)
        assert config['artifacts'] == PROJECT_ARTIFACTS, task.name
        assert config['verifier']['environment_mode'] == 'separate', task.name
        assert (task / 'tests/docker-compose.yaml').read_text() == verifier_compose(task), task.name
        validate_verifier_build(task)
        metadata = config.get('metadata', {})
        if metadata.get('requires_rendering') or metadata.get('graphics_required_for_full_reward'):
            # Harbor 0.23's Docker backend rejects environment.gpus; Compose
            # performs the actual device request for both execution phases.
            assert metadata['required_gpus'] == 1, task.name
            assert not config['environment'].get('gpus'), task.name
            assert (task / 'environment/docker-compose.yaml').read_text(encoding='utf-8') == GPU_AGENT_COMPOSE, task.name
        assert config['task']['name'] == 'Nitrode/' + task.name
        for rel in ['instruction.md', 'environment/Dockerfile', 'solution/solve.sh', 'tests/test.sh']:
            assert (task / rel).is_file(), (task.name, rel)
        assert len((task / 'instruction.md').read_text().strip()) > 50
        workspace = task / 'environment/workspace'
        assert list(workspace.glob('*.uproject'))
        assert 'COPY --chown=ue4:ue4 workspace/ /project/' in (task / 'environment/Dockerfile').read_text()
        if number in GDD_IDS:
            if number in {168, 169, 171}:
                wrapper = (task / 'tests/test.sh').read_text(encoding='utf-8')
                assert '--headless-only' not in wrapper and '--lane headless' not in wrapper, task.name
                assert (task / 'tests/emit_reward.py').read_text(encoding='utf-8') == (root / 'scripts/emit_verifier_reward.py').read_text(encoding='utf-8'), task.name
            elif number == 172:
                # v0.5.0 deliberately excludes HUD pixels from the score. Its
                # headless lane is complete scoring, not a diagnostic shortcut.
                scoring = json.loads((task / 'SCORING_RECEIPT.json').read_text())
                checks = json.loads((task / 'tests/task-package/verification/checks.json').read_text())
                assert config['task']['version'] == scoring['taskVersion'] == '0.5.0'
                assert checks['scoring_revision'] == scoring['scoringRevision'] == 'ue-0172-valid-fixtures-v3'
                assert checks['rendered'] == [] and not metadata['graphics_required_for_full_reward']
                expected = checks['submission'] + checks['preparation'] + checks['headless']
                assert len(expected) == len(set(expected)) == scoring['requiredChecks'] == 45
                assert '--headless-only' in (task / 'tests/test.sh').read_text(encoding='utf-8')
                for filename in scoring['changedCpp']:
                    assert digest(task / 'tests/ue' / filename) == digest(task / 'tests/task-package/tests' / filename)
                integration = json.loads((task / 'PUBLIC_INTEGRATION_RECEIPT.json').read_text())
                assert integration['scoring_receipt_sha256'] == digest(task / 'SCORING_RECEIPT.json')
                adaptations = {row['path']: row for row in integration['adaptations']}
                for row in scoring['files']:
                    if row['path'] in adaptations:
                        assert row['sha256'] == adaptations[row['path']]['scoring_revision_sha256']
                    else:
                        assert digest(task / row['path']) == row['sha256'], row['path']
                for row in adaptations.values():
                    assert digest(task / row['path']) == row['release_sha256'], row['path']
            brief = (task / 'instruction.md').read_text(encoding='utf-8')
            assert '/project/gdd.md' in brief and len(brief.split()) <= 110
            assert digest(task / 'gdd.md') == digest(workspace / 'gdd.md') == digest(task / 'environment/protected/gdd.md')
            package = task / 'tests/task-package'
            assert yaml.safe_load((package / 'spec.yaml').read_text(encoding='utf-8'))['instruction'] == brief.strip()
            assert (package / 'instruction.md').read_text(encoding='utf-8') == brief
            if number == 164:
                protected = json.loads((package / 'PROTECTED_PATHS.json').read_text())
                inventory = json.loads((package / 'REFERENCE_INVENTORY.json').read_text())
                assert inventory['gdd.md']['sha256'] == digest(task / 'gdd.md')
                assert 'gdd.md' in (task / 'environment/protected/.protected_paths.list').read_text().splitlines()
            elif number in {168, 169}:
                protected = json.loads((package / 'protection-manifest.json').read_text())['protected']
            else:
                protected = json.loads((package / 'protected-paths.json').read_text())
            assert protected['gdd.md'] == digest(workspace / 'gdd.md')
            receipt = json.loads((task / 'GDD_RECEIPT.json').read_text())
            assert receipt['gdd_sha256'] == digest(task / 'gdd.md')
            assert receipt['instruction_sha256'] == digest(task / 'instruction.md')
            print(f'{task.name}: short brief, project GDD and integrity inventories OK')
        else:
            spec = yaml.safe_load((task / 'tests/source-spec.yaml').read_text(encoding='utf-8'))
            assert spec['task_id'] == f'ue_task_{number:04d}'
            receipt = json.loads((task / 'SOURCE_RECEIPT.json').read_text())
            expected_instruction = public_instruction(spec) if receipt.get('oracle_files') else instruction_text(spec['instruction'].strip(), spec['files_to_edit'])
            assert (task / 'instruction.md').read_text(encoding='utf-8') == expected_instruction
            actual = {p.relative_to(task / 'solution/files').as_posix() for p in (task / 'solution/files').rglob('*') if p.is_file()}
            assert actual == set(receipt.get('oracle_files', spec['files_to_edit'])), task.name
            for rel, sha in receipt.get('oracle_files', {}).items():
                assert source_digest_matches(task / 'solution/files' / rel, sha), (task.name, rel)
                assert any(rel == p or rel.startswith(p + '/') for p in spec.get('editable_paths', spec['files_to_edit'])), rel
                assert not any(rel == p or rel.startswith(p + '/') for p in spec.get('protected_paths', [])), rel
            verifier = json.loads((task / 'tests/config.json').read_text())
            expected = [name for suite in verifier['suites'] for name in suite['expected']]
            assert len(expected) == len(set(expected)) and set(expected) == set(spec['test_requirements'])
            for suite in verifier['suites']:
                assert all(name == suite['filter'] or name.startswith(suite['filter'] + '.') for name in suite['expected'])
                assert '-d3d11' not in suite['flags'] and '-d3d12' not in suite['flags']
            for rel in verifier['protected']:
                assert (task / 'environment/protected' / rel).exists(), (task.name, rel)
            receipt = json.loads((task / 'SOURCE_RECEIPT.json').read_text())
            assert receipt['source_task'] == config['metadata']['source_task']
            for rel, sha in receipt['source_files'].items():
                if rel == 'spec.yaml':
                    path = task / 'tests/source-spec.yaml'
                elif rel.startswith('tests/'):
                    path = task / 'tests/ue' / rel.removeprefix('tests/')
                else:
                    assert rel.startswith('tests_fixture/')
                    path = task / 'tests/fixture_overlay' / rel.removeprefix('tests_fixture/')
                assert source_digest_matches(path, sha), (task.name, rel)
            assert (task / 'tests/verify.py').read_bytes() == (root / 'scripts/unreal_website_verifier.py').read_bytes()
            for path in workspace.rglob('*'):
                if path.is_file():
                    assert path.stat().st_size < 100 * 1024 * 1024, 'Oversized GitHub file: ' + str(path)
                    if path.stat().st_size < 200:
                        assert not path.read_bytes().startswith(b'version https://git-lfs.github.com/spec/v1'), path
            print(f'{task.name}: identity, oracle scope, {len(expected)} tests and source receipts OK')
    print('PASS: exactly 14 website tasks; Harbor schemas and package checks passed.')


if __name__ == '__main__':
    main()

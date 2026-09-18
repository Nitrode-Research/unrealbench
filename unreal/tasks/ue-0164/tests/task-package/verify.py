"""Verify a frozen submission via GEB, enforcing this package's supplied architecture.

python verify.py --source start|solution|ABS_SNAPSHOT --output-dir NEW_DIRECTORY
No solver, judge, shared state mutation or deployment. UE_ENGINE_ROOT must point to
an installed compatible engine. Root coordinator must schedule native execution.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def normalized_hash(path):
    data = path.read_bytes()
    if path.suffix.lower() in {'.cpp', '.h', '.cs', '.ini', '.uproject'}:
        data = data.replace(b'\r\n', b'\n')
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True)
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args()
    task = Path(__file__).resolve().parent
    source = task / args.source if args.source in {'start', 'solution'} else Path(args.source).resolve()
    if not source.is_dir() or args.output_dir.exists():
        parser.error('Existing source and new evidence directory required')
    protected = json.loads((task / 'PROTECTED_PATHS.json').read_text())
    inventory = json.loads((task / 'REFERENCE_INVENTORY.json').read_text())
    violations = []
    for rel in protected:
        actual, baseline = source / rel, task / 'start' / rel
        if not actual.is_file() or actual.is_symlink() or normalized_hash(actual) != normalized_hash(baseline):
            violations.append(rel)
    # Generated build outputs may exist; candidate-authored source/build/config files may not.
    for file in source.rglob('*'):
        if not file.is_file():
            continue
        rel = file.relative_to(source)
        if set(rel.parts) & {'Binaries', 'Intermediate', 'Saved', 'DerivedDataCache', '.git'}:
            continue
        if rel.as_posix() not in inventory:
            violations.append('unexpected:' + rel.as_posix())
    if violations:
        args.output_dir.mkdir(parents=True)
        report = {'status': 'submission_contract_violation', 'complete': False, 'reward_valid': False,
                  'reward': None, 'strict_success': None, 'violations': violations}
        (args.output_dir / 'report.json').write_text(json.dumps(report, indent=2))
        print('Submission changed protected architecture; see report.json')
        return 2
    result = subprocess.run([sys.executable, '-m', 'unrealbench.src.verify_suites', str(task),
                             '--source', str(source), '--output-dir', str(args.output_dir)])
    report_file = args.output_dir / 'report.json'
    if not report_file.is_file():
        return 2
    report = json.loads(report_file.read_text())
    report['diagnostic_fraction'] = report.get('reward')
    report['reward'] = report['diagnostic_fraction'] if report.get('reward_valid') else None
    report['score_policy'] = 'fraction of required behavioral groups; strict success requires all'
    report['protected_path_count'] = len(protected)
    report_file.write_text(json.dumps(report, indent=2))
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())

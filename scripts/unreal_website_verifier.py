"""Harbor entry point for imported NightSkyEngine tasks; standard library only."""
from pathlib import Path
import json
import re
import shutil
import subprocess
import sys

from score_automation import score

CRASH = re.compile(r'Signal \d+ caught|SIGSEGV|SIGABRT|Unhandled Exception|Fatal error:|Assertion failed')


def is_test_source(relative):
    """Identify candidate test translation units, including PlaytestTests folders."""
    return (relative.suffix.lower() == '.cpp' and
            any(part.lower() in {'test', 'tests'} or part.lower().endswith('tests') for part in relative.parts[:-1]))


def assess_suite(directory, expected, text, returncode):
    directory.mkdir(parents=True, exist_ok=True)
    log = directory / 'automation.log'
    log.write_text(text, encoding='utf-8')
    manifest = directory / 'expected.list'
    manifest.write_text('\n'.join(expected) + '\n', encoding='utf-8')
    reward = directory / 'reward.txt'
    reward.unlink(missing_ok=True)
    if CRASH.search(text) or returncode not in {0, 255}:
        return dict(complete=False, error='abnormal_process_exit', exit_code=returncode)
    status = score(log, manifest, reward, directory / 'score.json')
    report = json.loads((directory / 'score.json').read_text(encoding='utf-8'))
    complete = status in {0, 1} and report['reward_valid']
    # UE requests -1 (255 to the OS) if any tests fail, zero otherwise.
    if complete:
        expected_exit = 255 if report['failed'] else 0
        terminal = re.findall(r'LogAutomationCommandLine: Display: \*\*\*\* TEST COMPLETE\. EXIT CODE: (-?\d+) \*\*\*\*', text)
        if returncode != expected_exit or terminal != [str(-1 if report['failed'] else 0)]:
            reward.unlink(missing_ok=True)
            return dict(complete=False, error='inconsistent_exit_status', exit_code=returncode)
    return dict(complete=complete, exit_code=returncode, **report)


def aggregate(records):
    if not records or not all(record.get('complete') for record in records):
        return dict(reward_valid=False, reward=None, status='incomplete_verification', suites=records)
    total = sum(record['total'] for record in records)
    passed = sum(record['passed'] for record in records)
    return dict(reward_valid=True, reward=passed / total, passed=passed, total=total,
                status='complete_behavioral_run', suites=records)


def replace(source, target):
    if target.is_symlink() or target.is_file():
        target.unlink()
    elif target.exists():
        shutil.rmtree(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, target)
    else:
        shutil.copy2(source, target)


def run(command, log, timeout):
    with log.open('w', encoding='utf-8') as output:
        return subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=timeout).returncode


def main():
    tests, project, baseline = Path('/tests'), Path('/project'), Path('/protected-baseline')
    logs, engine = Path('/logs/verifier'), Path('/home/ue4/UnrealEngine')
    logs.mkdir(parents=True, exist_ok=True)
    reward = logs / 'reward.txt'
    reward.unlink(missing_ok=True)
    config = json.loads((tests / 'config.json').read_text())

    def finish(report, code):
        (logs / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
        if report['reward_valid']:
            reward.write_text(f"{report['reward']:.6f}\n")
        else:
            reward.unlink(missing_ok=True)
        return code

    try:
        # Capture the candidate before restoration or injection changes it.
        run([sys.executable, str(tests / 'capture_submission.py'), 'capture', '--project', str(project),
             '--baseline', str(baseline / '.submission-baseline.json'), '--output', '/logs/artifacts/submission'],
            logs / 'submission-capture.log', 300)
        for rel in config['protected']:
            if not (baseline / rel).exists():
                raise FileNotFoundError('Missing protected baseline: ' + rel)
            replace(baseline / rel, project / rel)
        original = set(config['original_tests'])
        for path in project.rglob('*.cpp'):
            rel = path.relative_to(project)
            if is_test_source(rel) and rel.as_posix() not in original:
                path.unlink()
        injection = project / 'Source' / config['module'] / 'Tests'
        if injection.exists():
            shutil.rmtree(injection)
        shutil.copytree(tests / 'ue', injection)
        if (tests / 'fixture_overlay').is_dir():
            shutil.copytree(tests / 'fixture_overlay', project, dirs_exist_ok=True)
        if not list(injection.rglob('*.cpp')):
            raise ValueError('No hidden tests were injected')
        uproject = project / (config['project'] + '.uproject')
        result = run([str(engine / 'Engine/Build/BatchFiles/Linux/Build.sh'), config['target'], 'Linux', 'Development',
                      '-project=' + str(uproject), '-NoUBA', '-MaxParallelActions=4'], logs / 'build.log', 2400)
        if result:
            return finish(dict(reward_valid=True, reward=0, status='compilation_error', suites=[]), 0)
        records = []
        for suite in config['suites']:
            directory = logs / suite['name']
            directory.mkdir(parents=True, exist_ok=True)
            command = [str(engine / 'Engine/Binaries/Linux/UnrealEditor-Cmd'), str(uproject),
                       '-ExecCmds=Automation RunTests ' + suite['filter'] + '; Quit',
                       '-TestExit=Automation Test Queue Empty', *suite['flags'], '-nop4', '-log', '-stdout', '-FullStdOutLogOutput']
            result = run(command, directory / 'process.log', 3600)
            text = (directory / 'process.log').read_text(encoding='utf-8', errors='replace')
            records.append(dict(name=suite['name'], **assess_suite(directory, suite['expected'], text, result)))
        report = aggregate(records)
        return finish(report, 0 if report['reward_valid'] else 2)
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        return finish(dict(reward_valid=False, reward=None, status='infrastructure_error', error=str(error)), 2)


if __name__ == '__main__':
    raise SystemExit(main())

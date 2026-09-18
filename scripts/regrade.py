"""Check recorded inputs, then optionally run Harbor's verifier-only regrade.

The default is read-only. This checks local, single-step Harbor jobs and never
repairs manifests or substitutes a starter for an unrecorded submission.
"""
from __future__ import annotations

import argparse
from importlib.metadata import version
import json
from pathlib import Path
import shlex
import subprocess
import sys
from types import SimpleNamespace

from harbor.models.task.config import TaskConfig
from harbor.models.task.artifacts import validate_artifact_entries
from harbor.models.trial.config import TrialConfig
from harbor.models.trial.paths import EnvironmentPaths
from harbor.models.trial.result import TrialResult
from harbor.trial.regrade import (
    RegradeTrial, check_task_regradable, expand_task_path, local_task_name,
    read_artifact_manifest,
)
try:
    from scripts.standardize_harbor import validate_verifier_build
except ModuleNotFoundError:
    from standardize_harbor import validate_verifier_build


def read_model(path: Path, model):
    try:
        return model.model_validate_json(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        # ValidationError can include input dictionaries containing credentials.
        raise ValueError(f"{path}: cannot read a valid {model.__name__}") from error


def check_job(source: Path, task_paths: list[Path]) -> dict:
    """Check all recorded trials without creating a job or starting containers."""
    source = source.resolve()
    report = dict(source=str(source), ready=False, trials=[], errors=[])
    errors = report["errors"]
    if version("harbor") != "0.23.0":
        errors.append("This preflight requires the repository's pinned Harbor 0.23.0.")
        return report
    if not (source / "config.json").is_file():
        errors.append(f"Not a Harbor job directory (missing config.json): {source}")
        return report

    tasks = {}
    for parent in task_paths:
        try:
            directories = expand_task_path(parent)
        except ValueError as error:
            errors.append(str(error))
            continue
        for directory in directories:
            directory = directory.resolve()
            try:
                config = TaskConfig.model_validate_toml(
                    (directory / "task.toml").read_text(encoding="utf-8")
                )
                name = local_task_name(directory)
            except (OSError, ValueError):
                errors.append(f"Invalid task configuration: {directory}")
                continue
            if name in tasks and tasks[name][0] != directory:
                errors.append(f"Multiple verifier packages have task name {name!r}.")
            tasks[name] = directory, config

    build_checks = {}
    for trial_dir in sorted(path for path in source.iterdir() if path.is_dir()):
        if not (trial_dir / "result.json").is_file():
            if (trial_dir / "config.json").is_file():
                errors.append(f"{trial_dir.name}: incomplete trial (no result.json).")
            continue
        trial = dict(name=trial_dir.name, task=None, ready=False, errors=[])
        report["trials"].append(trial)
        problems = trial["errors"]
        try:
            result = read_model(trial_dir / "result.json", TrialResult)
            trial["task"] = result.task_name
            if result.task_name not in tasks:
                raise ValueError(f"No verifier package for task {result.task_name!r}; names must match exactly.")
            directory, config = tasks[result.task_name]
            trial["verifier"] = str(directory)
            if config.steps or (trial_dir / "steps").exists():
                raise ValueError("This preflight supports single-step jobs only; use native Harbor for multi-step replay.")
            reason = check_task_regradable(directory)
            if reason:
                raise ValueError(reason)
            if directory not in build_checks:
                try:
                    validate_verifier_build(directory)
                    build_checks[directory] = None
                except (OSError, ValueError) as error:
                    build_checks[directory] = str(error)
            if build_checks[directory]:
                raise ValueError(build_checks[directory])
            # Harbor prefers the standalone source config, then falls back to
            # the config embedded in result.json. Preserve job-level artifacts.
            try:
                source_config = read_model(trial_dir / "config.json", TrialConfig)
            except ValueError:
                source_config = result.config
            artifacts = [*config.artifacts, *source_config.artifacts]
            paths = EnvironmentPaths.for_os(config.environment.os)
            validate_artifact_entries(artifacts, convention_source=paths.artifacts_dir.as_posix())
            # Reuse the pinned framework's exact source/destination/status/type/
            # exclusion checks. This method is read-only and only needs the OS
            # paths; constructing a Trial would create logs and environments.
            problems.extend(RegradeTrial._artifact_coverage_problems(
                SimpleNamespace(agent_env_paths=paths),
                source_artifacts_dir=trial_dir / "artifacts",
                source_record_dir=trial_dir,
                entries=read_artifact_manifest(trial_dir),
                declared_artifacts=artifacts,
            ))
        except (OSError, ValueError) as error:
            problems.append(str(error))
        except Exception as error:
            # RegradeError has plain diagnostic text, unlike model errors.
            from harbor.trial.regrade import RegradeError
            if not isinstance(error, RegradeError):
                raise
            problems.append(str(error))
        trial["ready"] = not problems
    if not report["trials"]:
        errors.append("No recorded trials found.")
    report["ready"] = not errors and bool(report["trials"]) and all(t["ready"] for t in report["trials"])
    return report


def command_for(args) -> list[str]:
    command = [sys.executable, "-m", "harbor.cli.main", "job", "regrade", str(args.source.resolve())]
    for task in args.task_path:
        command.extend(["-p", str(task.resolve())])
    command.extend(["-e", args.environment, "-n", str(args.n_concurrent), "--jobs-dir", str(args.jobs_dir.resolve())])
    if args.job_name:
        command.extend(["--job-name", args.job_name])
    return command


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Downloaded/local Harbor job directory")
    parser.add_argument("-p", "--task-path", type=Path, action="append", required=True)
    parser.add_argument("-e", "--environment", default="docker")
    parser.add_argument("-n", "--n-concurrent", type=int, default=1)
    parser.add_argument("--jobs-dir", type=Path, default=Path("jobs/regrades"))
    parser.add_argument("--job-name")
    parser.add_argument("--execute", action="store_true", help="Start native Harbor regrade only after preflight passes")
    parser.add_argument("--json", action="store_true", help="Print the preflight report as JSON")
    args = parser.parse_args(argv)
    if args.n_concurrent < 1:
        parser.error("--n-concurrent must be positive")
    if args.job_name and (Path(args.job_name).name != args.job_name or args.job_name in {".", ".."} or "/" in args.job_name or "\\" in args.job_name):
        parser.error("--job-name must be a single directory name")
    if args.jobs_dir.resolve().is_relative_to(args.source.resolve()):
        parser.error("--jobs-dir must be outside the source job")
    report = check_job(args.source, args.task_path)
    command = command_for(args)
    report["command"] = command
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        for error in report["errors"]:
            print("ERROR: " + error)
        for trial in report["trials"]:
            print(f"{'READY' if trial['ready'] else 'BLOCKED'} {trial['name']}: {trial['task']}")
            for error in trial["errors"]:
                print("  " + error)
        if report["ready"]:
            print("Recorded inputs are compatible. This does not check Docker, engine access, or verifier outcomes.")
            print("Command: " + (subprocess.list2cmdline(command) if sys.platform == "win32" else shlex.join(command)))
    if not report["ready"]:
        return 1
    if args.execute:
        return subprocess.run(command, check=False).returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

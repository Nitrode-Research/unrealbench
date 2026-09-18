#!/usr/bin/env python3
"""Score an Unreal automation run against an exact verifier-owned test manifest."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path


RESULT_RE = re.compile(
    r"LogAutomationController: (?:Display|Error): Test Completed\. "
    r"Result=\{(?P<result>[^}]*)\} Name=\{[^}]*\} Path=\{(?P<path>[^}]*)\}\s*$"
)
COMPLETE_RE = re.compile(
    r"LogAutomationCommandLine: Display: \*\*\*\* TEST COMPLETE\. EXIT CODE: -?\d+ \*\*\*\*\s*$"
)


def _write_reward(path: Path, value: float) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"{value:.4f}\n", encoding="utf-8")


def _invalidate_reward(path: Path) -> None:
    path.unlink(missing_ok=True)


def _write_diagnostic(path: Path | None, report: dict[str, object]) -> None:
    if path is None:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _write_report(reward_path: Path, results: list[tuple[str, str]], problems: list[str]) -> None:
    # No fabricated durations or test denominator when the suite is incomplete.
    cases = [] if problems else [{"name": name, "status": "passed" if outcome == "Success" else "failed"}
                                 for name, outcome in results]
    passed = sum(item["status"] == "passed" for item in cases)
    report = {"reportFormat": "CTRF", "specVersion": "0.0.0", "results": {
        "tool": {"name": "geb-automation"},
        "summary": {"tests": len(cases), "passed": passed, "failed": len(cases) - passed,
                    "skipped": 0, "pending": 0, "other": 0},
        "tests": cases,
        "extra": {"integrity": "invalid" if problems else "complete", "errors": problems,
                  "durationAvailable": False, "failureDetailSource": "automation.log"}}}
    try:
        (reward_path.parent / "test-results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    except OSError as exc:
        # Optional evidence must never alter the already computed reward state.
        print(f"WARNING: could not write test report ({exc}); reward state is unchanged")


def score(
    log_path: Path,
    expected_path: Path,
    reward_path: Path,
    diagnostic_path: Path | None = None,
) -> int:
    expected = [line.strip() for line in expected_path.read_text(encoding="utf-8").splitlines() if line.strip()]
    expected_counts = Counter(expected)
    if not expected or any(count != 1 for count in expected_counts.values()):
        _invalidate_reward(reward_path)
        _write_diagnostic(diagnostic_path, {
            "status": "invalid_verifier_manifest",
            "reward_valid": False,
            "reward": None,
            "diagnostic_fraction": None,
        })
        _write_report(reward_path, [], ["Invalid verifier test manifest"])
        print("VERDICT: invalid verifier test manifest -> reward invalid/null")
        return 2

    lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    completions = [line for line in lines if COMPLETE_RE.search(line)]
    results = [(match.group("path"), match.group("result")) for line in lines if (match := RESULT_RE.search(line))]
    result_counts = Counter(path for path, _ in results)
    expected_set = set(expected)
    actual_set = set(result_counts)

    problems: list[str] = []
    if len(completions) != 1:
        problems.append(f"completion markers={len(completions)} (expected 1)")
    missing = sorted(expected_set - actual_set)
    unexpected = sorted(actual_set - expected_set)
    duplicates = sorted(path for path, count in result_counts.items() if count != 1)
    if missing:
        problems.append("missing=" + ",".join(missing))
    if unexpected:
        problems.append("unexpected=" + ",".join(unexpected))
    if duplicates:
        problems.append("duplicate=" + ",".join(duplicates))
    if len(results) != len(expected):
        problems.append(f"result count={len(results)} (expected {len(expected)})")
    invalid_outcomes = sorted({outcome for _, outcome in results if outcome not in {"Success", "Fail"}})
    if invalid_outcomes:
        problems.append("invalid outcomes=" + ",".join(invalid_outcomes))

    if problems:
        _invalidate_reward(reward_path)
        _write_diagnostic(diagnostic_path, {
            "status": "invalid_result_set",
            "reward_valid": False,
            "reward": None,
            "diagnostic_fraction": None,
            "problems": problems,
        })
        _write_report(reward_path, [], problems)
        print("VERDICT: result-set integrity failure -> reward invalid/null")
        for problem in problems:
            print(f"  {problem}")
        return 2

    passed = sum(result == "Success" for _, result in results)
    failed = len(expected) - passed
    diagnostic_fraction = passed / len(expected)
    reward = diagnostic_fraction
    _write_reward(reward_path, reward)
    _write_diagnostic(diagnostic_path, {
        "status": "complete_behavioral_run",
        "reward_valid": True,
        "reward": reward,
        "strict_success": failed == 0,
        "diagnostic_fraction": diagnostic_fraction,
        "passed": passed,
        "failed": failed,
        "total": len(expected),
    })
    _write_report(reward_path, results, [])
    print(
        f"VERDICT: {passed}/{len(expected)} passed -> reward "
        f"{reward_path.read_text(encoding='utf-8').strip()} "
        f"(diagnostic_fraction={diagnostic_fraction:.4f})"
    )
    return 0 if failed == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--expected", type=Path, required=True)
    parser.add_argument("--reward", type=Path, required=True)
    parser.add_argument("--diagnostic", type=Path)
    args = parser.parse_args()
    try:
        return score(args.log, args.expected, args.reward, args.diagnostic)
    except (OSError, UnicodeError) as exc:
        _invalidate_reward(args.reward)
        _write_diagnostic(args.diagnostic, {
            "status": "scorer_input_failure",
            "reward_valid": False,
            "reward": None,
            "diagnostic_fraction": None,
            "error": str(exc),
        })
        _write_report(args.reward, [], ["Scorer input failure"])
        print(f"VERDICT: scorer input failure ({exc}) -> reward invalid/null")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

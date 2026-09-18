#!/usr/bin/env python3
"""Score an Unreal automation run against an exact verifier-owned test manifest."""

from __future__ import annotations

import argparse
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


def score(log_path: Path, expected_path: Path, reward_path: Path) -> int:
    expected = [line.strip() for line in expected_path.read_text(encoding="utf-8").splitlines() if line.strip()]
    expected_counts = Counter(expected)
    if not expected or any(count != 1 for count in expected_counts.values()):
        _write_reward(reward_path, 0.0)
        print("VERDICT: invalid verifier test manifest -> reward 0")
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

    if problems:
        _write_reward(reward_path, 0.0)
        print("VERDICT: result-set integrity failure -> reward 0")
        for problem in problems:
            print(f"  {problem}")
        return 2

    passed = sum(result == "Success" for _, result in results)
    failed = len(expected) - passed
    _write_reward(reward_path, passed / len(expected))
    print(f"VERDICT: {passed}/{len(expected)} passed -> reward {reward_path.read_text(encoding='utf-8').strip()}")
    return 0 if failed == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--expected", type=Path, required=True)
    parser.add_argument("--reward", type=Path, required=True)
    args = parser.parse_args()
    try:
        return score(args.log, args.expected, args.reward)
    except (OSError, UnicodeError) as exc:
        _write_reward(args.reward, 0.0)
        print(f"VERDICT: scorer input failure ({exc}) -> reward 0")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

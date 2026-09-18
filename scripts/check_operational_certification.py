"""Verify UnrealBench's 13-task operational certification from public evidence.

This check proves that each included task package is the certified byte set and
that a native verifier run executed all declared tests with a valid result. It
does not claim agent quality, benchmark difficulty, or visual quality beyond
the automated checks listed in the evidence receipts.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT if (ROOT / "CERTIFICATION.json").is_file() else ROOT / "release/public"
CERTIFICATE = PUBLIC / "CERTIFICATION.json"


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def package_digest(task: Path) -> tuple[int, str]:
    files = {
        path.relative_to(task).as_posix(): digest(path)
        for path in sorted(task.rglob("*"))
        if path.is_file()
        and "__pycache__" not in path.relative_to(task).parts
        and path.suffix != ".pyc"
    }
    encoded = json.dumps(files, sort_keys=True, separators=(",", ":")).encode()
    return len(files), hashlib.sha256(encoded).hexdigest()


def phase_evidence(task: dict, receipt: dict) -> tuple[float, int, int, bool]:
    name = task["name"]
    phase = task["evidence_phase"]
    if task["evidence_file"] == "validation/cpu-tasks.json":
        row = receipt["tasks"][name]
        result = row["phases"][phase]
        if name == "ue-0164":
            expected = ROOT / "unreal/tasks/ue-0164/tests/expected_reward_tests.list"
            declared = sum(bool(line.strip()) and not line.lstrip().startswith("#")
                           for line in expected.read_text(encoding="utf-8").splitlines())
            return result["reward"], declared, declared, result["exception"] is None
        native = result["native_summary"]
        return result["reward"], native["passed"], native["total"], (
            result["exception"] is None and native["reward_valid"]
        )
    if task["evidence_file"] == "validation/tasks-121-124-125.json":
        row = receipt["tasks"][name]
        if phase == "final_v013_regrade":
            trial = row[phase]["trials"][0]
            native = trial["verification"]
            return trial["reward"], native["passed"], native["total"], (
                trial["exception"] is None and native["reward_valid"]
            )
        result = row["phases"][phase]
        native = result["native_summary"]
        return result["reward"], native["passed"], native["total"], (
            result["exception"] is None and native["reward_valid"]
        )
    if task["evidence_file"] == "validation/task-172-v0.5.0.json":
        result = receipt["phases"][phase]
        return result["reward"], result["passed"], result["passed"] + result["failed"], result["reward_valid"]
    if task["evidence_file"] == "validation/tasks-168-169-171.json":
        result = receipt["tasks"][name][phase]
        lanes = result.get("lanes")
        if lanes:
            passed = sum(lane["passed"] for lane in lanes.values())
            total = sum(lane["expected"] for lane in lanes.values())
        else:
            passed = result["verification"]["passed"]
            total = passed + result["verification"]["failed"]
        valid = result["exception_type"] is None and result["verification"]["reward_valid"]
        return result["reward"], passed, total, valid
    raise ValueError(f"Unknown evidence format for {name}")


def main() -> int:
    certificate = json.loads(CERTIFICATE.read_text(encoding="utf-8"))
    catalog = json.loads((ROOT / "unreal/website-tasks.json").read_text(encoding="utf-8"))
    catalog_names = {entry["name"] for entry in catalog}
    excluded = {row["name"] for row in certificate["excluded_tasks"]}
    tasks = certificate["tasks"]

    assert certificate["certified"] is True
    assert certificate["profile"] == "operational-13-v1"
    assert excluded == {"ue-0123-nse-spectator-rooms"}
    assert {row["name"] for row in tasks} == catalog_names - excluded
    assert len(tasks) == 13

    receipts: dict[str, dict] = {}
    for relative, expected in certificate["evidence_json_sha256"].items():
        path = PUBLIC / relative
        assert path.is_file(), relative
        receipt = json.loads(path.read_text(encoding="utf-8"))
        canonical = json.dumps(receipt, sort_keys=True, separators=(",", ":")).encode()
        assert hashlib.sha256(canonical).hexdigest() == expected, relative
        receipts[relative] = receipt

    for row in tasks:
        task = ROOT / "unreal/tasks" / row["name"]
        count, actual_digest = package_digest(task)
        assert count == row["package_files"], row["name"]
        assert actual_digest == row["package_manifest_sha256"], row["name"]
        reward, passed, total, valid = phase_evidence(row, receipts[row["evidence_file"]])
        assert valid and reward == 1.0, row["name"]
        assert passed == total == row["declared_tests_executed"], row["name"]

    print("PASS: operational-13-v1 certifies 13 exact packages; native verifiers executed all declared tests.")
    print("EXCLUDED: ue-0123-nse-spectator-rooms remains public but uncertified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

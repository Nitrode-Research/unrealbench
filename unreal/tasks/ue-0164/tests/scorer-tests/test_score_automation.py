from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path


SCORER = Path(__file__).resolve().parents[1] / "score_automation.py"
SPEC = importlib.util.spec_from_file_location("score_automation", SCORER)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def result(path: str, outcome: str = "Success") -> str:
    level = "Display" if outcome == "Success" else "Error"
    return (
        f"[2026.09.15-00.00.00:000][1]LogAutomationController: {level}: "
        f"Test Completed. Result={{{outcome}}} Name={{Case}} Path={{{path}}}"
    )


COMPLETE = (
    "[2026.09.15-00.00.01:000][2]LogAutomationCommandLine: Display: "
    "**** TEST COMPLETE. EXIT CODE: 0 ****"
)


class ScoreAutomationTests(unittest.TestCase):
    def test_report_failure_preserves_reward_and_exit_code(self) -> None:
        for outcome, code, reward in [("Success", 0, "1.0000"), ("Fail", 1, "0.5000")]:
            with self.subTest(outcome=outcome), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "expected.txt").write_text("Suite.A\nSuite.B\n")
                (root / "automation.log").write_text("\n".join([result("Suite.A"), result("Suite.B", outcome), COMPLETE]))
                # Reproduce an unwritable optional report independently of reward.txt.
                (root / "test-results.json").mkdir()
                with patch("sys.argv", ["score", "--log", str(root / "automation.log"), "--expected", str(root / "expected.txt"), "--reward", str(root / "reward.txt"), "--diagnostic", str(root / "diagnostic.json")]):
                    self.assertEqual(MODULE.main(), code)
                self.assertEqual((root / "reward.txt").read_text().strip(), reward)
                self.assertEqual(json.loads((root / "diagnostic.json").read_text())["reward_valid"], True)

    def test_report_failure_does_not_hide_invalid_or_missing_input(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "test-results.json").mkdir()
            (root / "expected.txt").write_text("Suite.A\n")
            args = ["score", "--log", str(root / "automation.log"), "--expected", str(root / "expected.txt"), "--reward", str(root / "reward.txt"), "--diagnostic", str(root / "diagnostic.json")]
            for log in (None, COMPLETE):
                if log is not None:
                    (root / "automation.log").write_text(log)
                with patch("sys.argv", args):
                    self.assertEqual(MODULE.main(), 2)
                self.assertFalse((root / "reward.txt").exists())
                self.assertEqual(json.loads((root / "diagnostic.json").read_text())["reward_valid"], False)

    def run_case(
        self, lines: list[str], expected: list[str] | None = None
    ) -> tuple[int, str | None, dict[str, object]]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "expected.txt").write_text("\n".join(expected or ["Suite.A", "Suite.B"]) + "\n", encoding="utf-8")
            (root / "automation.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
            reward = root / "reward.txt"
            diagnostic = root / "diagnostic.json"
            reward.write_text("stale\n", encoding="utf-8")
            rc = MODULE.score(root / "automation.log", root / "expected.txt", reward, diagnostic)
            report = json.loads((root / "test-results.json").read_text())
            self.assertEqual(report["reportFormat"], "CTRF")
            self.assertEqual(report["results"]["extra"]["integrity"], "invalid" if rc == 2 else "complete")
            self.assertEqual(report["results"]["summary"]["tests"], 0 if rc == 2 else len(expected or ["Suite.A", "Suite.B"]))
            value = reward.read_text(encoding="utf-8").strip() if reward.exists() else None
            return rc, value, json.loads(diagnostic.read_text(encoding="utf-8"))

    def test_all_expected_results_pass(self) -> None:
        rc, reward, report = self.run_case([result("Suite.A"), result("Suite.B"), COMPLETE])
        self.assertEqual((0, "1.0000"), (rc, reward))
        self.assertEqual(1.0, report["diagnostic_fraction"])
        self.assertIs(True, report["strict_success"])

    def test_valid_failures_emit_partial_reward_and_false_strict_success(self) -> None:
        rc, reward, report = self.run_case([result("Suite.A"), result("Suite.B", "Fail"), COMPLETE])
        self.assertEqual((1, "0.5000"), (rc, reward))
        self.assertEqual(0.5, report["reward"])
        self.assertEqual(0.5, report["diagnostic_fraction"])
        self.assertIs(False, report["strict_success"])

    def test_unknown_reward_hack_line_voids_run(self) -> None:
        lines = [result("Suite.A"), result("Suite.B"), result("Attacker.Fake"), COMPLETE]
        self.assertEqual((2, None), self.run_case(lines)[:2])

    def test_duplicate_expected_line_voids_run(self) -> None:
        lines = [result("Suite.A"), result("Suite.A", "Fail"), result("Suite.B"), COMPLETE]
        self.assertEqual((2, None), self.run_case(lines)[:2])

    def test_missing_result_voids_run(self) -> None:
        self.assertEqual((2, None), self.run_case([result("Suite.A"), COMPLETE])[:2])

    def test_missing_or_duplicate_completion_marker_voids_run(self) -> None:
        self.assertEqual((2, None), self.run_case([result("Suite.A"), result("Suite.B")])[:2])
        self.assertEqual((2, None), self.run_case([result("Suite.A"), result("Suite.B"), COMPLETE, COMPLETE])[:2])

    def test_unknown_result_status_voids_run(self) -> None:
        self.assertEqual(
            (2, None),
            self.run_case([result("Suite.A"), result("Suite.B", "NotRun"), COMPLETE])[:2],
        )


if __name__ == "__main__":
    unittest.main()

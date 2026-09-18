"""Withhold task124's reward when its required independent HUD review expires."""
import json
from pathlib import Path
import re


REVIEW_EXPIRED = re.compile(
    r"^.*?LogAutomationController: Error: HUD image review deadline expired; evidence: ([^\r\n]+)$",
    re.MULTILINE,
)


def withhold_unavailable_review(logs):
    """Keep original scores as diagnostics; an unanswered review is not a score."""
    process_log = logs / "rendered/process.log"
    if not process_log.is_file():
        return False
    directories = REVIEW_EXPIRED.findall(process_log.read_text(encoding="utf-8", errors="replace"))
    if not directories:
        return False
    # Remove publishable rewards before attempting to rewrite the report.
    (logs / "reward.txt").unlink(missing_ok=True)
    (logs / "rendered/reward.txt").unlink(missing_ok=True)
    report_path = logs / "verification.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    report.update(
        reward_valid=False,
        reward=None,
        status="independent_review_unavailable",
        independent_review={
            "error": "Required HUD observations were not completed within the original scenario deadline",
            "process_log": "rendered/process.log",
            "capture_directories": list(dict.fromkeys(d.strip() for d in directories)),
            "scores_are_diagnostic_only": True,
        },
    )
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return True


def main(logs=Path("/logs/verifier")):
    import verify

    code = verify.main()
    return 2 if withhold_unavailable_review(logs) else code


if __name__ == "__main__":
    raise SystemExit(main())

"""Replay actual combined failures while retaining ordered lifecycle prerequisites.
Every trial keeps the three-match, seat/content/travel/recovery program intact;
only generated commands can be removed. A bounded incomplete reduction is not a
minimality claim. Run each trial through the normal exclusive editor governor.
"""

import argparse, json, os, signal, subprocess, sys, time
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument("project", type=Path)
    p.add_argument("directory", type=Path)
    p.add_argument("--editor", type=Path, required=True)
    p.add_argument("--failure-directory", type=Path, required=True)
    p.add_argument("--max-trials", type=int, default=24)
    p.add_argument("--case-timeout", type=float, default=7200)
    p.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    a = p.parse_args()
    original = json.loads((a.failure_directory / "combined-failure.json").read_text())
    plan = json.loads((a.failure_directory / "combined-plan.json").read_text())
    if original["kind"] != "assertion" or not {"drop", "delay"}.issubset(
        original["fault_classes"]
    ):
        raise SystemExit(
            "Requires an actual behavioral failure with observed packet loss and delayed delivery"
        )
    a.directory.mkdir(parents=True, exist_ok=True)
    best = plan["actions"]
    trials = []
    minimal = False

    def save():
        (a.directory / "smallest-combined-plan.json").write_text(
            json.dumps(dict(plan, actions=best), indent=2)
        )
        (a.directory / "shrink-result.json").write_text(
            json.dumps(
                dict(
                    kind="failure-reproducer",
                    one_minimal=minimal,
                    actions=len(best),
                    trials=trials,
                    preserved_milestones=original["milestones"],
                    required_fault_classes=original["fault_classes"],
                ),
                indent=2,
            )
        )

    def reproduce(actions):
        if len(trials) >= a.max_trials:
            return None
        trial = a.directory / f"trial-{len(trials):03d}"
        trial.mkdir()
        candidate = trial / "plan.json"
        candidate.write_text(json.dumps(dict(plan, actions=actions)))
        command = [
            sys.executable,
            str(Path(__file__).with_name("room_lifecycle_scenario.py")),
            str(a.project),
            str(trial / "run"),
            "--editor",
            str(a.editor),
            "--editor-args",
            a.editor_args,
            "--combined",
            "--combined-plan",
            str(candidate),
            "--delay",
            str(plan["delay"]),
        ]
        started = time.monotonic()
        timeout = False
        with (trial / "driver.log").open("w") as log:
            kwargs = (
                {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
                if os.name == "nt"
                else {"start_new_session": True}
            )
            child = subprocess.Popen(
                command, stdout=log, stderr=subprocess.STDOUT, **kwargs
            )
            try:
                code = child.wait(timeout=a.case_timeout)
            except subprocess.TimeoutExpired:
                timeout = True
                if os.name == "nt":
                    subprocess.run(
                        ["taskkill", "/PID", str(child.pid), "/T", "/F"],
                        stdout=log,
                        stderr=subprocess.STDOUT,
                        timeout=30,
                    )
                else:
                    os.killpg(child.pid, signal.SIGKILL)
                child.wait(timeout=30)
                code = -1
        path = trial / "run/combined-failure.json"
        failure = json.loads(path.read_text()) if path.exists() else {}
        match = (
            not timeout
            and failure.get("signature") == original["signature"]
            and failure.get("milestones") == original["milestones"]
            and set(original["fault_classes"]).issubset(
                failure.get("fault_classes", [])
            )
            and set(original["recoveries"]).issubset(failure.get("recoveries", []))
            and failure.get("fault_context") == original.get("fault_context")
            and set(original.get("causal_faults", [])).issubset(
                failure.get("causal_faults", [])
            )
        )
        result = trial / "run/result.json"
        healthy = result.exists() and json.loads(result.read_text()).get("passed")
        trials.append(
            dict(
                actions=len(actions),
                exit_code=code,
                seconds=time.monotonic() - started,
                reproduced=match,
                timeout=timeout,
                directory=str(trial),
            )
        )
        save()
        if timeout or not (match or healthy or failure.get("kind") == "assertion"):
            raise RuntimeError(
                "Infrastructure failure during reduction; best plan and exact packet traces preserved"
            )
        return match

    save()
    if reproduce(best) is not True:
        raise SystemExit(
            "Original failure did not reproduce with ordered prerequisites and actual faults"
        )
    width = max(1, len(best) // 2)
    while width and len(trials) < a.max_trials:
        changed = False
        offset = 0
        while offset < len(best) and len(trials) < a.max_trials:
            candidate = best[:offset] + best[offset + width :]
            if reproduce(candidate):
                best = candidate
                changed = True
                save()
            else:
                offset += width
        if width == 1:
            minimal = not changed and len(trials) < a.max_trials
            if not changed:
                break
        else:
            width = max(1, width // 2)
    save()
    return 0 if minimal else 3


if __name__ == "__main__":
    raise SystemExit(main())

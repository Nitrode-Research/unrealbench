"""Reduce an actual six-process fuzz failure while retaining causal fault evidence.
Run under the same exclusive launcher as the original case. Each trial starts fresh
participants and replays the fixed combat prefix, room clock and symbolic actions.
A timeout is infrastructure failure and is never evidence that a candidate passes.
"""

import argparse, json, math, os, signal, subprocess, sys, time
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument("project", type=Path)
    p.add_argument("directory", type=Path)
    p.add_argument("--editor", type=Path, required=True)
    p.add_argument("--failure-directory", type=Path, required=True)
    p.add_argument("--max-trials", type=int, default=24)
    p.add_argument("--case-timeout", type=float, default=1500)
    p.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    a = p.parse_args()
    a.directory.mkdir(parents=True, exist_ok=True)
    source = json.loads((a.failure_directory / "failure.json").read_text())
    plan = json.loads((a.failure_directory / "failing-plan.json").read_text())
    config = json.loads((a.failure_directory / "scenario-config.json").read_text())
    if source.get("kind") != "assertion" or not source.get("observed_fault_logs"):
        raise SystemExit(
            "Only an actual behavioral failure with observed loss/delay can be causally reduced"
        )
    required_restarts = {(e["seed"], e["step"]) for e in source.get("restarts", [])}
    trials = []
    best = list(plan["actions"])
    one_minimal = False
    infrastructure = False

    def save():
        (a.directory / "smallest-reproducer.json").write_text(
            json.dumps(dict(plan, actions=best, shrinking=True), indent=2)
        )
        (a.directory / "shrink-result.json").write_text(
            json.dumps(
                dict(
                    passed=False,
                    kind="failure-reproducer",
                    original_actions=len(plan["actions"]),
                    remaining_actions=len(best),
                    one_minimal=one_minimal,
                    infrastructure_failure=infrastructure,
                    signature=source["signature"],
                    preserved_restart_events=sorted(required_restarts),
                    trials=trials,
                ),
                indent=2,
            )
        )

    def reproduces(actions):
        nonlocal infrastructure
        if len(trials) >= a.max_trials:
            return None
        # Membership generations and nonces are resolved in the fresh run. Their
        # causal action identities remain the original seed/step pairs.
        retained = {(row["seed"], row["step"]) for row in actions if row.get("restart")}
        if not required_restarts.issubset(retained):
            return False
        trial = a.directory / f"trial-{len(trials):03d}"
        trial.mkdir()
        candidate = trial / "plan.json"
        candidate.write_text(
            json.dumps(dict(plan, actions=actions, shrinking=True), indent=2)
        )
        command = [
            sys.executable,
            str(Path(__file__).with_name("room_network_scenario.py")),
            str(a.project),
            str(trial / "run"),
            "--editor",
            str(a.editor),
            "--editor-args",
            a.editor_args,
            "--delay",
            str(config["delay"]),
            "--battle-seed",
            str(config["battle_seed"]),
            "--fuzz-plan",
            str(candidate),
        ]
        start = time.monotonic()
        timed_out = False
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
                timed_out = True
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
        path = trial / "run/failure.json"
        failure = json.loads(path.read_text()) if path.exists() else {}
        repeated = {(e["seed"], e["step"]) for e in failure.get("restarts", [])}
        match = (
            not timed_out
            and failure.get("kind") == "assertion"
            and failure.get("signature") == source["signature"]
            and bool(failure.get("observed_fault_logs"))
            and required_restarts.issubset(repeated)
            and set(source.get("fault_classes", [])).issubset(
                failure.get("fault_classes", [])
            )
        )
        healthy = (trial / "run/result.json").exists() and json.loads(
            (trial / "run/result.json").read_text()
        ).get("passed")
        known_behavior = failure.get("kind") == "assertion"
        trials.append(
            dict(
                directory=str(trial),
                actions=len(actions),
                exit_code=code,
                seconds=time.monotonic() - start,
                reproduced=match,
                timeout=timed_out,
            )
        )
        if timed_out or not (match or healthy or known_behavior):
            infrastructure = True
            save()
            raise RuntimeError(
                "Shrinking stopped on infrastructure failure; best reproducer preserved"
            )
        save()
        return match

    save()
    if not reproduces(best):
        raise SystemExit(
            "Original behavioral failure did not reproduce with its causal fault evidence"
        )
    granularity = 2
    while len(best) > 1 and len(trials) < a.max_trials:
        size = math.ceil(len(best) / granularity)
        reduced = False
        exhausted = False
        for offset in range(0, len(best), size):
            candidate = best[:offset] + best[offset + size :]
            outcome = reproduces(candidate)
            if outcome is None:
                exhausted = True
                break
            if outcome:
                best = candidate
                granularity = max(2, granularity - 1)
                reduced = True
                save()
                break
        if exhausted:
            break
        if reduced:
            continue
        if granularity >= len(best):
            one_minimal = True
            break
        granularity = min(len(best), granularity * 2)
    if len(best) == 1:
        empty = reproduces([])
        if empty is False:
            one_minimal = True
        elif empty is True:
            best = []
            one_minimal = True
    if not best:
        one_minimal = True
    save()
    print(
        json.dumps(
            dict(
                remaining_actions=len(best), one_minimal=one_minimal, trials=len(trials)
            )
        )
    )
    return 0 if one_minimal else 3


if __name__ == "__main__":
    raise SystemExit(main())

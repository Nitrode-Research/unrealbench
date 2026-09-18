"""Metamorphic real-process histories under independent delay/seed schedules."""

import argparse, json, os, signal, subprocess, sys, time
from pathlib import Path
from room_network_workers import WorkerGroup
from room_input_challenges import make_initial_inputs


def main():
    p = argparse.ArgumentParser()
    p.add_argument("project", type=Path)
    p.add_argument("directory", type=Path)
    p.add_argument("--editor", type=Path, required=True)
    p.add_argument("--case-timeout", type=float, default=1500)
    p.add_argument("--campaign-budget", type=float, default=0)
    p.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    a = p.parse_args()
    a.directory.mkdir(parents=True, exist_ok=True)
    runs = []
    assertions = 0
    began = time.monotonic()
    group = None
    input_plan = a.directory / "initial-input-plan.json"
    input_plan.write_text(json.dumps(make_initial_inputs()))
    try:
        children = [
            ("delay-0-seed-9009", 9009, ["--cases", "0,600", "--battle-seed", "9009"]),
            ("delay-1-seed-1", 1, ["--delay", "1", "--battle-seed", "1"]),
        ]
        for name, seed, extra in children:
            directory = a.directory / name
            command = [
                sys.executable,
                str(Path(__file__).with_name("room_network_scenario.py")),
                str(a.project),
                str(directory),
                "--editor",
                str(a.editor),
                "--editor-args",
                a.editor_args,
                *extra,
                "--held-out-only",
                "--no-recovery",
                "--input-plan",
                str(input_plan),
            ]
            with (a.directory / (name + "-driver.log")).open("w") as log:
                kwargs = (
                    {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
                    if os.name == "nt"
                    else {"start_new_session": True}
                )
                child = subprocess.Popen(
                    command, stdout=log, stderr=subprocess.STDOUT, **kwargs
                )
                try:
                    remaining = (
                        a.campaign_budget - (time.monotonic() - began) - 45
                        if a.campaign_budget
                        else a.case_timeout
                    )
                    if remaining <= 0:
                        raise subprocess.TimeoutExpired(command, 0)
                    code = child.wait(timeout=min(a.case_timeout, remaining))
                except subprocess.TimeoutExpired:
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
                    raise TimeoutError(
                        "bounded metamorphic child scenario did not complete"
                    )
            result = (
                json.loads((directory / "result.json").read_text())
                if (directory / "result.json").exists()
                else {}
            )
            assert (
                code == 0 and result.get("passed") and result.get("assertions", 0) > 0
            ), "complete held-out process case must pass before relation comparison"
            assertions += result["assertions"]
            for case in result["cases"]:
                runs.append(
                    dict(
                        delay=case["delay"],
                        seed=seed,
                        directory=str(directory),
                        match=case["match"],
                        frames=case["frames"],
                        confirmed=case["confirmed"],
                        semantic=case["semantic"],
                    )
                )
        ledgers = [
            json.loads((Path(run["directory"]) / run["semantic"]).read_text())
            for run in runs
        ]
        assert (
            ledgers[0] and ledgers[0] == ledgers[1]
        ), "same input/seed under different spectator delays/navigation/fault schedules preserves full native fighter trajectory"
        assertions += 1
        assert set(ledgers[0]) == set(
            ledgers[2]
        ), "different-seed positive control covers the same confirmed frame labels"
        assertions += 1
        assert any(
            ledgers[0][frame]["players"][1]["health"]
            != ledgers[2][frame]["players"][1]["health"]
            for frame in ledgers[0]
        ), "known distinct projectile seed changes actual damage, defeating constant-state or hash-only checks"
        assertions += 1
        group = WorkerGroup(
            a.editor,
            a.project,
            a.directory / "offline-replay",
            editor_args=a.editor_args.split(),
            battle_seed=7,
            extra_args=["-NullRHI"],
        )
        group._launch(
            "offline",
            "/Engine/Maps/Entry?game=/Script/NightSkyEngine.RoomWorkerGameMode",
        )
        group.wait_for(
            lambda: any(e.get("type") == "heartbeat" for e in group.events("offline")),
            "fresh offline process initialized",
        )
        hello = next(e for e in group.events("offline") if e.get("type") == "heartbeat")
        assert (
            hello["configuration_seed"] == 7
        ), "fresh offline process starts with unrelated ambient RNG configuration"
        assertions += 1
        for run in runs:
            confirmed = {
                int(frame): value
                for frame, value in json.loads(
                    (Path(run["directory"]) / run["confirmed"]).read_text()
                ).items()
            }
            semantic = json.loads(
                (Path(run["directory"]) / run["semantic"]).read_text()
            )
            offset = len(group.events("offline"))
            group.command("offline", "replay", match=run["match"])
            group.wait_for(
                lambda offset=offset: any(
                    e.get("type") == "offline-ready"
                    for e in group.events("offline")[offset:]
                ),
                "ordinary saved replay loaded",
            )
            for frame in range(1, run["frames"] + 1):
                group.command("offline", "replay-step")
                played = next(
                    e
                    for e in reversed(group.events("offline")[offset:])
                    if e.get("type") == "offline-frame"
                )
                assert (
                    played["frame"] == frame
                ), "existing sequential replay advances one native frame"
                assertions += 1
                assert (played["x"], played["health"]) == tuple(
                    confirmed[frame]
                ), "fresh offline native battle equals original fighter ledger"
                assertions += 1
                assert (
                    played["battle_state"] == semantic[str(frame)]
                ), "fresh offline full fighter, input, meter, timer, animation and projectile state equals authority"
                assertions += 1
                assert (
                    played["seed"] == run["seed"]
                ), "initial random seed survives fresh process export loading"
                assertions += 1
        (a.directory / "result.json").write_text(
            json.dumps(
                dict(
                    passed=True,
                    assertions=assertions,
                    runs=runs,
                    scope="held-out real-process delay/navigation metamorphism and distinct-seed damage",
                ),
                indent=2,
            )
        )
    finally:
        if group is not None:
            group.close()
        (a.directory / "campaign-runs.json").write_text(json.dumps(runs, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""One muted offline UE child replays a public export against its original native ledger."""

import argparse
import json
from pathlib import Path
from room_network_workers import WorkerGroup


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text(encoding="utf-8-sig"))
    group = WorkerGroup(
        args.editor,
        args.project,
        args.directory,
        editor_args=args.editor_args.split(),
        battle_seed=7,
    )
    assertions = 0

    def check(condition, message):
        nonlocal assertions
        assert condition, message
        assertions += 1

    try:
        group._launch(
            "offline",
            "/Engine/Maps/Entry?game=/Script/NightSkyEngine.RoomWorkerGameMode",
        )
        group.wait_for(
            lambda: any(e.get("type") == "heartbeat" for e in group.events("offline")),
            "fresh offline process initialized",
        )
        hello = next(e for e in group.events("offline") if e.get("type") == "heartbeat")
        check(
            hello["configuration_seed"] == 7
            and hello["configuration_seed"] != fixture["seed"],
            "fresh process starts with an unrelated seed before replay load",
        )
        check(
            hello["pid"] != fixture["recorder_pid"],
            "playback is a distinct fresh Unreal process",
        )
        check(
            hello["net_mode"] == 0 and not hello.get("server_connection"),
            "playback is offline without a room connection",
        )
        group.command("offline", "inspect-replay", match=fixture["match"])
        artifact = next(
            e for e in group.events("offline") if e.get("type") == "artifact"
        )
        check(
            artifact["match"] == fixture["match"]
            and artifact["outcome"] == fixture["outcome"],
            "saved identity and interrupted outcome survive fresh loading",
        )
        check(
            artifact["frames"] == 241 and artifact["participants"] == 2,
            "saved match retains frame zero, all inputs and participants",
        )
        group.command("offline", "replay", match=fixture["match"])
        group.wait_for(
            lambda: any(
                e.get("type") == "offline-ready" for e in group.events("offline")
            ),
            "saved replay opened its native stage",
        )
        check(
            len(fixture["frames"]) == 240,
            "complete original native ledger has every input pair",
        )
        check(
            any(row["health"] < 10000 for row in fixture["frames"]),
            "original ledger contains real hit damage",
        )
        check(
            len({row["x"] for row in fixture["frames"]}) > 1,
            "original ledger contains real movement",
        )
        for expected in fixture["frames"]:
            group.command("offline", "replay-step")
            actual = next(
                e
                for e in reversed(group.events("offline"))
                if e.get("type") == "offline-frame"
            )
            check(
                actual["frame"] == expected["frame"],
                "sequential native replay advances exactly one frame",
            )
            check(
                all(
                    actual[field] == expected[field]
                    for field in [
                        "x",
                        "health",
                        "p2x",
                        "p1health",
                        "input1",
                        "input2",
                        "timer",
                        "rng",
                        "meter1",
                        "meter2",
                        "phase",
                    ]
                ),
                "fresh native positions, health, inputs, timer, RNG, meters and phase equal original authority frame",
            )
            check(
                actual["battle_state"] == expected["battle_state"],
                "fresh native animation, velocities and active projectile phase equal the complete original authority ledger",
            )
            check(
                actual["seed"] == fixture["seed"] == 9009,
                "nondefault initial RNG survives durable export and fresh loading",
            )
        (args.directory / "result.json").write_text(
            json.dumps(
                {
                    "passed": True,
                    "assertions": assertions,
                    "scope": "independent fresh offline replay; six-participant room validation remains separate",
                },
                indent=2,
            )
        )
    finally:
        group.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

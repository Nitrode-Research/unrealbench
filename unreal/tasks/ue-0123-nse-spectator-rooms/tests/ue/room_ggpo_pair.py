from room_battle_preparation import prepare_battle
from room_response_contract import annotate_response, prior_delivery, response_matches, await_successful_start
"""Real two-process GGPO correction, independent authority and pause regression.
Run outside an automation editor to fit two total Unreal processes. The full
six-participant authority/DVR scenario is a separate required test.
"""

import argparse
import json
import time
import uuid
from pathlib import Path
from room_network_workers import WorkerGroup, observed_corrections


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    args = parser.parse_args()
    group = WorkerGroup(
        args.editor,
        args.project,
        args.directory,
        editor_args=args.editor_args.split(),
        extra_args=["-NullRHI"],
    )
    assertions = 0
    began = time.monotonic()

    def check(condition, message):
        nonlocal assertions
        assert condition, message
        assertions += 1

    def room(role, operation, **values):
        if values.pop("_observe_before", False):
            room(role, "observe")
        response_before = prior_delivery(group, role)
        nonce = uuid.uuid4().hex
        group.command(role, "room", operation=operation, nonce=nonce, **values)
        group.wait_for(
            lambda: any(
                e.get("type") == "delivery" and e.get("nonce") == nonce
                for e in group.events(role)
            ),
            role + " " + operation,
            timeout=30,
        )
        return annotate_response(next(
            e
            for e in reversed(group.events(role))
            if e.get("type") == "delivery" and e.get("nonce") == nonce
        ), response_before, operation, values)

    try:
        group._launch(
            "server",
            "/Engine/Maps/Entry?listen?game=/Script/NightSkyEngine.RoomWorkerGameMode",
            listen=True,
        )
        group.wait_for(
            lambda: any(e.get("net_mode") == 2 for e in group.events("server")),
            "listen authority initialized",
            timeout=60,
        )
        group.add_client()
        group.wait_for(
            lambda: any(e.get("server_connection") for e in group.events("client0")),
            "real remote connection",
            timeout=60,
        )
        check(
            len(
                {
                    next(e["pid"] for e in group.events(role) if "pid" in e)
                    for role in ("server", "client0")
                }
            )
            == 2,
            "two distinct Unreal processes",
        )
        group.command("server", "create", slot="ggpo-" + uuid.uuid4().hex, delay=0)
        package = "/Game/RoomProcess/" + uuid.uuid4().hex + "/"
        for role in ("server", "client0"):
            group.command(role, "content", package=package)
            group.command(
                role, "authenticate", identity="host" if role == "server" else "peer"
            )
            group.wait_for(
                lambda role=role: any(e.get("room") for e in group.events(role)),
                role + " authenticated",
            )
        assignments = []
        for seat, role in enumerate(("server", "client0")):
            identity = "host" if seat == 0 else "peer"
            check(response_matches(room(role, "queue"), "accepted"), "fighter queued")
            check(
                response_matches(room("server", "offer", number=seat, value=identity), "accepted"),
                "organizer offered seat",
            )
            group.wait_for(
                lambda role=role: any(e.get("offer") for e in group.events(role)),
                role + " offer",
            )
            offer = next(
                e["offer"] for e in reversed(group.events(role)) if e.get("offer")
            )
            assignment = room(role, "accept", assignment=offer)["assignment"]
            assignments.append(assignment)
            check(
                response_matches(room(
                    role,
                    "character",
                    assignment=assignment,
                    value=package + "Character.Character",
                ), "accepted"),
                "fighter selected real content",
            )
        check(
            response_matches(room("server", "stage", value=package + "Stage.Stage"), "accepted"),
            "organizer selected native stage",
        )
        prepare_battle(group, ("client0",), fighters=("host", "peer"))
        for seat, role in enumerate(("server", "client0")):
            current = room(role, "observe")
            check(
                bool(current.get("membership"))
                and (current.get("member_role") == "fighter"
                     or (role == "server" and current.get("member_role") == "organizer"))
                and bool(current.get("assignment")),
                "prepared current connection retains owned fighter role",
            )
            assignments[seat] = current["assignment"]
            check(
                response_matches(room(role, "ready", assignment=assignments[seat]), "accepted"),
                "current fighter ready",
            )
        check(len(set(assignments)) == 2, "prepared fighters have distinct current assignments")
        group.command("server", "clock", tick=0)
        check(room("server", "observe").get("active_match") is False,
              "final readiness leaves match inactive before organizer start")
        started = await_successful_start(group, "server", room("server", "start"), room)
        check(response_matches(started, "accepted"), "authority starts battle")
        match = started["match"]
        for role in ("server", "client0"):
            group.wait_for(
                lambda role=role: any(
                    e.get("prediction_frame") == 0 for e in group.events(role)
                ),
                role + " actual GGPO world initialized",
                timeout=60,
            )
        history = []
        object_history = []
        semantic_history = {}
        for frame in range(1, 81):
            if frame == 20:
                group.proxies[0].configure(
                    to_server=80, to_client=70, jitter=20, drop_every=5
                )
            if frame == 65:
                group.proxies[0].configure()
            group.command("server", "clock", tick=frame)
            for seat, role in enumerate(("server", "client0")):
                bits = (
                    (8 | (32 if frame % 20 < 8 else 0))
                    if seat == 0
                    else (4 if frame % 17 < 4 else 0)
                )
                if seat == 0 and frame in (20, 70):
                    bits |= 64
                check(
                    response_matches(room(
                        role,
                        "input",
                        match=match,
                        assignment=assignments[seat],
                        number=frame,
                        value=str(bits),
                    ), "accepted"),
                    "owner input accepted",
                )
            state = group.command("server", "step")
            check(
                state["battle_frame"] == frame,
                "independent authority advanced one confirmed pair",
            )
            history.append((state["battle_x"], state["battle_health"]))
            object_history.append(state["battle_objects"])
            semantic_history[frame] = state["battle_state"]
            for role in ("server", "client0"):
                group.wait_for(
                    lambda role=role: any(
                        e.get("prediction_frame") == frame
                        and e.get("prediction_confirmed", -1) >= frame
                        for e in group.events(role)
                    ),
                    role + " real GGPO confirmed frame",
                    timeout=30,
                )
                predicted = next(
                    e
                    for e in reversed(group.events(role))
                    if e.get("prediction_frame") == frame
                    and e.get("prediction_confirmed", -1) >= frame
                )
                check(
                    (predicted["prediction_x"], predicted["prediction_health"])
                    == history[-1],
                    "corrected native GGPO state matches independent authority",
                )
                check(
                    predicted["prediction_objects"] == object_history[-1],
                    "corrected projectile identity, age, position and hitstop match independent authority",
                )
            if frame == 40:
                check(
                    response_matches(room("server", "combat-pause"), "accepted"),
                    "organizer pauses combat",
                )
                check(
                    response_matches(room(
                        "client0",
                        "input",
                        match=match,
                        assignment=assignments[1],
                        number=41,
                        value="32",
                     _observe_before=True), "input rejected"),
                    "pause rejects additional combat input",
                )
                for _ in range(5):
                    paused = group.command("server", "step")
                    check(
                        paused["battle_frame"] == 40,
                        "paused authority simulation remains frozen",
                    )
                check(
                    response_matches(room("server", "combat-resume"), "accepted"),
                    "organizer resumes combat",
                )
        check(
            any(object_history),
            "actual native projectiles were active during GGPO combat",
        )
        check(any(h < 10000 for x, h in history), "actual combat produced damage")
        check(len({x for x, h in history}) > 1, "actual combat produced movement")
        rollbacks = sum(
            max(e.get("prediction_rollbacks", 0) for e in group.events(role))
            for role in ("server", "client0")
        )
        corrections = [
            correction
            for role in ("server", "client0")
            for correction in observed_corrections(group.events(role))
        ]
        # Speculation is optional; lockstep implementations may report no corrections.
        for correction in corrections:
            check(
                correction["after"] == semantic_history[correction["frame"]],
                "observed correction converges to independent confirmed gameplay",
            )
        (args.directory / "observed-corrections.json").write_text(
            json.dumps(corrections, indent=2)
        )
        packets = [
            json.loads(line)
            for line in (args.directory / "packets-client0.jsonl")
            .read_text()
            .splitlines()
        ]
        check(
            any(p.get("action") == "drop" for p in packets),
            "external boundary actually dropped packets",
        )
        check(
            any(
                p.get("action") == "deliver"
                and p.get("scheduled_delay", 0) >= 0.05
                and p["delivered"] - p["received"] >= 0.05
                for p in packets
            ),
            "external boundary actually delayed delivered packets",
        )
        departed = room("client0", "leave")
        check(
            response_matches(departed, "accepted"),
            "fighter disconnect uses room departure lifecycle",
        )
        ended = room("server", "export", match=match)
        check(
            response_matches(ended, "exported") and ended.get("replay"),
            "complete interrupted authority history exports after GGPO combat",
        )
        authentication_offset = len(group.events("client0"))
        group.command("client0", "authenticate", identity="peer")
        group.wait_for(
            lambda: any(
                e.get("type") == "delivery"
                and e.get("membership")
                and e["membership"] != departed["membership"]
                for e in group.events("client0")[authentication_offset:]
            ),
            "departed peer reauthenticates with new generation",
        )
        remote = room("client0", "export", match=match)
        check(
            response_matches(remote, "exported")
            and remote.get("replay_valid")
            and ended.get("replay_valid")
            and remote.get("replay_semantics") == ended.get("replay_semantics"),
            "remote complete export matches authority gameplay and configuration",
        )
        check(
            bool(remote.get("replay")) and remote["nonce"] != ended["nonce"],
            "actual remote transport delivered the complete correlated export",
        )
        selected = room("client0", "select-match", match=match)
        check(
            response_matches(selected, "accepted"),
            "remote viewer selects retained battle before main-world travel",
        )
        selected = room("client0", "seek", match=match, number=32)
        check(
            selected["frame"] == 32,
            "remote viewer establishes independent cursor before travel",
        )
        map_loads = {role: group.command(role, "snapshot")["completed_map_loads"]
                     for role in ("server", "client0")}
        travel_offsets = {role: len(group.events(role)) for role in ("server", "client0")}
        membership = selected["membership"]
        check(
            group.command("server", "travel")["ok"],
            "authority initiates actual server travel",
        )
        for role in ("server", "client0"):
            group.wait_for(
                lambda role=role: any(
                    e.get("completed_map_loads", 0) > map_loads[role]
                    and e.get("current_membership")
                    and e.get("owner_channel_ready")
                    and e.get("world_map") == "/Engine/Maps/Entry"
                    for e in group.events(role)[travel_offsets[role]:]
                ),
                role + " automatically reauthenticates after real world travel",
                timeout=60,
            )
        restored = room("client0", "seek", match=match, number=33)
        check(
            restored["membership"] == membership and restored["frame"] == 33,
            "travel restores durable membership and retained matching playback",
        )
        (args.directory / "result.json").write_text(
            json.dumps(
                {
                    "passed": True,
                    "assertions": assertions,
                    "rollback_restores": rollbacks,
                    "elapsed": time.monotonic() - began,
                    "frames": 80,
                    "scope": "real two-peer GGPO/authority integration; six-participant scenario remains separate",
                },
                indent=2,
            )
        )
    finally:
        group.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

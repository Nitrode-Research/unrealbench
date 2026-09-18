from room_battle_preparation import prepare_battle
from room_display_checks import capture_room_display, evaluate_room_displays
from room_response_contract import annotate_response, prior_delivery, response_matches, await_successful_start, seek_then_pause
"""Six real processes. Assert owner RPC authority while faults cross a real UDP boundary."""

import argparse
import json
import time
import signal
from bisect import bisect_right
from pathlib import Path
import uuid
from room_network_workers import WorkerGroup, force_kill_authority, observed_corrections
from room_navigation_fuzz import run_fuzz, make_plan
from room_disclosure_checks import active_delivery_disclosure_errors
from room_input_challenges import make_initial_inputs, direct_copy_evidence, CHALLENGE_FRAMES


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--battle-seed", type=int, default=9009)
    parser.add_argument("--delay", type=int, choices=[0, 1, 120, 600], default=120)
    parser.add_argument("--fuzz-plan", type=Path)
    parser.add_argument("--input-plan", type=Path)
    parser.add_argument("--held-out-only", action="store_true")
    parser.add_argument("--cases")
    parser.add_argument("--no-recovery", action="store_true")
    parser.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    args = parser.parse_args()
    delays = (
        [int(value) for value in args.cases.split(",")]
        if args.cases
        else [args.delay]
    )
    delay = delays[0]
    replay_plan = json.loads(args.fuzz_plan.read_text()) if args.fuzz_plan else None
    initial_inputs = (json.loads(args.input_plan.read_text()) if args.input_plan else
                      replay_plan.get("initial_inputs") if replay_plan and "initial_inputs" in replay_plan else
                      make_initial_inputs())
    if len(initial_inputs) != 160 or any(len(pair) != 2 for pair in initial_inputs):
        raise ValueError("Initial input plan must contain exactly 160 fighter pairs")
    group = WorkerGroup(
        args.editor,
        args.project,
        args.directory,
        editor_args=args.editor_args.split(),
        battle_seed=args.battle_seed,
        extra_args=["-NullRHI"],
    )
    (args.directory / "scenario-config.json").write_text(
        json.dumps(dict(delay=args.delay, battle_seed=args.battle_seed), indent=2)
    )
    assertions = 0
    began = time.monotonic()

    def progress(phase):
        event = {
            "phase": phase,
            "elapsed": time.monotonic() - began,
            "assertions": assertions,
        }
        args.directory.mkdir(parents=True, exist_ok=True)
        with (args.directory / "phases.jsonl").open("a") as stream:
            stream.write(json.dumps(event) + "\n")
        print(json.dumps(event), flush=True)

    def check(condition, message):
        nonlocal assertions
        assert condition, message
        assertions += 1

    def room(role, operation, **values):
        if values.pop("_observe_before", False):
            room(role, "observe")
        response_before = prior_delivery(group, role)
        nonce = values.pop("nonce", uuid.uuid4().hex)
        before = sum(
            e.get("type") == "delivery" and e.get("nonce") == nonce
            for e in group.events(role)
        )
        group.command(role, "room", operation=operation, nonce=nonce, **values)
        group.wait_for(
            lambda: sum(
                e.get("type") == "delivery" and e.get("nonce") == nonce
                for e in group.events(role)
            )
            > before,
            f"{role} acknowledged {operation}",
            timeout=30,
        )
        return annotate_response(next(
            e
            for e in reversed(group.events(role))
            if e.get("type") == "delivery" and e.get("nonce") == nonce
        ), response_before, operation, values)

    try:
        group.start(5)
        progress("six processes connected")
        room_slot = "network-" + uuid.uuid4().hex
        group.command("server", "create", slot=room_slot, delay=delay)
        for index in range(5):
            role = f"client{index}"
            group.command(role, "authenticate", identity=role)
            group.wait_for(
                lambda role=role: any(
                    e.get("member_role") == "spectator" for e in group.events(role)
                ),
                role + " authenticated",
                timeout=30,
            )
        check(
            len(
                {
                    next(e["pid"] for e in group.events(role) if "pid" in e)
                    for role in group.processes
                }
            )
            == 6,
            "six distinct Unreal participant processes",
        )
        for index in range(5):
            check(
                response_matches(room(f"client{index}", "queue"), "accepted"),
                "queue acknowledged through owner RPC",
            )
        initiator_before = room("client2", "observe")
        recipient_before = room("client3", "observe")
        refused = room("client2", "offer", number=0, value="client3")
        initiator_after = room("client2", "observe")
        recipient_after = room("client3", "observe")
        check(
            bool(refused["status"])
            and bool(refused.get("status")),
            "spectator offer is refused while a seat is vacant",
        )
        check(
            all(
                (
                    sorted(after[key]) == sorted(before[key])
                    if key == "roster"
                    else after[key] == before[key]
                )
                for before, after in (
                    (initiator_before, initiator_after),
                    (recipient_before, recipient_after),
                )
                for key in (
                    "roster",
                    "membership",
                    "assignment",
                    "offer",
                    "member_role",
                )
            ),
            "spectator offer cannot alter authoritative offer or seat ownership",
        )
        proxy = group.proxies[2]
        proxy.configure(finite_faults=True, finite_directions=("to_server", "to_client"),
                        finite_drop_count=2, context="lobby-controls")
        for index in range(5):
            status = room("client2", "lock", _observe_before=True)
            check(response_matches(status, "unauthorized"), "faulted spectator cannot lock selections")
            check(
                room("server", "lock")["locked"], "healthy organizer controls continue"
            )
            check(not room("server", "unlock")["locked"], "healthy unlock")
        group.wait_for(proxy.finite_faults_complete, "finite lobby packet faults delivered", timeout=10)
        check(proxy.finite_faults_complete(), "lobby drop delay reorder and duplicate witnesses complete")
        proxy.configure()
        check(
            response_matches(room("client2", "withdraw"), "accepted"),
            "faulted viewer recovers",
        )
        old_membership = room("client2", "withdraw")["membership"]
        check(
            response_matches(room("client2", "leave"), "accepted"),
            "viewer departure acknowledged",
        )
        old = group.processes.pop("client2")
        old.terminate()
        old.wait(timeout=10)
        group._launch("client2restart", f"127.0.0.1:{proxy.port}")
        group.wait_for(
            lambda: any(
                e.get("server_connection") for e in group.events("client2restart")
            ),
            "restarted socket connects",
        )
        group.command("client2restart", "authenticate", identity="client2")
        group.wait_for(
            lambda: any(
                e.get("member_role") == "spectator"
                for e in group.events("client2restart")
            ),
            "restarted viewer reauthenticates",
            timeout=30,
        )
        restored = room("client2restart", "queue")
        check(
            response_matches(restored, "accepted"),
            "restarted viewer performs healthy operation",
        )
        check(
            bool(restored["membership"]) and len(restored["roster"]) == len(set(restored["roster"])),
            "restart preserves one acknowledged membership",
        )
        check(
            response_matches(room("client2restart", "leave", membership=old_membership, _observe_before=True), "stale membership"),
            "old departure cannot remove restarted viewer",
        )
        check(
            response_matches(room("client2restart", "withdraw"), "accepted"),
            "healthy command follows stale event",
        )
        trace = [
            json.loads(line)
            for line in (args.directory / "packets-client2.jsonl")
            .read_text()
            .splitlines()
        ]
        check(
            any(e["action"] == "drop" for e in trace),
            "fault actually dropped UDP payloads",
        )
        check(
            any(
                e["action"] == "deliver" and e.get("scheduled_delay", 0) > 0
                for e in trace
            ),
            "fault actually delayed delivery",
        )
        for index in range(2, 5):
            deliveries = [
                e for e in group.events(f"client{index}") if e.get("type") == "delivery"
            ]
            check(bool(deliveries), "decoded payload audit exists")
            check(
                all(
                    e.get("frame") == -1
                    and not e.get("gameplay")
                    and not e.get("replay")
                    for e in deliveries
                ),
                "no gameplay exists before match",
            )
        progress("lobby faults and viewer restart complete")
        # Real native gameplay runs behind the same owner RPCs and faulted UDP channels.
        roles = ["client0", "client1", "client2restart", "client3", "client4"]
        package = ""
        match = ""
        ledger = {}
        semantic_ledger = {}
        fuzz = None
        cases = []

        def offer_seats():
            for seat in range(2):
                role = f"client{seat}"
                offset = len(group.events(role))
                check(
                    response_matches(room("server", "offer", number=seat, value=role), "accepted"),
                    "organizer offer",
                )
                group.wait_for(
                    lambda role=role, offset=offset: any(
                        e.get("offer") for e in group.events(role)[offset:]
                    ),
                    role + " offer delivery",
                    timeout=30,
                )
                offer = next(
                    e["offer"] for e in reversed(group.events(role)) if e.get("offer")
                )
                nonce = uuid.uuid4().hex
                accepted = room(role, "accept", assignment=offer, nonce=nonce)
                check(
                    accepted["member_role"] == "fighter",
                    "offered spectator gains fighter role",
                )
                duplicate = room(role, "accept", assignment=offer, nonce=nonce)
                check(
                    response_matches(duplicate, "duplicate")
                    and duplicate["assignment"] == accepted["assignment"],
                    "duplicate acceptance preserves one owner",
                )

        def run_case(case_delay, first):
            nonlocal room_slot, package, match, ledger, semantic_ledger, fuzz
            offsets = {role: len(group.events(role)) for role in roles}
            if not first:
                room_slot = "network-" + uuid.uuid4().hex
                group.command("server", "create", slot=room_slot, delay=case_delay)
                for role in roles:
                    offset = len(group.events(role))
                    group.command(
                        role,
                        "authenticate",
                        identity="client2" if role == "client2restart" else role,
                    )
                    group.wait_for(
                        lambda role=role, offset=offset: any(
                            e.get("type") == "delivery" and e.get("membership")
                            for e in group.events(role)[offset:]
                        ),
                        role + " reauthenticated into fresh case room",
                        timeout=30,
                    )
                for role in roles:
                    check(
                        response_matches(room(role, "queue"), "accepted"),
                        "queue acknowledged through owner RPC",
                    )
            offer_seats()
            package = "/Game/RoomProcess/" + uuid.uuid4().hex + "/"
            group.command("server", "content", package=package)
            for role in roles:
                group.command(role, "content", package=package)
                group.command(
                    role,
                    "authenticate",
                    identity="client2" if role == "client2restart" else role,
                )
                group.command(role, "battle")
            for seat in range(2):
                role = "client" + str(seat)
                assignment = next(
                    e["assignment"]
                    for e in reversed(group.events(role))
                    if e.get("assignment")
                )
                check(
                    response_matches(room(
                        role,
                        "character",
                        assignment=assignment,
                        value=package + "Character.Character",
                    ), "accepted"),
                    "network character selection",
                )
            check(
                response_matches(room("server", "stage", value=package + "Stage.Stage"), "accepted"),
                "network stage selection",
            )
            prepare_battle(group, roles, fighters=roles[:2])
            prepared_assignments = []
            for seat in range(2):
                role = "client" + str(seat)
                current = room(role, "observe")
                check(
                    bool(current.get("membership"))
                    and current.get("member_role") == "fighter"
                    and bool(current.get("assignment")),
                    "prepared current connection retains owned fighter role",
                )
                assignment = current["assignment"]
                prepared_assignments.append(assignment)
                check(
                    response_matches(room(role, "ready", assignment=assignment), "accepted"),
                    "actual package content accepted",
                )
            check(len(set(prepared_assignments)) == 2, "prepared fighters have distinct current assignments")
            departed_viewers = {
                role: room(role, "leave")["membership"] for role in roles[2:]
            }
            group.command("server", "clock", tick=0)
            check(room("server", "observe").get("active_match") is False,
                  "final readiness leaves match inactive before organizer start")
            if not args.no_recovery:
                # Establish the selection lock between matches, then verify its
                # acknowledged state survives active gameplay and authority death.
                locked = room("server", "lock")
                check(response_matches(locked, "accepted") and locked["locked"],
                      "selection lock acknowledged before match start")
            started = await_successful_start(group, "server", room("server", "start"), room)
            check(
                response_matches(started, "accepted"),
                "native battle started through normal room command",
            )
            progress("combat started")
            for role in roles[:2]:
                group.wait_for(
                    lambda role=role: any(
                        e.get("prediction_frame") == 0
                        for e in group.events(role)[offsets[role] :]
                    ),
                    role + " initialized real GGPO world",
                    timeout=60,
                )
            match = room(roles[0], "observe")["match"]
            check(bool(match), "current fighter publishes the newly active match identity")
            assignments = []
            for role in roles[:2]:
                current = room(role, "observe")
                check(
                    bool(current.get("membership"))
                    and current.get("member_role") == "fighter"
                    and bool(current.get("assignment"))
                    and current.get("match") == match,
                    "active current connection retains owned fighter role and match",
                )
                assignments.append(current["assignment"])
            check(len(set(assignments)) == 2, "active fighters have distinct current assignments")
            ledger = {0: (-150000, 10000)}
            semantic_ledger = {}
            join_frames = {20: roles[2], 80: roles[3], 140: roles[4]}
            input_challenge_rows = []
            for frame in range(1, 161):
                group.command("server", "clock", tick=frame)
                if frame in (60, 100):
                    # Each finite episode must finish before the healthy interval.
                    episode_viewer = group.proxies[2 if frame == 60 else 3]
                    for faulted in (episode_viewer, group.proxies[1]):
                        group.wait_for(faulted.finite_faults_complete,
                                       "bidirectional finite combat faults delivered", timeout=10)
                        check(faulted.finite_faults_complete(),
                              "combat burst loss delay reorder duplicate witnesses complete")
                        faulted.configure()
                    recovered_viewer = room(roles[2 if frame == 60 else 3], "observe")
                    check(bool(recovered_viewer.get("membership")) and recovered_viewer["match"] == match,
                          "viewer acknowledges healthy retained membership after each fault episode")
                if frame in (35, 80):
                    episode_viewer = group.proxies[2 if frame == 35 else 3]
                    for label, faulted in (("viewer", episode_viewer), ("fighter", group.proxies[1])):
                        faulted.configure(finite_faults=True,
                                          finite_directions=("to_server", "to_client"),
                                          finite_drop_count=2,
                                          context=f"combat-{case_delay}-frame{frame}-{label}")
                if frame in join_frames:
                    joining = join_frames[frame]
                    group.command(
                        joining,
                        "authenticate",
                        identity="client2" if joining == "client2restart" else joining,
                    )
                    group.wait_for(
                        lambda: any(
                            e.get("type") == "delivery"
                            and e.get("membership")
                            and e["membership"] != departed_viewers[joining]
                            for e in group.events(joining)[-10:]
                        ),
                        joining + " late-join membership",
                        timeout=30,
                    )
                    joined = room(joining, "pause")
                    check(
                        joined["match"] == match
                        and joined["frame"] == min(frame - 1, max(-1, frame - case_delay)),
                        "late join reaches exactly the independently released prefix",
                    )
                    check(
                        joined["member_role"] == "spectator",
                        "late join cannot acquire a combat role",
                    )
                    if frame == 140:
                        check(
                            any(health < 10000 for x, health in ledger.values()),
                            "distinctive damage occurred before third spectator joined",
                        )
                        check(
                            len({x for x, health in ledger.values()}) > 1,
                            "movement occurred before late spectator join",
                        )
                    progress(
                        "late spectator joined after "
                        + str(frame - 1)
                        + " confirmed frames"
                    )
                pending_inputs = []
                for seat in range(2):
                    bits = initial_inputs[frame - 1][seat]
                    role = "client" + str(seat)
                    values = dict(match=match, assignment=assignments[seat],
                                  number=frame, value=str(bits))
                    nonce = uuid.uuid4().hex
                    before = prior_delivery(group, role)
                    identifier = group.send(role, "room", operation="input", nonce=nonce, **values)
                    pending_inputs.append((role, identifier, nonce, before, values))
                # Opposite seats are independent until the authority commits the
                # pair. Preserve each correlated acknowledgement before stepping.
                for role, identifier, nonce, before, values in pending_inputs:
                    group.reply(role, identifier)
                    group.wait_for(lambda role=role, nonce=nonce: group.delivery(role, nonce) is not None,
                                   role + " acknowledged input", timeout=30)
                    accepted = annotate_response(group.delivery(role, nonce), before, "input", values)
                    check(response_matches(accepted, "accepted"), "fighter owner input acknowledged")
                authority = group.command("server", "step")
                check(
                    authority["battle_frame"] == frame,
                    "authority commits exactly one real input pair",
                )
                ledger[frame] = (authority["battle_x"], authority["battle_health"])
                semantic_ledger[frame] = authority["battle_state"]
                if case_delay >= 120 and frame in CHALLENGE_FRAMES:
                    hidden = room(roles[2], "observe")
                    check(hidden["frame"] < 0 and not hidden.get("gameplay"),
                          "random input challenge remains entirely before frame-zero release")
                    input_challenge_rows.append(dict(frame=frame, submitted=initial_inputs[frame - 1],
                                                     observed=[hidden["input1"], hidden["input2"]]))
                for role in roles[:2]:
                    group.wait_for(
                        lambda role=role: any(
                            e.get("prediction_frame") == frame
                            and e.get("prediction_confirmed", -1) >= frame
                            for e in group.events(role)[offsets[role] :]
                        ),
                        role + " GGPO confirms native frame",
                        timeout=30,
                    )
                    present = next(
                        e
                        for e in reversed(group.events(role)[offsets[role] :])
                        if e.get("prediction_frame") == frame
                        and e.get("prediction_confirmed", -1) >= frame
                    )
                    check(
                        (present["prediction_x"], present["prediction_health"])
                        == ledger[frame],
                        "corrected GGPO gameplay equals independent authoritative simulation",
                    )
                    check(
                        present["prediction_state"] == semantic_ledger[frame],
                        "corrected fighters match the full independent semantic frame",
                    )
                if frame % 40 == 0:
                    progress("combat frame " + str(frame))
            check(
                any(health < 10000 for x, health in ledger.values()),
                "native network battle caused actual health damage",
            )
            check(
                len({x for x, health in ledger.values()}) > 1,
                "native network battle caused actual movement",
            )
            if input_challenge_rows:
                evidence = direct_copy_evidence(input_challenge_rows)
                (args.directory / f"input-disclosure-{case_delay}.json").write_text(
                    json.dumps(dict(evidence=evidence, observations=input_challenge_rows), indent=2))
                check(not evidence["suspected_direct_copy"],
                      "buffered direct fields track verifier-private unreleased fighter inputs beyond the declared coincidence bound")
            group.command("server", "clock", tick=160 + case_delay + 20)
            for role in roles[2:]:
                offset = len(group.events(role))
                selected, paused = seek_then_pause(room, check, role, match, 32, 160)
                check(selected["frame"] == 32, "spectator seeks older confirmed frame")
                group.wait_for(
                    lambda role=role, offset=offset: any(
                        e.get("type") == "presentation" and e.get("frame") == 32
                        for e in group.events(role)[offset:]
                    ),
                    "viewer reconstructs frame32",
                    timeout=30,
                )
                present = next(
                    e
                    for e in reversed(group.events(role)[offset:])
                    if e.get("type") == "presentation" and e.get("frame") == 32
                )
                check(
                    (present["x"], present["health"]) == ledger[32],
                    "separate process native presentation equals fighter ledger",
                )
            if args.fuzz_plan:
                plan = json.loads(args.fuzz_plan.read_text())
            elif args.held_out_only:
                plan = make_plan(case_delay, list(range(25001, 25003)))
            else:
                plan = make_plan(case_delay)
            plan["initial_inputs"] = initial_inputs
            fuzz = run_fuzz(
                group,
                room,
                check,
                roles,
                match,
                assignments,
                ledger,
                progress,
                package,
                case_delay,
                plan,
                semantic_ledger,
            )
            suffix = f"-{case_delay}" if args.cases else ""
            (args.directory / f"confirmed-ledger{suffix}.json").write_text(
                json.dumps(ledger, indent=2)
            )
            (args.directory / f"semantic-ledger{suffix}.json").write_text(
                json.dumps(semantic_ledger, indent=2)
            )
            server_ticks = sorted(
                (e["seconds"], e["authority_tick"])
                for e in group.events("server")
                if e.get("type") == "reply" and "authority_tick" in e
            )
            tick_stamps = [stamp for stamp, value in server_ticks]
            expected_p1 = [pair[0] for pair in initial_inputs]
            expected_p2 = [pair[1] for pair in initial_inputs]
            expected_pairs = {0: (0, 0)}
            expected_pairs.update({frame: (expected_p1[frame - 1], expected_p2[frame - 1])
                                   for frame in range(1, 161)})
            for index, seed in enumerate(plan["seeds"], start=161):
                submitted = next(row for row in plan["inputs"] if row["seed"] == seed)
                expected_pairs[index] = (submitted["p1"], submitted["p2"])
            for role in roles[2:]:
                for delivery in group.events(role)[offsets[role] :]:
                    if delivery.get("type") != "delivery":
                        continue
                    stamp_index = bisect_right(tick_stamps, delivery["seconds"]) - 1
                    # Lobby deliveries may predate the first explicit room-clock
                    # sample. They have no released gameplay to timestamp.
                    tick = server_ticks[stamp_index][1] if stamp_index >= 0 else None
                    errors = active_delivery_disclosure_errors(
                        delivery, tick, case_delay, fuzz["confirmed_ticks"], expected_pairs)
                    check(not errors, "raw spectator disclosure: " + "; ".join(errors))
                    if delivery.get("frame", -1) < 0 or not delivery.get("gameplay"):
                        continue
                    check(
                        delivery["decoded_valid"],
                        "released gameplay is semantically decodable",
                    )
                    check(
                        delivery["decoded_match"] == delivery["match"],
                        "released inputs belong to the selected match",
                    )
                    check(
                        len(delivery["decoded_inputs1"]) == delivery["frame"],
                        "decoded journal contains no future player1 inputs",
                    )
                    check(
                        len(delivery["decoded_inputs2"]) == delivery["frame"],
                        "decoded journal contains no future player2 inputs",
                    )
                    check(
                        delivery["decoded_inputs1"][:160]
                        == expected_p1[: min(160, delivery["frame"])],
                        "released player1 history matches independently submitted input",
                    )
                    check(
                        delivery["decoded_inputs2"][:160]
                        == expected_p2[: min(160, delivery["frame"])],
                        "released player2 history matches independently submitted input",
                    )
            corrections = [
                correction
                for role in roles[:2]
                for correction in observed_corrections(
                    group.events(role)[offsets[role] :]
                )
            ]
            # Speculation is optional; lockstep implementations may report no corrections.
            for correction in corrections:
                check(
                    correction["after"] == semantic_ledger[correction["frame"]],
                    "observed correction converges to independent confirmed gameplay",
                )
            (args.directory / "observed-corrections.json").write_text(
                json.dumps(corrections, indent=2)
            )
            progress("DVR seeks, GGPO corrections and decoded payload audit complete")
            cases.append(
                dict(
                    delay=case_delay,
                    match=match,
                    frames=max(ledger),
                    confirmed=f"confirmed-ledger{suffix}.json",
                    semantic=f"semantic-ledger{suffix}.json",
                )
            )
            if args.no_recovery:
                # Clock advance releases history but cannot finish an active match.
                # End it through an ordinary fighter departure at a known accepted
                # time, preserving the independent confirmed gameplay prefix.
                interruption_tick = fuzz["final_room_tick"]
                group.command("server", "clock", tick=interruption_tick)
                check(response_matches(room(roles[0], "leave"), "accepted"),
                      "ordinary fighter departure accepted after fuzz gameplay")
                terminal = room(roles[1], "observe")
                check(terminal.get("match") == match and terminal.get("frame") == max(ledger)
                      and bool(terminal.get("outcome", "").strip())
                      and room("server", "observe").get("active_match") is False,
                      "fighter departure retains the final confirmed frame with an outcome")
                group.command("server", "clock", tick=interruption_tick + case_delay)
                exported = room(roles[2], "export", match=match)
                check(
                    response_matches(exported, "exported") and bool(exported["replay"]),
                    "complete durable history exports after fighter interruption release",
                )
                semantics = exported.get("replay_semantics", {})
                frames = semantics.get("frames", [])
                check(semantics.get("match") == match
                      and semantics.get("outcome_tick") == interruption_tick
                      and len(frames) == max(ledger) + 1
                      and all(frame["confirmation"] == fuzz["confirmed_ticks"][index]
                              for index, frame in enumerate(frames)),
                      "fighter interruption uses accepted departure time without changing confirmations")
                group.wait_for(
                    lambda: any(e.get("type") == "public-export-recorded" and e.get("match") == match
                                for e in group.events(roles[2])),
                    "client export durably saved",
                )
                progress("export durably saved")

        for index, case_delay in enumerate(delays):
            run_case(case_delay, index == 0)

        if not args.no_recovery:
            # Crash during active gameplay; recovery must retain the prefix and interrupt the match.
            for role in roles[2:]:
                check(
                    response_matches(room(role, "queue"), "accepted"),
                    "queue acknowledged before authority crash",
                )
            reserved_assignments = {role: room(role, "observe")["assignment"]
                                    for role in roles[:2]}
            check(room("server", "observe")["locked"],
                  "acknowledged selection lock remains set before authority crash")
            authority_pid = next(
                e["pid"] for e in group.events("server") if e.get("type") == "heartbeat"
            )
            termination = force_kill_authority(authority_pid)
            (args.directory / "authority-crash.json").write_text(
                json.dumps(
                    dict(
                        pid=authority_pid,
                        termination=termination,
                        confirmed_frame=max(ledger),
                    )
                )
            )
            group.processes["server"].wait(timeout=10)
            group.close()
            group = WorkerGroup(
                args.editor,
                args.project,
                args.directory / "authority-restart",
                editor_args=args.editor_args.split(),
                battle_seed=args.battle_seed,
                extra_args=["-NullRHI"],
                # Only the recovered viewer supplying panel pixels needs the configured RHI.
                rendering_roles={"client2"},
            )
            group.start(5)
            recovery_tick = fuzz["final_room_tick"] + 1000
            group.command("server", "clock", tick=recovery_tick)
            group.command("server", "recover", slot=room_slot)
            for index in range(5):
                role = f"client{index}"
                group.command(role, "authenticate", identity=role)
                expected = "fighter" if index < 2 else "spectator"
                group.wait_for(
                    lambda role=role, expected=expected: any(
                        e.get("member_role") == expected for e in group.events(role)
                    ),
                    role + " reserved role restored",
                    timeout=30,
                )
                check(True, "authenticated role restored after authority crash")
                restored_role = room(role, "observe")
                if role in reserved_assignments:
                    check(restored_role["assignment"] == reserved_assignments[role],
                          "exact acknowledged fighter assignment survives real authority death")
                check(restored_role["locked"],
                      "acknowledged selection lock survives real authority death")
            restored = room("server", "offer", number=0, value="client3")
            check(
                response_matches(restored, "occupied"),
                "acknowledged fighter seats survived authority process loss",
            )
            check(
                response_matches(restored, "occupied"),
                "reserved fighter seat survived process loss",
            )
            check(
                not room("server", "unlock")["locked"],
                "recovered authority controls are healthy",
            )
            progress("authority restart and role restoration complete")
            for role in ["client2", "client3", "client4"]:
                group.command(role, "content", package=package)
                group.command(role, "authenticate", identity=role)
            if delay > 0:
                group.command("server", "clock", tick=recovery_tick + delay - 1)
                pending = room("client2", "export", match=match)
                check(
                    response_matches(pending, "pending")
                    and not pending.get("replay")
                    and not pending.get("outcome"),
                    "fresh recovery outcome and complete bytes remain withheld at r+D-1 despite long downtime",
                )
                permitted = room("client2", "seek", match=match, number=max(ledger))
                check(
                    response_matches(permitted, "accepted")
                    and permitted["frame"] == max(ledger),
                    "same recovered connection receives the old confirmed frame while fresh outcome is pending",
                )
                check(
                    not room("server", "unlock")["locked"],
                    "recovered organizer controls remain usable before outcome release",
                )
            group.command("server", "clock", tick=recovery_tick + delay)
            exported = room("client2", "export", match=match)
            check(
                response_matches(exported, "exported") and bool(exported["replay"]),
                "complete durable history exports after recovery outcome release",
            )
            recovered_semantics = exported.get("replay_semantics", {})
            check(recovered_semantics.get("match") == match
                  and recovered_semantics.get("outcome_tick") == recovery_tick,
                  "interruption outcome records actual durable recovery time")
            recovered_frames = recovered_semantics.get("frames", [])
            check(len(recovered_frames) == max(ledger) + 1
                  and all(frame["confirmation"] == fuzz["confirmed_ticks"][index]
                          for index, frame in enumerate(recovered_frames)),
                  "real recovery preserves every original confirmation including frame zero")
            group.wait_for(
                lambda: any(e.get("type") == "public-export-recorded" and e.get("match") == match
                                for e in group.events("client2")),
                "client export durably saved",
            )
            capture_room_display(group, "client2", recovery_pending=False, member_role="spectator",
                                 room_identity=exported["room"], match_identity=match)
            evaluate_room_displays(group, check)
            progress("export durably saved")
            group.close()
            group = WorkerGroup(
                args.editor,
                args.project,
                args.directory / "offline-replay",
                editor_args=args.editor_args.split(),
                battle_seed=7,
                extra_args=["-NullRHI"],
            )
            group._launch(
                "offline",
                "/Engine/Maps/Entry?game=/Script/NightSkyEngine.RoomWorkerGameMode",
            )
            group.wait_for(
                lambda: any(
                    e.get("type") == "heartbeat" for e in group.events("offline")
                ),
                "fresh offline process initialized",
            )
            hello = next(
                e for e in group.events("offline") if e.get("type") == "heartbeat"
            )
            check(
                hello["configuration_seed"] == 7 and args.battle_seed != 7,
                "fresh offline process starts with unrelated ambient RNG configuration",
            )
            group.command("offline", "replay", match=match)
            group.wait_for(
                lambda: any(
                    e.get("type") == "offline-ready" for e in group.events("offline")
                ),
                "ordinary saved replay loaded",
            )
            progress("fresh offline replay loaded")
            for frame in range(1, max(ledger) + 1):
                group.command("offline", "replay-step")
                played = next(
                    e
                    for e in reversed(group.events("offline"))
                    if e.get("type") == "offline-frame"
                )
                check(
                    played["frame"] == frame,
                    "existing sequential replay advances one native frame",
                )
                check(
                    (played["x"], played["health"]) == ledger[frame],
                    "fresh offline native battle equals original fighter ledger",
                )
                check(
                    played["battle_state"] == semantic_ledger[frame],
                    "fresh offline full fighter, input, meter, timer, animation and projectile state equals authority",
                )
                check(
                    played["seed"] == args.battle_seed,
                    "initial random seed survives fresh process export loading",
                )
            progress("full offline ledger verified")
            # After independent artifact playback, reuse this process for a second recovery.
            # Its predecessor has exited, so storage never has two live authorities.
            group.command("offline", "clock", tick=recovery_tick + delay + 1000)
            group.command("offline", "recover", slot=room_slot)
            group.command("offline", "content", package=package)
            group.command("offline", "authenticate", identity="client2")
            repeated = room("offline", "export", match=match)
            check(response_matches(repeated, "exported")
                  and repeated.get("replay_semantics") == recovered_semantics,
                  "later fresh recovery does not reset released outcome or original confirmations")
        scope = "six-process owner RPCs, faulted transport, native combat and DVR"
        if not args.no_recovery:
            scope += ", authority recovery"
        (args.directory / "result.json").write_text(
            json.dumps(
                {
                    "passed": True,
                    "assertions": assertions,
                    "scope": scope,
                    "cases": cases,
                    "fuzz": fuzz,
                },
                indent=2,
            )
        )
    finally:
        group.close()


if __name__ == "__main__":
    raise SystemExit(main())

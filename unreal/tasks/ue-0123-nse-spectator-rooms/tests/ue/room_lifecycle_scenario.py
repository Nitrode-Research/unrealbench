from room_battle_preparation import prepare_battle
from room_display_checks import capture_room_display, evaluate_room_displays
from room_response_contract import annotate_response, prior_delivery, response_matches, await_successful_start, seek_then_pause
"""Six actual participants: completed successors, changed content, seats and travel."""

import argparse, json, time, uuid
from pathlib import Path
from room_network_workers import WorkerGroup
from room_transfer_recovery import interrupt_history
from room_corrupt_transfer import corrupt_history
from room_combined_lifecycle import CombinedLifecycle


def main():
    p = argparse.ArgumentParser()
    p.add_argument("project", type=Path)
    p.add_argument("directory", type=Path)
    p.add_argument("--editor", type=Path, required=True)
    p.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    p.add_argument("--delay", type=int, default=120, choices=[0, 1, 120, 600])
    p.add_argument("--combined", action="store_true")
    p.add_argument("--pilot", action="store_true")
    p.add_argument("--combined-plan", type=Path)
    p.add_argument("--combined-per-phase", type=int, default=50)
    p.add_argument("--driver-deadline", type=float, default=0)
    a = p.parse_args()
    group = WorkerGroup(
        a.editor,
        a.project,
        a.directory,
        editor_args=a.editor_args.split(),
        extra_args=["-NullRHI"],
        # Only this viewer supplies actual panel pixels at rematch/integrity checkpoints.
        rendering_roles={"client4"},
    )
    assertions = 0
    began = time.monotonic()
    clock = 0
    matches = []
    ledgers = []
    semantic_ledgers = []
    confirmation_ledgers = []
    phase = "startup"
    roles = [f"client{i}" for i in range(5)]
    fighters = roles[:2]
    frozen = "client2"
    frozen_cursor = None

    def check(value, label):
        nonlocal assertions
        assert value, label
        assertions += 1

    combined = None

    def progress(label):
        nonlocal phase
        if a.driver_deadline and time.monotonic() - began > a.driver_deadline:
            raise TimeoutError("driver deadline exceeded in phase " + label)
        phase = label
        a.directory.mkdir(parents=True, exist_ok=True)
        event = dict(
            phase=label, elapsed=time.monotonic() - began, assertions=assertions
        )
        with (a.directory / "phases.jsonl").open("a") as f:
            f.write(json.dumps(event) + "\n")
        print(json.dumps(event), flush=True)

    def room(role, operation, **args):
        if args.pop("_observe_before", False):
            room(role, "observe")
        response_before = prior_delivery(group, role)
        nonce = args.pop("nonce", uuid.uuid4().hex)
        previous = group.delivery(role, nonce)
        group.command(role, "room", operation=operation, nonce=nonce, **args)
        group.wait_for(
            lambda: group.delivery(role, nonce) is not None
            and group.delivery(role, nonce) is not previous,
            role + " " + operation,
            timeout=30,
        )
        reply = group.delivery(role, nonce)
        if combined:
            combined.record(role, operation, dict(args, nonce=nonce), reply)
        return annotate_response(reply, response_before, operation, args)

    def auth(role, retry=False):
        offset = len(group.events(role))
        group.command(
            role,
            "retry-content" if retry else "authenticate",
            identity="host" if role == "server" else role,
        )
        group.wait_for(
            lambda: any(
                e.get("type") == "delivery" and e.get("membership")
                for e in group.events(role)[offset:]
            ),
            role + " authenticated",
            timeout=30,
        )

    def tick(value):
        nonlocal clock
        clock = value
        group.command("server", "clock", tick=clock)

    def offer(role, seat):
        room(role, "queue")
        check(
            response_matches(room("server", "offer", number=seat, value=role), "accepted"),
            "organizer chooses queued recipient",
        )
        offer_id = room(role, "queue")["offer"]
        nonce = uuid.uuid4().hex
        accepted = room(role, "accept", assignment=offer_id, nonce=nonce)
        check(response_matches(accepted, "accepted"), "new seat accepted")
        check(
            room(role, "accept", assignment=offer_id, nonce=nonce)["assignment"]
            == accepted["assignment"],
            "duplicate acceptance has one owner",
        )
        return accepted["assignment"]

    def show(role, match, frame, expected):
        nonlocal frozen_cursor
        offset = len(group.events(role))
        view, paused = seek_then_pause(room, check, role, match, frame,
                                      max(ledgers[matches.index(match)]))
        if role == frozen:
            frozen_cursor = paused["frame"]
            if combined:
                combined.frozen_cursor = frozen_cursor
        check(
            response_matches(view, "accepted") and view["match"] == match,
            "retained viewer seeks requested match/frame",
        )
        expected_state = semantic_ledgers[matches.index(match)][frame]

        def presentation_settled(event):
            return (
                event.get("type") == "presentation"
                and event.get("current_match") == match
                and event.get("frame") == frame
                and event.get("prediction_state") == expected_state
            )

        def view_settled(event):
            return (
                event.get("type") == "delivery"
                and event.get("match") == match
                and event.get("frame") == frame
                and event.get("nonce") == view["nonce"]
            )

        group.wait_for(
            lambda: any(
                presentation_settled(event)
                for event in group.events(role)[offset:]
            ) and any(
                view_settled(event)
                for event in group.events(role)[offset:]
            ),
            role + " native presentation",
            timeout=30,
        )
        state = next(
            event
            for event in reversed(group.events(role)[offset:])
            if presentation_settled(event)
        )
        check(
            (state["x"], state["health"]) == tuple(expected),
            "retained native presentation equals independent authority ledger",
        )
        check(
            state["prediction_state"] == semantic_ledgers[matches.index(match)][frame],
            "retained full native presentation equals independent authority ledger",
        )
        return paused

    try:
        group.start(5)
        if a.combined:
            combined = CombinedLifecycle(
                group,
                check,
                a.pilot,
                a.combined_plan,
                a.delay,
                per_phase=a.combined_per_phase,
            )
        check(
            len(
                {
                    next(e["pid"] for e in group.events(r) if "pid" in e)
                    for r in group.processes
                }
            )
            == 6,
            "six distinct participant processes",
        )
        package = "/Game/RoomLifecycle/" + uuid.uuid4().hex + "/"
        group.command(
            "server",
            "content",
            package=package + "M0/",
            training=False,
            round_seconds=1,
        )
        group.command(
            "server", "create", slot="lifecycle-" + uuid.uuid4().hex, delay=a.delay
        )
        tick(0)
        auth("server")
        for role in roles:
            auth(role)
        assignments = [offer(role, seat) for seat, role in enumerate(fighters)]
        stable_room = room("server", "unlock")["room"]
        old_assignment = ""
        old_fighter = ""
        for generation in range(3):
            progress("selection generation " + str(generation))
            if combined:
                combined.checkpoint(
                    generation,
                    0,
                    room,
                    auth,
                    roles,
                    fighters,
                    matches,
                    ledgers,
                    clock,
                    package,
                    frozen,
                    semantic_ledgers,
                    confirmation_ledgers,
                    a.delay,
                )
            if generation == 1:
                old_fighter = fighters[1]
                old_assignment = assignments[1]
                check(
                    response_matches(room(old_fighter, "leave"), "accepted"),
                    "old fighter vacates only at completed boundary",
                )
                auth(old_fighter)
                # Decline and non-FIFO acceptance through real queued participants.
                room("client4", "queue")
                room("client3", "queue")
                check(
                    response_matches(room("server", "offer", number=1, value="client4"), "accepted"),
                    "first replacement offered",
                )
                declined = room("client4", "queue")["offer"]
                check(
                    response_matches(room("client4", "decline", assignment=declined), "accepted"),
                    "candidate declines seat",
                )
                fighters[1] = "client3"
                assignments[1] = offer(fighters[1], 1)
                check(
                    assignments[1] != old_assignment,
                    "replacement has distinct assignment",
                )
            current_package = package + "M" + str(generation) + "/"
            for role in ["server", *roles]:
                group.command(
                    role,
                    "content",
                    package=current_package,
                    training=False,
                    round_seconds=1,
                    heavy=generation == 1,
                    alternate_map=generation == 1,
                )
            auth("server")
            for seat, role in enumerate(fighters):
                check(
                    response_matches(room(
                        role,
                        "character",
                        assignment=assignments[seat],
                        value=current_package + "Character.Character",
                    ), "accepted"),
                    "current fighter changes own character",
                )
            check(
                response_matches(room("server", "stage", value=current_package + "Stage.Stage"), "accepted"),
                "organizer changes actual stage",
            )
            check(room("server", "lock")["locked"], "selections lock authoritatively")
            check(
                response_matches(room(
                    fighters[0],
                    "character",
                    assignment=assignments[0],
                    value=package + "M0/Character.Character",
                 _observe_before=True), "locked"),
                "locked selection preserves accepted content",
            )
            check(
                not room("server", "unlock")["locked"],
                "organizer unlock remains usable",
            )
            for role in fighters:
                room(role, "pause")
                auth(role, retry=True)
            if generation == 1:
                # Actual new required file unavailable/mismatched while old retained content stays healthy.
                filename = (
                    a.project.parent
                    / "Content"
                    / Path(current_package.removeprefix("/Game/"))
                    / "Character.uasset"
                )
                for role in ["server", *roles]:
                    group.command(
                        role,
                        "release-content-file",
                        package=current_package + "Character",
                    )
                original = filename.read_bytes()
                try:
                    for missing in (
                        combined.fault_order(generation) if combined else (True, False)
                    ):
                        if missing:
                            filename.unlink()
                        else:
                            filename.write_bytes(
                                original + b"room gameplay revision mismatch"
                            )
                        auth(fighters[1], retry=True)
                        check(
                            response_matches(room(
                                fighters[1],
                                "ready",
                                assignment=assignments[1],
                                match=matches[-1],
                             _observe_before=True), "content unavailable"),
                            "new seat cannot ready missing/mismatched newly selected content",
                        )
                        check(
                            response_matches(room("server", "start", _observe_before=True), "fighters not ready"),
                            "content diagnostic blocks new match",
                        )
                        show(frozen, matches[0], 20, ledgers[0][20])
                        check(
                            room("server", "lock")["locked"],
                            "healthy organizer controls work during content failure",
                        )
                        room("server", "unlock")
                        filename.write_bytes(original)
                finally:
                    filename.write_bytes(original)
                auth(fighters[1], retry=True)
            if combined:
                combined.checkpoint(
                    generation,
                    1,
                    room,
                    auth,
                    roles,
                    fighters,
                    matches,
                    ledgers,
                    clock,
                    current_package,
                    frozen,
                    semantic_ledgers,
                    confirmation_ledgers,
                    a.delay,
                )
            # Include ordinary preparation travel in the original start interval.
            start_event_offset = len(group.events("server"))
            fighter_start_offset = len(group.events(fighters[0]))
            prepare_battle(group, roles, fighters=fighters)
            for seat, role in (
                combined.ready_order(generation, fighters)
                if combined
                else enumerate(fighters)
            ):
                auth(role, retry=True)
                current = room(role, "observe")
                check(
                    bool(current.get("membership"))
                    and current.get("member_role") == "fighter"
                    and bool(current.get("assignment")),
                    "prepared current connection retains owned fighter role",
                )
                assignments[seat] = current["assignment"]
                check(
                    response_matches(room(
                        role,
                        "ready",
                        assignment=assignments[seat],
                        match=matches[-1] if matches else "",
                    ), "accepted"),
                    "current content and current owner ready",
                )
            check(len(set(assignments)) == 2, "prepared fighters have distinct current assignments")
            check(room("server", "observe").get("active_match") is False,
                  "final readiness leaves match inactive before organizer start")
            started = await_successful_start(group, "server", room("server", "start"), room)
            check(
                response_matches(started, "accepted"), "normal ready/start creates successor"
            )
            group.wait_for(
                lambda: any(
                    event.get("owner_channel_ready")
                    and event.get("current_match")
                    and event.get("current_match") not in matches
                    and event.get("prediction_state", {}).get("frame") == 0
                    for event in group.events(fighters[0])[fighter_start_offset:]
                ),
                "current fighter channel and successor presentation ready",
                timeout=120,
            )
            successor = room(fighters[0], "observe")
            match = successor["match"]
            check(
                bool(match)
                and match not in matches
                and started["room"] == stable_room
                and successor["room"] == stable_room,
                "stable room with distinct match identity",
            )
            matches.append(match)
            expected_map = (
                current_package + "Arena"
                if generation == 1
                else "/Engine/Maps/Entry"
            )
            expected_health = 12000 if generation == 1 else 10000

            def authority_ready(event):
                return (
                    event.get("battle_frame") == 0
                    and event.get("active_match")
                    and event.get("battle_max_health") == expected_health
                    and event.get("world_map") == expected_map
                )

            group.wait_for(
                lambda: any(
                    authority_ready(event)
                    for event in group.events("server")[start_event_offset:]
                ),
                "selected authority stage and character ready at frame zero",
                timeout=120,
            )
            state = next(
                event
                for event in reversed(group.events("server")[start_event_offset:])
                if authority_ready(event)
            )
            check(
                state["battle_max_health"] == (12000 if generation == 1 else 10000),
                "selected distinct character actually spawned",
            )
            check(
                state["world_map"]
                == (
                    current_package + "Arena"
                    if generation == 1
                    else "/Engine/Maps/Entry"
                ),
                "selected map actually loaded",
            )
            for role in fighters:
                group.wait_for(
                    lambda role=role: any(
                        e.get("current_match") == match
                        and e.get("prediction_state", {}).get("frame") == 0
                        for e in group.events(role)[-20:]
                    ),
                    role + " new real match",
                    timeout=120,
                )
            for role in roles:
                if role not in fighters:
                    auth(role, retry=True)
            if generation:
                unchanged = room(frozen, "pause")
                check(
                    unchanged["match"] == matches[0]
                    and unchanged["frame"] == (combined.frozen_cursor if combined else frozen_cursor),
                    "travel preserves independent old selected timeline",
                )
            ledger = {0: (-150000, 12000 if generation == 1 else 10000)}
            objects = {0: []}
            ledgers.append(ledger)
            semantics = {0: state["battle_state"]}
            semantic_ledgers.append(semantics)
            confirmed = {0: clock}
            confirmation_ledgers.append(confirmed)
            replacement_damage = False
            for frame in range(1, 601):
                if a.driver_deadline and time.monotonic() - began > a.driver_deadline:
                    raise TimeoutError("driver deadline exceeded in phase " + phase)
                tick(clock + 1)
                if generation == 1 and frame == 1:
                    check(
                        response_matches(room(
                            old_fighter,
                            "input",
                            match=match,
                            assignment=old_assignment,
                            number=frame,
                            value="32",
                         _observe_before=True), "input rejected"),
                        "old fighter cannot inject after seat transfer and travel",
                    )
                    check(
                        response_matches(room(
                            fighters[1],
                            "input",
                            match=matches[0],
                            assignment=assignments[1],
                            number=frame,
                            value="32",
                         _observe_before=True), "input rejected"),
                        "old match cannot address replacement fight",
                    )
                if frame == 12:
                    group.proxies[4].configure(
                        to_client=90, to_server=50, jitter=20, drop_every=4
                    )
                if frame == 45:
                    group.proxies[4].configure()
                for seat, role in enumerate(fighters):
                    bits = (
                        (
                            8
                            | (32 if frame % 20 < 8 else 0)
                            | (64 if frame in (20, 70) else 0)
                        )
                        if seat == 0
                        else (4 if frame % 17 < 4 else 0)
                    )
                    if generation == 1 and seat == 0 and frame <= 15:
                        bits = 8
                    if generation == 1 and seat == 1 and frame <= 8:
                        bits |= 32
                    check(
                        response_matches(room(
                            role,
                            "input",
                            match=match,
                            assignment=assignments[seat],
                            number=frame,
                            value=str(bits),
                        ), "accepted"),
                        "only legal current-owner combat input accepted",
                    )
                state = group.command("server", "step")
                check(
                    state["battle_frame"] == frame,
                    "authority advances exactly one legal pair",
                )
                ledger[frame] = (state["battle_x"], state["battle_health"])
                objects[frame] = state["battle_objects"]
                semantics[frame] = state["battle_state"]
                confirmed[frame] = clock
                replacement_damage |= state["battle_p1_health"] < (
                    12000 if generation == 1 else 10000
                )
                if frame % 3 == 0 or frame == 1 or not state["active_match"]:
                    for role in fighters:
                        group.wait_for(
                            lambda role=role: any(
                                e.get("current_match") == match
                                and e.get("prediction_state", {}).get("frame") == frame
                                and e.get("current_frame", -1) >= frame
                                and e.get("prediction_state") == semantics[frame]
                                for e in group.events(role)[-30:]
                            ),
                            role + " corrected successor frame",
                            timeout=30,
                        )
                        corrected = next(
                            e
                            for e in reversed(group.events(role))
                            if e.get("current_match") == match
                            and e.get("prediction_state", {}).get("frame") == frame
                            and e.get("current_frame", -1) >= frame
                            and e.get("prediction_state") == semantics[frame]
                        )
                        check(
                            (corrected["prediction_x"], corrected["prediction_health"])
                            == ledger[frame]
                            and corrected["prediction_objects"] == objects[frame],
                            "both real fighters equal independent authority including projectile phase",
                        )
                        check(
                            corrected["prediction_state"] == semantics[frame],
                            "both real fighters equal full independent native state",
                        )
                if generation == 0 and frame == 22:
                    tick(clock + a.delay)
                    show(frozen, match, 20, ledger[20])
                if frame == 25:
                    tick(clock + a.delay)
                    observer = "client4"
                    # A retained cursor advertises its old content until the viewer
                    # requests the successor. Inspect the requested manifest before
                    # asserting access; a first content rejection is recoverable.
                    room(observer, "select-match", match=match)
                    auth(observer, retry=True)
                    selected = room(observer, "select-match", match=match)
                    check(
                        response_matches(selected, "accepted") and selected["match"] == match,
                        "viewer selects successor after installed content refresh",
                    )
                    shown = show(observer, match, 20, ledger[20])
                    capture_room_display(group, observer, match_identity=match,
                                         room_identity=selected["room"], member_role="spectator",
                                         cursor=shown["frame"], history_start=0, buffering=False)
                    if combined:
                        combined.checkpoint(
                            generation,
                            2,
                            room,
                            auth,
                            roles,
                            fighters,
                            matches,
                            ledgers,
                            clock,
                            current_package,
                            frozen,
                            semantic_ledgers,
                            confirmation_ledgers,
                            a.delay,
                        )
                if frame % 60 == 0:
                    progress("generation " + str(generation) + " frame " + str(frame))
                if not state["active_match"]:
                    break
            check(
                not state["active_match"],
                "normal competitive battle actually completes within fixture bound",
            )
            if generation == 1:
                check(
                    replacement_damage,
                    "legal new-owner attack changes actual opposing fighter health after old-owner rejection",
                )
            check(
                any(
                    health < (12000 if generation == 1 else 10000)
                    for x, health in ledger.values()
                ),
                "successor performs independently observed damage",
            )
            tick(clock + a.delay)
            ended = room("server", "export", match=match)
            check(
                response_matches(ended, "exported")
                and ended.get("replay_valid")
                and ended.get("replay_semantics", {}).get("match") == match
                and bool(ended.get("replay_semantics", {}).get("outcome", "").strip())
                and len(ended.get("replay_semantics", {}).get("frames", [])) == len(ledger),
                "completed successor exports after final release",
            )
            (a.directory / f"ledger-{generation}.json").write_text(
                json.dumps(
                    dict(
                        match=match,
                        frames=ledger,
                        objects=objects,
                        semantic=semantics,
                        confirmed_ticks=confirmed,
                    ),
                    indent=2,
                )
            )
            if generation == 0:
                interrupt_history(
                    group, room, check, roles, match, ledger, current_package
                )
                progress(
                    "completed history transfer interrupted by actual viewer crash and recovered"
                )
                corrupt_history(
                    group, room, check, roles, match, ledger, current_package
                )
                progress(
                    "actual malformed transfer isolates viewer and fresh process recovers"
                )
            if combined:
                combined.checkpoint(
                    generation,
                    3,
                    room,
                    auth,
                    roles,
                    fighters,
                    matches,
                    ledgers,
                    clock,
                    current_package,
                    frozen,
                    semantic_ledgers,
                    confirmation_ledgers,
                    a.delay,
                )
            for role in roles:
                if role not in fighters:
                    if generation == 2 and role == frozen:
                        expired = room(role, "pause")
                        check(
                            response_matches(expired, "history unavailable")
                            and expired["match"] == matches[0],
                            "third terminal match expires old selection without automatic switch",
                        )
                    elif role == frozen:
                        show(role, matches[0], 20, ledgers[0][20])
            check(
                response_matches(room("server", "export", match=matches[-1]), "exported"),
                "latest terminal retained",
            )
            if generation:
                check(
                    response_matches(room("server", "export", match=matches[-2]), "exported"),
                    "second latest terminal retained",
                )
        check(
            response_matches(room("server", "export", match=matches[0], _observe_before=True), "history unavailable"),
            "exactly two terminal matches remain",
        )
        packets = [
            json.loads(line)
            for line in (a.directory / "packets-client4.jsonl").read_text().splitlines()
        ]
        check(
            any(p.get("action") == "drop" for p in packets)
            and any(
                p.get("action") == "deliver" and p.get("scheduled_delay", 0) > 0
                for p in packets
            ),
            "viewer transfer faults actually occurred while fighters completed all successors",
        )
        evaluate_room_displays(group, check)
        (a.directory / "result.json").write_text(
            json.dumps(
                dict(
                    passed=True,
                    assertions=assertions,
                    matches=matches,
                    elapsed=time.monotonic() - began,
                    combined=combined.finish() if combined else None,
                ),
                indent=2,
            )
        )
    except AssertionError as exc:
        if combined:
            combined.failure(exc, phase)
        (a.directory / "failure.json").write_text(
            json.dumps(
                dict(kind="assertion", phase=phase, signature=str(exc)), indent=2
            )
        )
        raise
    except Exception as exc:
        # Missing observations and process/IPC failures are not a behavioral
        # counterexample. Preserve the bounded attempt without invoking the
        # causal shrinker, which requires an observed assertion failure.
        a.directory.mkdir(parents=True, exist_ok=True)
        (a.directory / "failure.json").write_text(
            json.dumps(
                dict(
                    kind="infrastructure",
                    phase=phase,
                    signature=str(exc),
                    exception=type(exc).__name__,
                    assertions=assertions,
                    elapsed=time.monotonic() - began,
                ),
                indent=2,
            )
        )
        raise
    finally:
        group.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

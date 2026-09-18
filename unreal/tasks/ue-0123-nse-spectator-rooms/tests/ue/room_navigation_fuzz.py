from room_response_contract import response_matches, seek_then_pause
"""Replayable independent viewer model over real participants and faulted sockets."""

import json, random, time, uuid
from pathlib import Path
from room_fuzz_batch import run_batch


def make_plan(delay=120, seeds=None):
    seeds = seeds or [24001, 24002, 24003]
    actions = []
    inputs = []
    for seed_index, seed in enumerate(seeds):
        rng = random.Random(seed)
        # Every class has a witness; seeds vary order and arguments, not coverage.
        choices = list(range(15))
        rng.shuffle(choices)
        for step, choice in enumerate(choices):
            role = (seed_index + choice) % 3
            if choice == 12:
                role = seed_index % 3
            actions.append(
                dict(
                    seed=seed,
                    step=step,
                    role=role,
                    choice=choice,
                    number=rng.randrange(1000000),
                    restart=choice == 12,
                )
            )
        inputs.append(
            dict(
                seed=seed,
                p1=rng.choice([0, 4, 8, 32, 64]),
                p2=rng.choice([0, 4, 8, 32, 64]),
            )
        )
    return dict(format=1, delay=delay, seeds=seeds, actions=actions, inputs=inputs)


def run_fuzz(
    group,
    room,
    check,
    roles,
    match,
    assignments,
    ledger,
    progress,
    package,
    delay=120,
    plan=None,
    semantic_ledger=None,
):
    plan = plan or make_plan(delay)
    directory = group.directory
    (directory / "fuzz-plan.json").write_text(json.dumps(plan, indent=2))
    check(
        plan["format"] == 1 and plan["delay"] == delay,
        "replayed plan matches disclosure configuration",
    )
    model = {}
    for role in roles[2:]:
        paused = room(role, "pause")
        check(response_matches(paused, "accepted") and paused["match"] == match
              and paused["frame"] in ledger,
              "initial explicit pause selects independently recorded retained gameplay")
        model[role] = dict(frame=paused["frame"], mode="paused")
    confirmed = {frame: frame for frame in ledger}
    clock = 160 + delay + 20
    classes = set()
    executed = []
    restarts = []
    fault_evidence = []
    try:
        for seed_index, seed in enumerate(plan["seeds"]):
            edge = max(frame for frame in ledger if confirmed[frame] + delay <= clock)
            for expected in model.values():
                if expected["mode"] in ("live", "playing"):
                    expected["frame"] = edge
            for role, expected in model.items():
                if expected["mode"] in ("live", "playing"):
                    group.wait_for(
                        lambda role=role: any(
                            e.get("type") == "presentation" and e.get("frame") == edge
                            for e in group.events(role)[-30:]
                        ),
                        role + " released playback catches up before next seed",
                        timeout=30,
                    )
            proxy = group.proxies[2 + seed_index % 3]
            packet_log = directory / f"packets-client{2+seed_index%3}.jsonl"
            fault_offset = (
                len(packet_log.read_text().splitlines()) if packet_log.exists() else 0
            )
            proxy.configure(
                to_server=20 + seed % 4 * 10,
                to_client=20 + seed % 5 * 10,
                jitter=10,
                finite_faults=True,
                context=f"navigation-{seed}",
            )
            actions = [a for a in plan["actions"] if a["seed"] == seed]
            action_index = 0
            while action_index < len(actions):
                action = actions[action_index]
                if action["choice"] not in (0, 4, 12):
                    batch = []
                    messages = {0: 0, 1: 0, 2: 0}
                    while action_index < len(actions) and len(batch) < 6:
                        candidate = actions[action_index]
                        count = 2 if candidate["choice"] == 7 else 1
                        if (
                            candidate["choice"] in (0, 4, 12)
                            # Distinct actions for one role depend on its previous receipt.
                            # Choice 7 still sends its duplicate pair within one action.
                            or messages[candidate["role"]] > 0
                        ):
                            break
                        messages[candidate["role"]] += count
                        batch.append(candidate)
                        action_index += 1
                    run_batch(
                        batch,
                        group,
                        check,
                        roles,
                        match,
                        assignments,
                        ledger,
                        model,
                        edge,
                        clock,
                        executed,
                        classes,
                    )
                    continue
                action_index += 1
                executed.append(action)
                role = roles[2 + action["role"]]
                expected = model[role]
                choice = action["choice"]
                classes.add(choice)
                before = len(group.events(role))
                status = "accepted"
                if choice == 0:
                    target = action["number"] % (edge + 1)
                    sought, delivery = seek_then_pause(room, check, role, match, target, edge)
                    group.wait_for(lambda: group.presentation(role, sought["nonce"]) is not None,
                                   "correlated seek presents its requested destination", timeout=30)
                    sought_state = group.presentation(role, sought["nonce"])
                    check(sought_state["frame"] == target
                          and (sought_state["x"], sought_state["health"]) == tuple(ledger[target])
                          and not sought_state.get("integrity_failed"),
                          "seek destination equals independent native ledger before pause")
                    if semantic_ledger is not None:
                        check(sought_state["prediction_state"] == semantic_ledger[target],
                              "seek destination full native state precedes paused baseline")
                    expected.update(frame=delivery["frame"], mode="paused")
                elif choice == 1:
                    delivery = room(
                        role,
                        "seek",
                        match=match,
                        number=-1 if action["number"] % 2 else edge + 1,
                    )
                    status = "seek rejected"
                elif choice == 2:
                    expected["mode"] = "paused"
                    delivery = room(role, "pause")
                elif choice == 3:
                    expected.update(frame=edge, mode="live")
                    delivery = room(role, "live")
                elif choice == 4:
                    room(role, "seek", match=match, number=edge - 1)
                    room(role, "resume")
                    group.wait_for(
                        lambda: any(
                            e.get("type") == "presentation" and e.get("frame") == edge
                            for e in group.events(role)[before:]
                        ),
                        "resume reaches independently known edge",
                        timeout=30,
                    )
                    expected.update(frame=edge, mode="playing")
                    delivery = room(role, "resume")
                elif choice == 5:
                    delivery = room(role, "lock")
                    status = "unauthorized"
                elif choice == 6:
                    delivery = room(
                        role,
                        "input",
                        match=match,
                        assignment=assignments[0],
                        number=max(ledger) + 1,
                        value="32",
                    )
                    status = "input rejected"
                elif choice == 7:
                    nonce = uuid.uuid4().hex
                    room(role, "queue", nonce=nonce)
                    delivery = room(role, "queue", nonce=nonce)
                    status = "duplicate"
                elif choice == 8:
                    delivery = room(role, "withdraw")
                elif choice == 9:
                    delivery = room(role, "start")
                    status = "unauthorized"
                elif choice == 10:
                    delivery = room(
                        role, "ready", match=match, assignment=assignments[0]
                    )
                    status = "stale assignment"
                elif choice == 11:
                    delivery = room(role, "select-match", match="missing-" + str(seed))
                    status = "history unavailable"
                elif choice == 12:
                    old = next(
                        e["membership"]
                        for e in reversed(group.events(role))
                        if e.get("membership")
                    )
                    room(role, "leave")
                    if action.get("restart"):
                        prior = next(
                            e["pid"] for e in reversed(group.events(role)) if "pid" in e
                        )
                        group.restart(role, 2 + action["role"])
                        group.command(role, "content", package=package)
                        current = next(
                            e["pid"] for e in reversed(group.events(role)) if "pid" in e
                        )
                        check(
                            current != prior,
                            "viewer restart uses a fresh actual Unreal process",
                        )
                        restarts.append(
                            dict(
                                seed=seed,
                                step=action["step"],
                                old_pid=prior,
                                new_pid=current,
                            )
                        )
                    authentication_offset = len(group.events(role))
                    group.command(
                        role,
                        "authenticate",
                        identity="client2" if role == "client2restart" else role,
                    )
                    group.wait_for(
                        lambda: any(
                            e.get("type") == "delivery"
                            and e.get("membership")
                            and e["membership"] != old
                            for e in group.events(role)[authentication_offset:]
                        ),
                        "new authenticated generation",
                        timeout=30,
                    )
                    delivery = room(role, "pause", membership=old)
                    status = "stale membership"
                elif choice == 13:
                    prior_delivery = next(
                        event
                        for event in reversed(group.events(role))
                        if event.get("type") == "delivery"
                    )
                    offer_state = {
                        key: (
                            sorted(prior_delivery[key])
                            if key == "roster"
                            else prior_delivery[key]
                        )
                        for key in (
                            "roster",
                            "membership",
                            "assignment",
                            "offer",
                            "member_role",
                        )
                    }
                    delivery = room(role, "offer", number=0, value="client3")
                    status = "unauthorized"
                else:
                    delivery = room(role, "decline", assignment="old-" + str(seed))
                    status = "stale offer"
                label = f"seed {seed} step {action['step']} action {choice}"
                if choice == 13:
                    check(
                        bool(delivery["status"])
                        and bool(delivery.get("status"))
                        and all(
                            (
                                sorted(delivery[key])
                                if key == "roster"
                                else delivery[key]
                            )
                            == value
                            for key, value in offer_state.items()
                        ),
                        label + " refusal preserves authoritative offer and ownership",
                    )
                else:
                    check(response_matches(delivery, status), label + " outcome")
                check(
                    delivery["match"] == match
                    and delivery["frame"] == expected["frame"]
                    and delivery["mode"] == expected["mode"],
                    label + " independent selected timeline/cursor/mode",
                )
                check(
                    delivery["edge"] == edge,
                    label + " independently timestamped released prefix",
                )
                group.wait_for(
                    lambda: any(
                        e.get("type") == "presentation"
                        for e in group.events(role)[before:]
                    ),
                    label + " actual presentation",
                    timeout=30,
                )
                shown = next(
                    e
                    for e in reversed(group.events(role))
                    if e.get("type") == "presentation"
                )
                check(
                    not shown.get("integrity_failed", False)
                    and shown["frame"] == expected["frame"]
                    and (shown["x"], shown["health"])
                    == tuple(ledger[expected["frame"]]),
                    label + " actual reconstructed native state",
                )
                with (directory / "fuzz-observations.jsonl").open("a") as stream:
                    stream.write(
                        json.dumps(
                            dict(
                                action=action,
                                tick=clock,
                                edge=edge,
                                expected=dict(expected),
                                actual={
                                    k: delivery[k]
                                    for k in ("frame", "mode", "match", "status")
                                },
                                presented={
                                    k: shown[k] for k in ("frame", "x", "health")
                                },
                            )
                        )
                        + "\n"
                    )
            # Drive a bounded read-only witness if a shrunk plan has no traffic.
            for _ in range(4):
                if proxy.finite_faults_complete():
                    break
                room(roles[2 + seed_index % 3], "observe")
            group.wait_for(proxy.finite_faults_complete,
                           "finite loss/duplicate/reorder witness delivered", timeout=10)
            proxy.configure()
            packets = [
                json.loads(line)
                for line in packet_log.read_text().splitlines()[fault_offset:]
            ]
            check(
                any(p.get("action") == "drop" for p in packets)
                and any(
                    p.get("action") == "deliver" and p.get("scheduled_delay", 0) > 0
                    for p in packets
                ),
                "seed " + str(seed) + " actually delivered external loss/delay",
            )
            check(
                any(
                    p.get("action") == "deliver" and p.get("duplicate") for p in packets
                ),
                "seed " + str(seed) + " actual duplicate datagrams delivered",
            )
            reordered = False
            for direction in ("to_server", "to_client"):
                sequences = [
                    p["sequence"]
                    for p in packets
                    if p.get("action") == "deliver"
                    and p.get("direction") == direction
                    and not p.get("duplicate")
                ]
                reordered |= any(a > b for a, b in zip(sequences, sequences[1:]))
            check(
                reordered,
                "seed " + str(seed) + " actual datagram order inversion observed",
            )
            fault_evidence.append(
                dict(
                    seed=seed,
                    packet_log=str(packet_log),
                    start_row=fault_offset,
                    rows=len(packets),
                    duplicate=True,
                    reorder=True,
                )
            )
            frame = max(ledger) + 1
            clock += 1
            group.command("server", "clock", tick=clock)
            inputs = next(row for row in plan["inputs"] if row["seed"] == seed)
            for seat in range(2):
                check(
                    response_matches(room(
                        roles[seat],
                        "input",
                        match=match,
                        assignment=assignments[seat],
                        number=frame,
                        value=str(inputs["p1" if seat == 0 else "p2"]),
                    ), "accepted"),
                    "healthy fighter input after recovered fuzz seed",
                )
            state = group.command("server", "step")
            check(
                state["battle_frame"] == frame,
                "fighter progress after navigation/auth faults",
            )
            ledger[frame] = (state["battle_x"], state["battle_health"])
            confirmed[frame] = clock
            if semantic_ledger is not None:
                semantic_ledger[frame] = state["battle_state"]
            for role in roles[:2]:
                group.wait_for(
                    lambda role=role: any(
                        e.get("prediction_frame") == frame
                        and e.get("prediction_confirmed", -1) >= frame
                        for e in group.events(role)[-30:]
                    ),
                    "both GGPO peers confirm fuzz continuation",
                    timeout=30,
                )
                state = next(
                    e
                    for e in reversed(group.events(role))
                    if e.get("prediction_frame") == frame
                    and e.get("prediction_confirmed", -1) >= frame
                )
                check(
                    (state["prediction_x"], state["prediction_health"])
                    == tuple(ledger[frame]),
                    "corrected continuation agrees with authority",
                )
                if semantic_ledger is not None:
                    check(
                        state["prediction_state"] == semantic_ledger[frame],
                        "corrected continuation full semantic state equals independent authority",
                    )
            if seed % 3 == 0:
                observer = roles[2 + seed_index % 3]
                if delay > 0:
                    clock += delay - 1
                    group.command("server", "clock", tick=clock)
                    # Advancing the release clock may legitimately move a live
                    # cursor. Observe that current state before asserting a rejected
                    # seek leaves it unchanged; the target itself must stay gated.
                    withheld = room(observer, "seek", match=match, number=frame,
                                    _observe_before=True)
                    check(
                        response_matches(withheld, "seek rejected")
                        and withheld["frame"] < frame and withheld["edge"] < frame,
                        f"seed {seed} newly confirmed frame absent at t+D-1",
                    )
                    permitted = max(f for f in ledger if confirmed[f] + delay <= clock)
                    healthy = room(observer, "seek", match=match, number=permitted)
                    check(
                        response_matches(healthy, "accepted")
                        and healthy["frame"] == permitted,
                        f"seed {seed} same connection delivers permitted history before release",
                    )
                    clock += 1
                    group.command("server", "clock", tick=clock)
                released = room(observer, "seek", match=match, number=frame)
                check(
                    response_matches(released, "accepted") and released["frame"] == frame,
                    f"seed {seed} newly confirmed frame available exactly at t+D",
                )
                group.wait_for(
                    lambda: any(
                        e.get("type") == "presentation"
                        and e.get("nonce") == released["nonce"]
                        for e in group.events(observer)
                    ),
                    f"seed {seed} released native presentation",
                    timeout=30,
                )
                shown = next(
                    e
                    for e in reversed(group.events(observer))
                    if e.get("type") == "presentation"
                    and e.get("nonce") == released["nonce"]
                )
                check(
                    (shown["x"], shown["health"]) == tuple(ledger[frame]),
                    f"seed {seed} actual newly released native state",
                )
                if semantic_ledger is not None:
                    check(
                        shown["prediction_state"] == semantic_ledger[frame],
                        f"seed {seed} complete newly released native state",
                    )
                paused = room(observer, "pause")
                check(response_matches(paused, "accepted") and paused["match"] == match
                      and paused["frame"] == frame,
                      "explicit pause holds newly released terminal edge")
                model[observer].update(frame=frame, mode="paused")
            (directory / "confirmed-ticks.json").write_text(
                json.dumps(confirmed, indent=2)
            )
            (directory / "confirmed-ledger.json").write_text(
                json.dumps(ledger, indent=2)
            )
            progress(
                f"process fuzz seed {seed}, {len(actions)} operations, confirmed frame {frame}"
            )
        if not plan.get("shrinking"):
            check(
                classes == set(range(15)),
                "all published process fuzz action classes exercised",
            )
        return dict(
            seeds=plan["seeds"],
            operations=len(executed),
            action_classes=sorted(classes),
            restarts=restarts,
            faults=fault_evidence,
            confirmed_ticks=confirmed,
            final_room_tick=clock,
        )
    except AssertionError as error:
        observed = []
        fault_classes = set()
        for packet_log in directory.glob("packets-*.jsonl"):
            rows = [json.loads(line) for line in packet_log.read_text().splitlines()]
            if any(row.get("action") == "drop" for row in rows):
                fault_classes.add("drop")
            if any(
                row.get("action") == "deliver" and row.get("scheduled_delay", 0) > 0
                for row in rows
            ):
                fault_classes.add("delay")
            if any(
                row.get("action") == "deliver" and row.get("duplicate") for row in rows
            ):
                fault_classes.add("duplicate")
            for direction in ("to_server", "to_client"):
                seq = [
                    row["sequence"]
                    for row in rows
                    if row.get("action") == "deliver"
                    and row.get("direction") == direction
                    and not row.get("duplicate")
                ]
                if any(a > b for a, b in zip(seq, seq[1:])):
                    fault_classes.add("reorder")
            if any(row.get("action") == "drop" for row in rows) and any(
                row.get("action") == "deliver" and row.get("scheduled_delay", 0) > 0
                for row in rows
            ):
                observed.append(str(packet_log))
        failure = dict(plan, actions=executed, shrinking=True)
        (directory / "failing-plan.json").write_text(json.dumps(failure, indent=2))
        (directory / "failure.json").write_text(
            json.dumps(
                dict(
                    kind="assertion",
                    signature=str(error).split("; see ", 1)[0],
                    phase="process-fuzz",
                    faults=fault_evidence,
                    restarts=restarts,
                    observed_fault_logs=observed,
                    fault_classes=sorted(fault_classes),
                ),
                indent=2,
            )
        )
        raise

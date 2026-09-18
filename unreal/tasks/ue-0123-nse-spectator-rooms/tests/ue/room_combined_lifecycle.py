from room_response_contract import response_matches, seek_then_pause
"""Generated room actions interleaved with real content, seat and match transitions."""

import json, random, time, uuid


class CombinedLifecycle:
    def __init__(self, group, check, pilot=False, replay_plan=None, delay=120, per_phase=50):
        self.group, self.check = group, check
        self.delay = delay
        self.seeds = [24001, 24009, 24017]
        # Keep legacy pilot/per_phase arguments readable in saved invocations.
        # Coverage is now a fixed complete class schedule at each checkpoint.
        self.pilot = pilot
        self.rows = []
        self.classes = set()
        self.phase = "startup"
        self.generation = 0
        self.active_context = None
        self.plan = []
        for index, seed in enumerate(self.seeds):
            generation = index
            rng = random.Random(seed)
            for phase in range(4):
                choices = list(range(12))
                rng.shuffle(choices)
                for step, choice in enumerate(choices):
                    self.plan.append(
                        dict(
                            seed=seed,
                            generation=generation,
                            phase=phase,
                            step=phase * 12 + step,
                            choice=choice,
                            number=rng.randrange(1000000),
                        )
                    )
        self.shrinking = bool(replay_plan)
        if replay_plan:
            saved = json.loads(replay_plan.read_text())
            self.plan = saved["actions"]
            self.seeds = saved["seeds"]
            self.pilot = saved["pilot"]
            if saved["delay"] != delay:
                raise ValueError("Combined replay must preserve the original delay")
        (group.directory / "combined-plan.json").write_text(
            json.dumps(
                dict(
                    pilot=self.pilot, seeds=self.seeds, actions=self.plan, delay=delay
                ),
                indent=2,
            )
        )

    def record(self, role, operation, args, reply):
        # Receipts resolve newly allocated identities without predicting them.
        row = dict(
            kind="lifecycle",
            seed=self.seeds[self.generation],
            causal_index=len(self.rows),
            generation=self.generation,
            phase=self.phase,
            role=role,
            operation=operation,
            arguments=args,
            receipt={
                k: reply.get(k)
                for k in (
                    "status",
                    "match",
                    "membership",
                    "assignment",
                    "frame",
                    "mode",
                    "acknowledgement",
                )
            },
        )
        self._write(row)

    def _write(self, row):
        self.rows.append(row)
        with (self.group.directory / "combined-actions.jsonl").open("a") as out:
            out.write(json.dumps(row) + "\n")

    def checkpoint(
        self,
        generation,
        phase,
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
        delay,
    ):
        self.generation = generation
        self.phase = f"generated-{phase}"
        viewers = [r for r in roles if r not in fighters and r != frozen]
        actions = [
            a
            for a in self.plan
            if a["generation"] == generation and a["phase"] == phase
        ]
        began = time.monotonic()
        model = {}
        packet_offsets = {
            r: len(
                (self.group.directory / f"packets-{r}.jsonl").read_text().splitlines()
            )
            for r in viewers
        }
        for role in viewers:
            if matches:
                initialized, paused = seek_then_pause(
                    room, self.check, role, matches[-1], 0,
                    max(frame for frame, tick in confirmation_ledgers[-1].items()
                        if tick + delay <= clock))
                self.check(
                    response_matches(initialized, "accepted") and initialized["frame"] == 0,
                    "generated checkpoint starts from independently known frame zero",
                )
                model[role] = dict(frame=paused["frame"], match=matches[-1], mode="paused")
            else:
                model[role] = dict(frame=-1, match="", mode="paused")
        for role in viewers:
            self.group.proxies[int(role.removeprefix("client"))].configure(
                finite_faults=True, context=f"lifecycle-{generation}-{phase}-{role}")
        for action in actions:
            role = viewers[action["number"] % len(viewers)]
            choice = action["choice"]
            self.classes.add(choice)
            proxy = self.group.proxies[int(role.removeprefix("client"))]
            self.active_context = (
                f"seed-{action['seed']}-phase-{phase}-step-{action['step']}"
            )
            expected_frame = model[role]["frame"]
            expected_match = model[role]["match"]
            expected_mode = "paused"
            status = "accepted"
            operation = "pause"
            args = {}
            target = None
            if matches:
                target = len(matches) - 1
                edge = max(
                    frame
                    for frame, tick in confirmation_ledgers[target].items()
                    if tick + delay <= clock
                )
            if choice == 0 and target is not None:
                expected_frame = 0 if action["number"] % 2 else edge
                expected_match = matches[target]
                expected_mode = "paused"
                operation = "seek"
                args = dict(match=expected_match, number=expected_frame)
            elif choice == 1 and target is not None:
                operation = "seek"
                args = dict(match=matches[target], number=edge + 1)
                status = "seek rejected"
            elif choice == 2:
                operation = "lock"
                status = "unauthorized"
            elif choice == 3:
                operation = "queue"
            elif choice == 4:
                operation = "withdraw"
            elif choice == 5:
                operation = "ready"
                args = dict(
                    assignment="retired-" + str(action["seed"]),
                    match=matches[-1] if matches else "",
                )
                status = "stale assignment"
            elif choice == 6:
                operation = "input"
                args = dict(
                    assignment="retired-" + str(action["seed"]),
                    match=matches[-1] if matches else "",
                    number=1,
                    value="32",
                )
                status = "input rejected"
            elif choice == 7:
                nonce = uuid.uuid4().hex
                room(role, "queue", nonce=nonce)
                operation = "queue"
                args = dict(nonce=nonce)
                status = "duplicate"
            elif choice == 8:
                old = room(role, "pause")["membership"]
                room(role, "leave")
                auth(role)
                operation = "pause"
                args = dict(membership=old)
                status = "stale membership"
            elif choice == 9:
                auth(role, retry=True)
                operation = "pause"
            elif choice == 10:
                room("server", "lock")
                reply = room("server", "unlock")
                self.check(
                    not reply["locked"],
                    "generated organizer lock/unlock remains usable",
                )
            elif choice == 11:
                operation = "select-match"
                args = dict(match="unretained-" + str(action["seed"]))
                status = "history unavailable"
            if not matches and operation == "pause" and status == "accepted":
                operation = "withdraw"
            reply = room(role, operation,
                         _observe_before=status not in ("accepted", "duplicate"), **args)
            label = (
                f"combined seed {action['seed']} step {action['step']} phase {phase}"
            )
            self.check(response_matches(reply, status), label + " independent outcome")
            if matches:
                self.check(
                    (reply["frame"], reply["match"])
                    == (expected_frame, expected_match)
                    and (operation == "seek" and status == "accepted"
                         or reply["mode"] == expected_mode),
                    label + " independent selected history",
                )
            model[role] = dict(
                frame=expected_frame, match=expected_match, mode=expected_mode
            )
            if operation == "seek" and status == "accepted":
                offset = len(self.group.events(role))
                self.group.wait_for(
                    lambda: self.group.presentation(role, reply["nonce"]) is not None,
                    label + " native presentation",
                    timeout=30,
                )
                shown = self.group.presentation(role, reply["nonce"])
                self.check(
                    (shown["x"], shown["health"])
                    == tuple(ledgers[target][expected_frame]),
                    label + " native combat ledger",
                )
                self.check(
                    shown["prediction_state"]
                    == semantic_ledgers[target][expected_frame],
                    label + " full native combat ledger",
                )
            if operation == "seek" and status == "accepted":
                paused = room(role, "pause")
                self.check(response_matches(paused, "accepted")
                           and paused["match"] == expected_match
                           and expected_frame <= paused["frame"] <= edge
                           and (reply["mode"] != "paused" or paused["frame"] == expected_frame),
                           label + " explicit pause establishes released baseline")
                model[role] = dict(frame=paused["frame"], match=expected_match, mode="paused")
            self._write(
                dict(
                    kind="generated",
                    action=action,
                    tick=clock,
                    expected=dict(
                        frame=expected_frame,
                        match=expected_match,
                        mode=expected_mode,
                        status=status,
                    ),
                    actual={k: reply[k] for k in ("frame", "match", "mode", "status")},
                    next_paused_baseline=dict(model[role]),
                    seconds=time.monotonic() - began,
                )
            )
        for role in viewers:
            proxy = self.group.proxies[int(role.removeprefix("client"))]
            for _ in range(4):
                if proxy.finite_faults_complete():
                    break
                room(role, "observe")
            self.group.wait_for(proxy.finite_faults_complete,
                                "finite lifecycle packet witness delivered", timeout=10)
            proxy.configure()
        room("server", "unlock")
        # The deterministic lifecycle maintains one viewer across all travel.
        # Re-establish that deliberate boundary after independently checked fuzz.
        if matches and generation < 2:
            _, paused = seek_then_pause(room, self.check, frozen, matches[0], 20,
                                        max(ledgers[0]))
            self.frozen_cursor = paused["frame"]
        packets = []
        for role in viewers:
            path = self.group.directory / f"packets-{role}.jsonl"
            packets.extend(
                json.loads(line)
                for line in path.read_text().splitlines()[packet_offsets[role] :]
            )
        if actions:
            self.check(
                any(row.get("action") == "drop" for row in packets),
                "combined checkpoint actual packet loss occurred",
            )
        if actions:
            self.check(
                any(
                    row.get("action") == "deliver" and row.get("scheduled_delay", 0) > 0
                    for row in packets
                ),
                "combined checkpoint actual delayed delivery occurred",
            )
        self._write(
            dict(
                kind="fault-window",
                generation=generation,
                phase=phase,
                packet_start_rows=packet_offsets,
                delivered_rows=len(packets),
            )
        )
        self.phase = "lifecycle"
        self.active_context = None
        with (self.group.directory / "combined-timings.jsonl").open("a") as out:
            out.write(
                json.dumps(
                    dict(
                        generation=generation,
                        phase=phase,
                        commands=len(actions),
                        seconds=time.monotonic() - began,
                    )
                )
                + "\n"
            )

    def failure(self, error, phase):
        fault_classes = set()
        context_rows = []
        for path in self.group.directory.glob("packets-*.jsonl"):
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            context_rows.extend(
                r
                for r in rows
                if self.active_context is not None
                and r.get("fault_context") == self.active_context
            )
            if any(r.get("action") == "drop" for r in rows):
                fault_classes.add("drop")
            if any(
                r.get("action") == "deliver" and r.get("scheduled_delay", 0) > 0
                for r in rows
            ):
                fault_classes.add("delay")
            if any(r.get("action") == "deliver" and r.get("duplicate") for r in rows):
                fault_classes.add("duplicate")
            for direction in ("to_server", "to_client"):
                seq = [
                    r["sequence"]
                    for r in rows
                    if r.get("action") == "deliver"
                    and r.get("direction") == direction
                    and not r.get("duplicate")
                ]
                if any(a > b for a, b in zip(seq, seq[1:])):
                    fault_classes.add("reorder")
        milestones = [
            (r["generation"], r["role"], r["operation"])
            for r in self.rows
            if r["kind"] == "lifecycle"
            and r["phase"] == "lifecycle"
            and r["operation"] in ("accept", "ready", "start", "character", "stage")
        ]
        recoveries = [
            name
            for name in (
                "interrupted-history-transfer.json",
                "corrupt-history-transfer.json",
            )
            if (self.group.directory / name).exists()
        ]
        causal_faults = sorted(
            {r["action"] for r in context_rows if r["action"] == "drop"}
            | {
                "delay"
                for r in context_rows
                if r["action"] == "deliver" and r.get("scheduled_delay", 0) > 0
            }
            | {
                "duplicate"
                for r in context_rows
                if r["action"] == "deliver" and r.get("duplicate")
            }
        )
        for direction in ("to_server", "to_client"):
            seq = [
                r["fault_sequence"]
                for r in context_rows
                if r["action"] == "deliver"
                and r["direction"] == direction
                and not r.get("duplicate")
            ]
            if (
                any(a > b for a, b in zip(seq, seq[1:]))
                and "reorder" not in causal_faults
            ):
                causal_faults.append("reorder")
        report = dict(
            kind="assertion",
            signature=str(error),
            phase=phase,
            fault_classes=sorted(fault_classes),
            fault_context=self.active_context,
            causal_faults=causal_faults,
            causal_packet_rows=context_rows,
            milestones=milestones,
            recoveries=recoveries,
        )
        (self.group.directory / "combined-failure.json").write_text(
            json.dumps(report, indent=2)
        )

    def fault_order(self, generation):
        seed = self.seeds[generation]
        return (False, True) if random.Random(seed).randrange(2) else (True, False)

    def ready_order(self, generation, fighters):
        rows = list(enumerate(fighters))
        random.Random(self.seeds[generation]).shuffle(
            rows
        )
        return rows

    def finish(self):
        generated = [r for r in self.rows if r["kind"] == "generated"]
        self.check(
            len(generated) == len(self.plan),
            "every generated lifecycle action executed",
        )
        if not self.pilot and not self.shrinking:
            self.check(
                self.classes == set(range(12)),
                "all generated lifecycle action classes covered",
            )
        return dict(
            pilot=self.pilot,
            seeds=self.seeds,
            commands=len(generated),
            classes=sorted(self.classes),
        )

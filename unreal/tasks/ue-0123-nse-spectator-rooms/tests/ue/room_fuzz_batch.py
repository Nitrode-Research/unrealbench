from room_response_contract import response_matches, annotate_response
"""Batch causally independent viewer RPCs, preserving every per-command assertion."""

import json, uuid


def run_batch(
    actions,
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
):
    pending = []
    for action in actions:
        role = roles[2 + action["role"]]
        expected = model[role]
        choice = action["choice"]
        classes.add(choice)
        executed.append(action)
        nonce = uuid.uuid4().hex
        status = "accepted"
        values = dict(nonce=nonce)
        if choice == 0:
            raise ValueError("Seeking followed by explicit pause is a causal barrier")
        elif choice == 1:
            operation = "seek"
            values.update(match=match, number=-1 if action["number"] % 2 else edge + 1)
            status = "seek rejected"
        elif choice == 2:
            operation = "pause"
            expected["mode"] = "paused"
        elif choice == 3:
            operation = "live"
            expected.update(frame=edge, mode="live")
        elif choice == 5:
            operation = "lock"
            status = "unauthorized"
        elif choice == 6:
            operation = "input"
            values.update(
                match=match,
                assignment=assignments[0],
                number=max(ledger) + 1,
                value="32",
            )
            status = "input rejected"
        elif choice == 7:
            operation = "queue"
            status = "duplicate"
        elif choice == 8:
            operation = "withdraw"
        elif choice == 9:
            operation = "start"
            status = "unauthorized"
        elif choice == 10:
            operation = "ready"
            values.update(match=match, assignment=assignments[0])
            status = "stale assignment"
        elif choice == 11:
            operation = "select-match"
            values.update(match="missing-" + str(action["seed"]))
            status = "history unavailable"
        elif choice == 13:
            operation = "offer"
            values.update(number=0, value="client3")
            status = "unauthorized"
        elif choice == 14:
            operation = "decline"
            values.update(assignment="old-" + str(action["seed"]))
            status = "stale offer"
        else:
            raise ValueError("Causal barriers cannot be included in a simple batch")
        prior_delivery = next(
            event
            for event in reversed(group.events(role))
            if event.get("type") == "delivery"
        )
        offer_state = {
            key: sorted(prior_delivery[key]) if key == "roster" else prior_delivery[key]
            for key in ("roster", "membership", "assignment", "offer", "member_role")
        }
        offset = len(group.events(role))
        ids = []
        for _ in range(2 if choice == 7 else 1):
            ids.append(group.send(role, "room", operation=operation, **values))
        pending.append(
            dict(
                action=action,
                role=role,
                nonce=nonce,
                offset=offset,
                ids=ids,
                expected=dict(expected),
                status=status,
                offer_state=offer_state,
                before=prior_delivery, operation=operation, arguments=values,
            )
        )
    for item in pending:
        role = item["role"]
        nonce = item["nonce"]
        action = item["action"]
        expected = item["expected"]
        label = f"seed {action['seed']} step {action['step']} action {action['choice']}"
        for identifier in item["ids"]:
            group.reply(role, identifier)

        def receipts():
            return [
                e
                for e in group.events(role)[item["offset"] :]
                if e.get("type") == "delivery" and e.get("nonce") == nonce
            ]

        group.wait_for(
            lambda: len(receipts()) >= len(item["ids"]),
            label + " all command acknowledgments",
            timeout=30,
        )
        delivery = annotate_response(receipts()[-1],
            receipts()[0] if len(item["ids"]) > 1 else item["before"],
            item["operation"], item["arguments"])
        if action["choice"] == 13:
            check(
                bool(delivery["status"])
                and bool(delivery.get("status"))
                and all(
                    (sorted(delivery[key]) if key == "roster" else delivery[key])
                    == value
                    for key, value in item["offer_state"].items()
                ),
                label + " refusal preserves authoritative offer and ownership",
            )
        else:
            check(response_matches(delivery, item["status"]), label + " outcome")
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
            lambda: sum(
                e.get("type") == "presentation" and e.get("nonce") == nonce
                for e in group.events(role)[item["offset"] :]
            )
            >= len(item["ids"]),
            label + " every actual native presentation",
            timeout=30,
        )
        shown = next(
            e
            for e in reversed(group.events(role))
            if e.get("type") == "presentation" and e.get("nonce") == nonce
        )
        check(
            not shown.get("integrity_failed", False)
            and shown["frame"] == expected["frame"]
            and (shown["x"], shown["health"]) == tuple(ledger[expected["frame"]]),
            label + " actual reconstructed native state",
        )
        with (group.directory / "fuzz-observations.jsonl").open("a") as stream:
            stream.write(
                json.dumps(
                    dict(
                        action=action,
                        tick=clock,
                        edge=edge,
                        expected=expected,
                        actual={
                            k: delivery[k] for k in ("frame", "mode", "match", "status")
                        },
                        presented={k: shown[k] for k in ("frame", "x", "health")},
                        batch_size=len(actions),
                    )
                )
                + "\n"
            )

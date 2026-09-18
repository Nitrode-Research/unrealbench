from room_response_contract import annotate_response, prior_delivery, response_matches
"""Actual authority death before response, after durable acknowledgment and in travel.
A missing response permits either complete durable outcome; partial state never does.
Runs two workers at a time. This complements the full battle-prefix crash scenario.
"""

import argparse, json, signal, uuid
from pathlib import Path
from room_network_workers import WorkerGroup, force_kill_authority


def main():
    p = argparse.ArgumentParser()
    p.add_argument("project", type=Path)
    p.add_argument("directory", type=Path)
    p.add_argument("--editor", type=Path, required=True)
    p.add_argument(
        "--window",
        choices=["before-response", "after-ack", "travel", "all"],
        default="all",
    )
    p.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    a = p.parse_args()
    a.directory.mkdir(parents=True, exist_ok=True)
    assertions = 0
    trace = []
    group = None

    def check(ok, message):
        nonlocal assertions
        assert ok, message
        assertions += 1

    def room(role, operation, nonce=None, **values):
        if values.pop("_observe_before", False):
            room(role, "observe")
        response_before = prior_delivery(group, role)
        nonce = nonce or uuid.uuid4().hex
        group.command(role, "room", operation=operation, nonce=nonce, **values)
        group.wait_for(
            lambda: any(
                e.get("type") == "delivery" and e.get("nonce") == nonce
                for e in group.events(role)
            ),
            operation + " acknowledged",
            timeout=30,
        )
        return annotate_response(next(
            e
            for e in reversed(group.events(role))
            if e.get("nonce") == nonce and e.get("type") == "delivery"
        ), response_before, operation, values)

    try:
        for window in (
            ["before-response", "after-ack", "travel"]
            if a.window == "all"
            else [a.window]
        ):
            slot = "crash-" + uuid.uuid4().hex
            nonce = uuid.uuid4().hex
            group = WorkerGroup(
                a.editor,
                a.project,
                a.directory / window / "before",
                editor_args=a.editor_args.split(),
                extra_args=["-NullRHI"],
            )
            group.start(1)
            group.command("server", "create", slot=slot, delay=120)
            group.command("client0", "authenticate", identity="viewer")
            group.wait_for(
                lambda: any(e.get("membership") for e in group.events("client0")),
                "authenticated member",
            )
            member = room("client0", "queue")
            check(response_matches(member, "accepted"), "queue durably acknowledged")
            base = room("server", "unlock")
            check(not base["locked"], "known unlocked state acknowledged")
            authority = next(e["pid"] for e in group.events("server") if "pid" in e)
            if window == "before-response":
                group.send("server", "room", operation="lock", nonce=nonce)
            else:
                locked = room("server", "lock", nonce=nonce)
                check(
                    locked["locked"] and locked["ack"] > base["ack"],
                    "complete lock durable acknowledgment",
                )
                if window == "travel":
                    group.command("server", "travel")
            termination = force_kill_authority(authority)
            group.processes["server"].wait(timeout=30)
            trace.append(
                dict(
                    window=window,
                    pid=authority,
                    signal=termination,
                    nonce=nonce,
                    observed_ack=window != "before-response",
                )
            )
            group.close()
            group = None
            group = WorkerGroup(
                a.editor,
                a.project,
                a.directory / window / "after",
                editor_args=a.editor_args.split(),
                extra_args=["-NullRHI"],
            )
            group.start(1)
            check(
                next(e["pid"] for e in group.events("server") if "pid" in e)
                != authority,
                "new authority process",
            )
            group.command("server", "recover", slot=slot)
            group.command("client0", "authenticate", identity="viewer")
            group.wait_for(
                lambda: any(e.get("membership") for e in group.events("client0")),
                "member restored",
            )
            recovered = next(
                e for e in reversed(group.events("client0")) if e.get("membership")
            )
            offered = room("server", "offer", number=0, value="viewer")
            check(
                response_matches(offered, "accepted"),
                "acknowledged queue survives without requeue",
            )
            check(
                recovered["membership"] == member["membership"],
                "acknowledged membership survives actual authority death",
            )
            check(
                bool(recovered["membership"]) and len(recovered["roster"]) == len(set(recovered["roster"])),
                "one recovered member identity",
            )
            replayed = room("server", "lock", nonce=nonce)
            if window == "before-response":
                check(
                    bool(replayed.get("status")),
                    "unobserved operation is absent or wholly durable",
                )
                check(
                    recovered["locked"] == (response_matches(replayed, "duplicate")),
                    "durable nonce and lock state are atomic",
                )
            else:
                check(
                    response_matches(replayed, "duplicate"),
                    "acknowledged nonce survives authority restart",
                )
            check(replayed["locked"], "idempotent complete lock state after recovery")
            check(
                response_matches(room("client0", "withdraw"), "accepted"),
                "recovered queue remains usable",
            )
            check(
                not room("server", "unlock")["locked"],
                "healthy organizer operation after crash",
            )
            group.close()
            group = None
        (a.directory / "result.json").write_text(
            json.dumps(
                dict(passed=True, assertions=assertions, crashes=trace), indent=2
            )
        )
    finally:
        if group:
            group.close()
        (a.directory / "crash-trace.json").write_text(json.dumps(trace, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

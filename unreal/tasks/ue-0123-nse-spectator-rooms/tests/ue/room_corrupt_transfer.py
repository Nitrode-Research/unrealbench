from room_response_contract import response_matches, restart_authenticator
from room_display_checks import capture_room_display
"""A malformed real owner-RPC transfer stops one viewer while a healthy peer works."""

import json
import uuid


def corrupt_history(group, room, check, roles, match, ledger, package):
    role = roles[4]
    room(role, "seek", match=match, number=32)
    before = len(group.events(role))
    nonce = uuid.uuid4().hex
    group.expected_integrity_errors.add(role)
    try:
        group.command(
            "server", "corrupt-transfer", identity=role, client_index=4, nonce=nonce
        )
        group.wait_for(
            lambda: any(e.get("integrity_failed") for e in group.events(role)[before:]),
            "actual malformed owner-RPC payload produces viewer integrity diagnostic",
            timeout=30,
        )
        rejected = next(
            e for e in reversed(group.events(role)) if e.get("integrity_failed")
        )
        check(
            any(
                event.get("type") == "delivery" and event.get("nonce") == nonce
                for event in group.events(role)[before:]
            ),
            "corrupted transfer actually arrived at affected viewer",
        )
        capture_room_display(group, role, integrity_error=True)
        state_before = (rejected["prediction_x"], rejected["prediction_health"])
        other = room(roles[3], "seek", match=match, number=32)
        check(
            other["frame"] == 32,
            "healthy viewer reaches the same frame while corrupted viewer stops",
        )
        control = next(
            e
            for e in reversed(group.events(roles[3]))
            if e.get("type") == "presentation"
        )
        check(
            (control["x"], control["health"]) == tuple(ledger[32]),
            "healthy same-frame native gameplay defeats blanket error behavior",
        )
        room(role, "seek", match=match, number=20)
        group.wait_for(
            lambda: any(
                e.get("integrity_failed") and e.get("type") == "presentation"
                for e in group.events(role)[before:]
            ),
            "stopped viewer keeps its diagnostic",
        )
        stopped = group.events(role)[-1]
        check(
            stopped.get("integrity_failed")
            and (stopped["prediction_x"], stopped["prediction_health"]) == state_before,
            "further valid messages cannot animate a poisoned view",
        )
        check(
            response_matches(room("server", "unlock"), "accepted"),
            "healthy room controls remain available after corruption",
        )
        offset = len(group.events(role))
        reconnect = restart_authenticator(group, role, role, package)
        group.restart(role, 4, force=True, reconnect=reconnect)
        group.expected_integrity_errors.remove(role)
        group.wait_for(
            lambda: any(
                e.get("type") == "delivery" and e.get("membership")
                for e in group.events(role)[offset:]
            ),
            "fresh process authenticates after corruption",
            timeout=30,
        )
        restored = room(role, "seek", match=match, number=32)
        check(
            restored["frame"] == 32
            and not group.events(role)[-1].get("integrity_failed"),
            "fresh authenticated playback resumes healthy retained history",
        )
        capture_room_display(group, role, integrity_error=False, recovery_pending=False)
        (group.directory / "corrupt-history-transfer.json").write_text(
            json.dumps(
                dict(
                    passed=True,
                    request_nonce=nonce,
                    receipt_observed=True,
                    corrupt_viewer=role,
                    healthy_viewer=roles[3],
                    frame=32,
                ),
                indent=2,
            )
        )
    finally:
        group.expected_integrity_errors.discard(role)

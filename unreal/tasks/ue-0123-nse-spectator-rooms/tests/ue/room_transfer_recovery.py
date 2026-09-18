from room_response_contract import response_matches, seek_then_pause, restart_authenticator
"""Crash a real viewer while an accepted history export is in flight."""

import json, time, uuid


def interrupt_history(group, room, check, roles, match, ledger, package):
    role = roles[4]
    proxy = group.proxies[4]
    sought, selected = seek_then_pause(room, check, role, match, 32, max(ledger))
    cursor = selected["frame"]
    check(
        selected["match"] == match and selected["mode"] == "paused",
        "viewer selects known terminal history before interrupted download",
    )
    before = len(group.events(role))
    old = next(
        e["membership"] for e in reversed(group.events(role)) if e.get("membership")
    )
    prior_pid = next(e["pid"] for e in reversed(group.events(role)) if "pid" in e)
    nonce = uuid.uuid4().hex
    proxy.block_client = True
    packet_offset = len(proxy.blocked_client_packets)
    try:
        group.command(role, "room", operation="export", match=match, nonce=nonce)
        group.wait_for(
            lambda: any(
                event.get("type") == "authority-delivery"
                and event.get("nonce") == nonce
                and response_matches(event, "exported")
                and event.get("export_bytes", 0) > 0
                for event in group.events("server")
            ),
            "authority accepted this exact history export before interruption",
            timeout=30,
        )
        accepted_at = time.monotonic()
        packet_offset = len(proxy.blocked_client_packets)
        group.wait_for(
            lambda: len(proxy.blocked_client_packets) > packet_offset,
            "actual downstream bytes withheld during history request",
            timeout=30,
        )
        partial = proxy.blocked_client_packets[packet_offset]
        check(
            not any(
                event.get("type") == "delivery" and event.get("nonce") == nonce
                for event in group.events(role)[before:]
            ),
            "viewer has no complete correlated history response before forced death",
        )
        check(
            group.events(role)[-1]["pid"] == prior_pid,
            "in-flight transfer belongs to the actual soon-to-be-killed viewer",
        )
        # Restart force-kills the heartbeat-reported engine, including on Windows.
        # It does not call the room leave operation before process death.
        fresh = len(group.events(role))
        reconnect = restart_authenticator(group, role, role, package)
        group.restart(
            role,
            4,
            force=True,
            after_shutdown=lambda: setattr(proxy, "block_client", False),
            reconnect=reconnect,
        )
        check(
            response_matches(room("server", "unlock"), "accepted"),
            "organizer remains usable through interrupted viewer transfer",
        )
        other = room(roles[3], "seek", match=match, number=cursor)
        check(
            other["frame"] == cursor,
            "healthy viewer receives the same retained frame after peer transfer loss",
        )
        group.wait_for(
            lambda: any(
                e.get("type") == "delivery"
                and e.get("membership")
                and e["membership"] != old
                for e in group.events(role)[fresh:]
            ),
            "fresh identity restores acknowledged viewer history",
            timeout=30,
        )
        restored = room(role, "pause")
        check(
            restored["match"] == match
            and restored["frame"] == cursor
            and restored["mode"] == "paused",
            "interrupted transfer recovery restores exact selected match and paused cursor",
        )
        check(
            response_matches(room(role, "leave", membership=old, _observe_before=True), "stale membership"),
            "late departure cannot remove recovered viewer",
        )
        offset = len(group.events(role))
        shown = room(role, "seek", match=match, number=cursor)
        group.wait_for(
            lambda: any(
                e.get("type") == "presentation" and e.get("nonce") == shown["nonce"]
                for e in group.events(role)[offset:]
            ),
            "fresh viewer reconstructs complete frame after partial transfer loss",
            timeout=30,
        )
        state = next(
            e
            for e in reversed(group.events(role))
            if e.get("type") == "presentation" and e.get("nonce") == shown["nonce"]
        )
        check(
            (state["x"], state["health"]) == tuple(ledger[cursor]),
            "fresh native reconstruction equals independent authority ledger",
        )
        check(
            state["pid"] != prior_pid and not state.get("integrity_failed"),
            "old partial bytes neither poison nor replace fresh process playback",
        )
        retried = room(role, "export", match=match)
        authority = room("server", "export", match=match)
        check(
            response_matches(retried, "exported")
            and retried.get("replay_valid")
            and authority.get("replay_valid")
            and retried.get("replay_semantics") == authority.get("replay_semantics"),
            "retry obtains complete semantic history after interrupted transfer",
        )
        (group.directory / "interrupted-history-transfer.json").write_text(
            json.dumps(
                dict(
                    passed=True,
                    old_pid=prior_pid,
                    new_pid=state["pid"],
                    withheld_bytes=partial["bytes"],
                    withheld_packets=len(proxy.blocked_client_packets) - packet_offset,
                    frame=cursor,
                    match=match,
                    request_nonce=nonce,
                    authority_acceptance_observed_at=accepted_at,
                    mode="actual downstream history traffic withheld then forced viewer process death",
                    observed_at=partial["received"],
                ),
                indent=2,
            )
        )
    finally:
        proxy.block_client = False

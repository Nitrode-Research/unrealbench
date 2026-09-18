"""Response checks based on public effects, independent of diagnostic vocabulary."""


def annotate_response(reply, before, operation, arguments):
    return dict(reply, _before=before, _operation=operation, _arguments=dict(arguments))


def prior_delivery(group, role):
    return next((dict(e) for e in reversed(group.events(role))
                 if e.get("type") == "delivery"), {})


def response_matches(reply, expectation):
    # expectation names describe test intent; they are never compared to Status.
    if not isinstance(reply, dict):
        return False
    if expectation not in ("accepted", "exported", "duplicate") and not str(reply.get("status", "")).strip():
        return False
    if expectation == "exported":
        return bool(reply.get("replay") or reply.get("export_bytes"))
    if expectation == "pending":
        return not reply.get("replay") and not reply.get("export_bytes")
    before = reply.get("_before", {})
    operation = reply.get("_operation")
    args = reply.get("_arguments", {})
    if expectation == "duplicate":
        # Batched callers supply the first correlated receipt as the baseline.
        return (not before or reply.get("ack") == before.get("ack"))
    if expectation != "accepted":
        # A rejection cannot alter the caller's selected timeline or control state.
        fields = ("match", "frame", "mode", "assignment", "offer", "member_role",
                  "membership", "locked", "paused")
        return all(reply.get(key) == before.get(key) for key in fields if key in before)
    if operation in ("lock", "unlock"):
        return reply.get("locked") == (operation == "lock")
    if operation in ("combat-pause", "combat-resume"):
        return reply.get("paused") == (operation == "combat-pause")
    if operation == "seek":
        return (reply.get("match") == args.get("match")
                and reply.get("frame") == args.get("number"))
    if operation == "select-match":
        return reply.get("match") == args.get("match")
    if operation in ("pause", "resume", "live"):
        return reply.get("mode") == {"pause": "paused", "resume": "playing", "live": "live"}[operation]
    if operation == "accept":
        return bool(reply.get("assignment")) and reply.get("member_role") == "fighter"
    if operation == "decline":
        return not reply.get("offer") and reply.get("member_role") != "fighter"
    if operation == "start":
        # Start changes the active fight, not an organizer's historical selection.
        # Scenarios obtain the fresh identity from the actual current fighters.
        return reply.get("active_match") is True
    # Ready/queue/input have no explicit success flag in the public payload.
    # Their effects are checked by the following start/offer/confirmed-ledger steps.
    return reply.get("ack", 0) > 0


def await_successful_start(group, role, reply, room, timeout=30):
    """Allow preparation after a start request; success remains a public effect."""
    if reply.get("active_match") is True:
        return reply
    observed = reply

    def active():
        nonlocal observed
        # Each probe is correlated by room(); old events and unsolicited
        # delivery frequency do not determine whether preparation completed.
        observed = room(role, "observe")
        return observed.get("active_match") is True

    group.wait_for(active, "requested start establishes active initialized match", timeout=timeout)
    return annotate_response(observed, reply.get("_before", {}), "start", {})


def seek_then_pause(room, check, role, match, frame, edge=None):
    """Check the seek receipt, then establish an explicitly paused baseline.

    Transport can permit playback ticks between the two commands. The pause
    receipt may therefore be later than the requested destination.
    """
    sought = room(role, "seek", match=match, number=frame)
    check(response_matches(sought, "accepted")
          and sought.get("match") == match and sought.get("frame") == frame,
          "seek receipt identifies the exact requested destination")
    upper = sought["edge"] if edge is None else edge
    paused = room(role, "pause")
    check(response_matches(paused, "accepted") and paused.get("match") == match
          and frame <= paused.get("frame", -1) <= upper
          and (sought.get("mode") != "paused" or paused.get("frame") == frame),
          "explicit pause retains the timeline within its released forward range")
    return sought, paused


def restart_authenticator(group, role, identity, package):
    """Retry completed refusals; readiness requires a fresh process membership."""
    old_pid = next(e["pid"] for e in reversed(group.events(role)) if "pid" in e)
    pending = None

    def authenticate():
        nonlocal pending
        if pending is None:
            group.command(role, "content", package=package)
        else:
            responses = [e for e in group.events(role)[pending:]
                         if e.get("type") == "delivery" and e.get("pid")
                         and e["pid"] != old_pid]
            if any(e.get("membership") for e in responses):
                return True
            if not responses:
                return False  # Keep at most one authentication request pending.
        pending = len(group.events(role))
        group.command(role, "authenticate", identity=identity)
        return False

    return authenticate

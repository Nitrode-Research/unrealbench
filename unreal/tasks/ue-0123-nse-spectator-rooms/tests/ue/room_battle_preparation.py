"""Prepare verifier-owned native actors without starting or advancing a room match."""


def prepare_battle(group, roles, *, fighters, timeout=60):
    def latest(role):
        return next((event for event in reversed(group.events(role))
                     if "completed_map_loads" in event), {})

    before_loads = {role: latest(role).get("completed_map_loads", 0)
                    for role in ("server", *roles)}
    reply = group.command("server", "prepare-battle", fighters=list(fighters))
    serial = reply["prepared_battle_serial"]
    seed = reply["configuration_seed"]
    prepared = {}

    def ready():
        nonlocal prepared
        prepared = latest("server")
        return (prepared.get("prepared_battle_serial") == serial
                and prepared.get("prepared_battle_ready") is True
                and prepared.get("configuration_seed") == seed)

    group.wait_for(ready, "configured fixture battle prepared before room start", timeout=timeout)
    expected_map = prepared["world_map"]
    traveled = prepared.get("preparation_travel") is True

    def channels_ready():
        for role in roles:
            state = latest(role)
            if (not state.get("owner_channel_ready") or not state.get("current_membership")
                    or state.get("world_map") != expected_map):
                return False
            if traveled and state.get("completed_map_loads", 0) <= before_loads[role]:
                return False
        return True

    # The ordinary travel path recreates the participant channels. Readiness and
    # start still run through their original room commands after this fixture wait.
    group.wait_for(channels_ready, "participant channels ready after fixture preparation", timeout=timeout)
    # A fresh fixture snapshot separates association from organizer start.
    inactive = group.command("server", "snapshot")
    assert inactive.get("active_match") is False, "preparation must leave public match inactive"
    return prepared

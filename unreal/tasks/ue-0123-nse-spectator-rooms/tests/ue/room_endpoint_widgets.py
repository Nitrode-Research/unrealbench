from room_ui_review import evaluate as evaluate_display
import os
from room_response_contract import response_matches
"""Actual host/connect/authentication widget workflow in two Unreal processes."""

import argparse, json, time, uuid
from pathlib import Path
from room_network_workers import WorkerGroup, PacketProxy


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
    a = p.parse_args()
    group = WorkerGroup(
        a.editor, a.project, a.directory, editor_args=a.editor_args.split()
    )
    assertions = 0
    began = time.monotonic()
    display_observations = []

    def check(value, label):
        nonlocal assertions
        assert value, label
        assertions += 1

    def widget_input_ready(role):
        # Native loading state is sampled by the same worker that dispatches input.
        # The dispatcher also keeps a command queued if loading begins after this
        # observation; it never retries an action that has already been attempted.
        events = group.events(role)
        return bool(events) and events[-1].get("widget_input_ready") is True

    def widget(role, action, **fields):
        return group.command(role, "widget", action=action, fields=fields)

    try:
        group._launch(
            "server",
            "/Engine/Maps/Entry?game=/Script/NightSkyEngine.RoomWorkerGameMode",
            listen=True,
        )
        group.wait_for(
            lambda: any(e.get("net_mode") == 0 for e in group.events("server"))
            and widget_input_ready("server"),
            "standalone host world",
        )
        widget(
            "server",
            "create",
            Identity="host",
            Secret="secret",
            Slot="widget-" + uuid.uuid4().hex,
            Delay="120",
        )
        check(
            any(
                e.get("action") == "create" and bool(e.get("image"))
                for e in group.events("server")
            ),
            "create widget reports persistent room creation",
        )
        display_observations.append(dict(field="room_created", expected=True,
            image=next(e["image"] for e in reversed(group.events("server")) if e.get("action") == "create")))
        widget("server", "authenticate")
        widget("server", "host")
        group.wait_for(
            lambda: any(e.get("net_mode") == 2 for e in group.events("server")),
            "host button starts actual listen driver",
            timeout=30,
        )
        check(
            any(
                e.get("action") == "host" and bool(e.get("image"))
                for e in group.events("server")
            ),
            "host widget confirms endpoint",
        )
        display_observations.append(dict(field="endpoint_listening", expected=True,
            image=next(e["image"] for e in reversed(group.events("server")) if e.get("action") == "host")))
        # Blind pixel review reads each state; reflected text is only diagnostic.
        proxy = PacketProxy(group.port, a.directory, "client0")
        group.proxies.append(proxy)
        group._launch(
            "client0",
            "/Engine/Maps/Entry?game=/Script/NightSkyEngine.RoomWorkerGameMode",
        )
        group.wait_for(
            lambda: any(e.get("net_mode") == 0 for e in group.events("client0"))
            and widget_input_ready("client0"),
            "client begins in separate standalone world",
        )
        widget(
            "client0",
            "connect",
            Identity="viewer",
            Secret="secret",
            Address=f"127.0.0.1:{proxy.port}",
        )
        group.wait_for(
            lambda: any(
                e.get("type") == "delivery"
                and e.get("member_role") == "spectator"
                for e in group.events("client0")
            ) and widget_input_ready("client0"),
            "connect widget performs travel and automatic login",
            timeout=120,
        )
        joined = next(
            e
            for e in reversed(group.events("client0"))
            if e.get("member_role") == "spectator"
        )
        check(bool(joined["room"]), "joined real persistent room after endpoint travel")
        check(
            any(
                e.get("server_connection") and e.get("net_mode") == 3
                for e in group.events("client0")
            ),
            "client has real server connection",
        )
        widget("client0", "queue")
        group.wait_for(
            lambda: any(
                e.get("type") == "delivery" and response_matches(e, "accepted")
                for e in group.events("client0")
            ),
            "post-travel widget owner RPC",
            timeout=30,
        )
        widget("server", "offer", Value="viewer", Frame="0")
        group.wait_for(
            lambda: any(e.get("type") == "delivery" and e.get("offer")
                        for e in group.events("client0")),
            "queued viewer receives organizer offer through post-travel widgets", timeout=30)
        check(
            len(
                {
                    next(e["pid"] for e in group.events(r) if "pid" in e)
                    for r in ("server", "client0")
                }
            )
            == 2,
            "two distinct engine processes",
        )
        observations_path = a.directory / "display-observations.json"
        observations_path.write_text(json.dumps(display_observations, indent=2))
        check(evaluate_display(display_observations, a.directory / "display-review",
                Path(os.environ.get("OT_ROOM_UI_REVIEWS", str(a.directory / "blind-reviews")))),
              "blind actual display meanings agree with creation and listen state")
        (a.directory / "result.json").write_text(
            json.dumps(
                dict(
                    passed=True,
                    assertions=assertions,
                    elapsed=time.monotonic() - began,
                    scope="actual native host/connect widgets and automatic membership travel",
                ),
                indent=2,
            )
        )
    finally:
        group.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

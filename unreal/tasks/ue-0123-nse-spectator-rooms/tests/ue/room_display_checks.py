"""Capture real panel text; expected state stays out of blind-reader requests."""
import json
import os
from pathlib import Path
from room_ui_review import evaluate


def capture_room_display(group, role, **expected):
    offset = len(group.events(role))
    group.command(role, "capture-room-display")
    captured = next(event for event in group.events(role)[offset:]
                    if event.get("type") == "room-display")
    rows = getattr(group, "room_display_observations", [])
    for field, value in expected.items():
        rows.append(dict(field=field, expected=value, image=captured["image"]))
    group.room_display_observations = rows
    return captured


def evaluate_room_displays(group, check):
    rows = getattr(group, "room_display_observations", [])
    if not rows:
        return
    path = group.directory / "status-display-observations.json"
    path.write_text(json.dumps(rows, indent=2))
    reviews = Path(os.environ.get("OT_ROOM_UI_REVIEWS", str(group.directory / "blind-reviews")))
    check(evaluate(rows, group.directory / "status-display-review", reviews),
          "independent reading of actual room status agrees with scenario state")

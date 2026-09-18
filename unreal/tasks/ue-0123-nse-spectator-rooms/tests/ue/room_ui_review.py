"""Evaluate displayed room text; unknown representations use a blind local reviewer."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import time

FIELDS = {
    "organizer_role": "Does the display identify the local user's room-organizer authority or role?",
    "selection_locked": "Does the display say room selection is locked? False means it explicitly says selection is unlocked/editable.",
    "replay_listing": "Does the display list at least one available saved replay? False means it explicitly reports an empty replay list.",
    "replay_playing": "Does the display report that saved replay playback is running?",
    "error_visible": "Does the display communicate a failed or rejected user request, with a meaningful diagnostic?",
    "export_pending": "Does the display communicate that a complete replay export is unavailable until match completion and delayed release? A disabled export control with clear pending context is valid.",
    "replay_request_unavailable": "Does the display communicate that the requested replay cannot currently be played, through a disabled play control or a meaningful rejection diagnostic? Read the current control or result, not merely an action label.",
    "secret_masked": "Does the identifiable visible secret-entry control conceal its contents rather than expose readable entered contents? Blank echo suppression, a constant placeholder, or concealment glyphs are valid. Readable field labels or placeholder text alone are not secret disclosure. Do not require the image to establish whether a nonempty value was entered or retained. Return false if entered contents are readable; return unreadable if the control cannot be located or its visual presentation cannot be assessed.",
    "room_created": "Does the display report successful creation of a persistent room?",
    "endpoint_listening": "Does the display report an active host/listen endpoint?",
    "room_identity": "Read the current room identity shown as status, excluding editable request fields. Return its displayed identifying label or value. Friendly names and abbreviations are valid; an absent or unavailable identity is unreadable.",
    "match_identity": "Read the selected match identity shown as status, excluding editable request fields. Return its displayed identifying label or value. Friendly names and abbreviations are valid; an absent or unavailable identity is unreadable.",
    "assignment_identity": "Read the local fighter seat-assignment identity shown as status. Return its displayed identifying label or value. Friendly names and abbreviations are valid; an absent or unavailable identity is unreadable.",
    "member_role": "Read the local user's current role. Return organizer, fighter, or spectator. An organizer can also occupy a fighter seat; return organizer in that case.",
    "playback_mode": "Read the current spectator playback mode. Return paused, playing, or live. Read state, not action button labels.",
    "cursor": "Read the current spectator position, excluding editable request fields. Return the equivalent integer frame count at 60 Hz. Unambiguous timecode or elapsed-time units are valid; convert them to frames.",
    "released_edge": "Read the highest currently released seekable position, excluding editable request fields. Return its equivalent integer frame count at 60 Hz. Unambiguous timecode or elapsed-time units are valid.",
    "history_start": "Read the lowest retained seekable position, excluding editable request fields. Return its equivalent integer frame count at 60 Hz. Unambiguous timecode or elapsed-time units are valid.",
    "buffering": "Does the display report that selected gameplay is buffering or waiting for its delay? False means it clearly reports released gameplay is available.",
    "combat_paused": "Does the display report that authority combat is paused? False means it clearly reports combat is running or resumed. Playback pause is a different state.",
    "integrity_error": "Does the display report a playback-history integrity failure? False means it explicitly reports history integrity is healthy or verified.",
    "recovery_pending": "Does the display report recovery or reauthentication is still required/in progress? False means it explicitly reports recovery is complete or the connection is ready with no recovery pending.",
}
VALUE_TYPES = {field: "string" for field in
               ("room_identity", "match_identity", "assignment_identity", "member_role", "playback_mode")}
VALUE_TYPES.update({field: "integer" for field in ("cursor", "released_edge", "history_start")})


IDENTITY_FIELDS = {"room_identity", "match_identity", "assignment_identity"}


def observations_match(rows, observations):
    identities = {field: {} for field in IDENTITY_FIELDS}
    for row, entry in zip(rows, observations):
        actual = entry.get("actual")
        if entry.get("readable") is not True or not valid_observed_value(row["field"], actual):
            return False
        if row["field"] in IDENTITY_FIELDS:
            # Formatting may change for the same identity, including abbreviated
            # and full labels. A label must still distinguish different identities.
            mapping = identities[row["field"]]
            expected = row["expected"]
            if any(identity != expected and actual in labels for identity, labels in mapping.items()):
                return False
            mapping.setdefault(expected, set()).add(actual)
        elif type(actual) is not type(row["expected"]) or actual != row["expected"]:
            return False
    return len(rows) == len(observations) and bool(rows)


def valid_observed_value(field, value):
    kind = VALUE_TYPES.get(field, "boolean")
    if kind == "boolean":
        return type(value) is bool
    if kind == "integer":
        return type(value) is int
    if not isinstance(value, str) or not value.strip():
        return False
    if field == "member_role":
        return value in ("organizer", "fighter", "spectator")
    if field == "playback_mode":
        return value in ("paused", "playing", "live")
    return True



def automatic_observation(field, text):
    """Read only clear textual meanings. None delegates an unfamiliar representation."""
    t = " ".join(text.casefold().split())
    if field == "selection_locked":
        if re.search(r'\b(unlocked|selection (?:is )?(?:open|editable)|not locked|locked\s*[:=]?\s*(?:false|0|off|no))\b', t):
            return False
        if re.search(r'\blocked\b', t):
            return True
    if field == "organizer_role":
        if re.search(r'\bnot (?:the )?organizer\b', t):
            return False
        if re.search(r'\b(?:role\s*[:=]\s*(?:organizer|host)|organizer)\b', t):
            return True
        if re.search(r'\b(?:role\s*[:=]\s*spectator|spectating)\b', t):
            return False
    if field == "replay_listing":
        if re.search(r'\b(?:no saved replays|no replays|replays?\s*[:=]\s*(?:none|empty|0))\b', t):
            return False
        if re.search(r'\b(?:saved replays?|replay list)\s*:\s*[\[\(]?[0-9a-f]{8}-[0-9a-f-]{20,}', t):
            return True
    if field == "replay_playing":
        if re.search(r'\b(?:replay stopped|replay paused|not playing)\b', t):
            return False
        if re.search(r'\b(?:playing|replaying|replay (?:is )?(?:running|started))\b', t):
            return True
    if field == "error_visible" and re.search(r'\b(?:no errors?|no failure|nothing failed)\b', t):
        return False
    if field == "error_visible" and re.search(r'\b(?:error|invalid|rejected|denied|failed|unavailable|not found)\b', t):
        return True
    if field == "room_created" and re.search(r'\b(?:not created|creation failed|could not create)\b', t):
        return False
    if field == "room_created" and re.search(r'\b(?:room created|created room|created)\b', t):
        return True
    if field == "endpoint_listening" and re.search(r'\b(?:not listening|host inactive|listen failed)\b', t):
        return False
    if field == "endpoint_listening" and re.search(r'\b(?:listening|listen server|host active)\b', t):
        return True
    return None


def request_fingerprint(request):
    # Paths may change when the same immutable image moves to the reader's host.
    # Every other reviewer-visible field, including the rubric and value type,
    # participates in the binding. Unexpected additions invalidate the request.
    binding = {key: value for key, value in request.items()
               if key not in ("image_path", "request_sha256")}
    return hashlib.sha256(json.dumps(binding, sort_keys=True).encode()).hexdigest()


def validate_request(request):
    current = packet(request["field"], request.get("text", ""), request.get("image_path"))
    if (request_fingerprint(request) != request.get("request_sha256")
            or current["request_sha256"] != request["request_sha256"]):
        raise ValueError("Capture, rubric, schema or value type changed")
    return current


def packet(field, text="", image_path=None):
    if field not in FIELDS:
        raise ValueError("Unknown display field")
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    result = dict(schema=2, text=text, text_sha256=digest, field=field,
                  definition=FIELDS[field], value_type=VALUE_TYPES.get(field, "boolean"),
                  instruction="Read only the actual captured display. Return the observed value with the requested JSON type. "
                  "No expected value or triggering action is provided. Report readable=false if the requested meaning is absent or unclear. "
                  "Any wording, language, friendly identity label, abbreviation or layout is allowed. Do not consult gameplay/state logs.")
    if image_path is not None:
        path = Path(image_path).resolve()
        data = path.read_bytes()
        if (len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR"
                or int.from_bytes(data[16:20], "big") <= 0 or int.from_bytes(data[20:24], "big") <= 0):
            raise ValueError("UI image capture must be a nonempty PNG")
        result["image_path"] = str(path)
        result["image_sha256"] = hashlib.sha256(data).hexdigest()
        # Image rows carry only the pixels. Reflected text is not evidence that
        # a custom-painted control shows the same thing.
        result.pop("text")
        result.pop("text_sha256")
    result["request_sha256"] = request_fingerprint(result)
    return result


def publish_request(field, text, image_path, reviews):
    request = packet(field, text, image_path)
    if image_path is None:
        return request
    data = Path(image_path).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != request["image_sha256"]:
        raise ValueError("Capture changed while publishing blind evidence")
    reviews.mkdir(parents=True, exist_ok=True)
    destination = reviews / (digest + ".png")
    try:
        with destination.open("xb") as stream:
            stream.write(data)
    except FileExistsError:
        if destination.read_bytes() != data:
            raise ValueError("Trusted hash-named image contains different bytes")
    return packet(field, image_path=destination)


def evaluate(rows, directory, reviews, wait_seconds=90):
    if not rows:
        raise ValueError("Display verification requires nonempty captured evidence")
    directory.mkdir(parents=True, exist_ok=True)
    reviews.mkdir(parents=True, exist_ok=True)
    pending, observations = [], []
    for index, row in enumerate(rows):
        request = publish_request(row["field"], row.get("text", ""), row.get("image"), reviews)
        actual = None if "image" in row else automatic_observation(row["field"], row["text"])
        entry = dict(index=index, **request)
        if actual is None:
            key = request["request_sha256"] + "." + request["field"]
            (reviews / (key + ".request.json")).write_text(json.dumps(request, indent=2))
            pending.append((row, entry, reviews / (key + ".observation.json")))
        else:
            entry.update(actual=actual, readable=True, evidence="Automatic reading of explicit displayed wording")
        observations.append(entry)
    deadline = time.monotonic() + wait_seconds
    while pending and time.monotonic() < deadline:
        pending = [(row, entry, path) for row, entry, path in pending
                   if not load_observation(entry, path)]
        if pending:
            time.sleep(min(0.25, max(0, deadline - time.monotonic())))
    # One final reload lets offline review reuse this same immutable capture.
    for row, entry, path in pending:
        load_observation(entry, path)
    passed = observations_match(rows, observations)
    result = dict(passed=passed, assertions=len(rows), observations=observations,
                  reason="Displayed meanings must match scenario state; missing/unreadable review never passes")
    (directory / "result.json").write_text(json.dumps(result, indent=2))
    return passed


def load_observation(entry, path):
    if not path.exists():
        return False
    response = json.loads(path.read_text())
    if response.get("request_sha256") != entry["request_sha256"] or response.get("field") != entry["field"]:
        raise ValueError("Review does not bind this captured display and field")
    if "image_path" in entry:
        current = hashlib.sha256(Path(entry["image_path"]).read_bytes()).hexdigest()
        if current != entry["image_sha256"]:
            raise ValueError("Image changed after the blind request was created")
    if not isinstance(response.get("evidence"), str) or not response["evidence"].strip():
        raise ValueError("Review needs independently observed evidence")
    if response.get("readable") is True and not valid_observed_value(entry["field"], response.get("actual")):
        raise ValueError("Readable review needs a value of the field's declared type")
    entry.update(response)
    return True


def main():
    p = argparse.ArgumentParser()
    p.add_argument("project", type=Path)
    p.add_argument("directory", type=Path)
    p.add_argument("--observations", type=Path, required=True)
    p.add_argument("--editor")
    p.add_argument("--editor-args", default="")
    p.add_argument("--reviews", type=Path)
    p.add_argument("--wait-seconds", type=float, default=90)
    a = p.parse_args()
    if not 0 <= a.wait_seconds <= 90:
        p.error("wait-seconds must be within 0..90")
    reviews = a.reviews or Path(os.environ.get("OT_ROOM_UI_REVIEWS", str(a.directory / "blind-reviews")))
    rows = json.loads(a.observations.read_text(encoding="utf-8-sig"))
    return 0 if evaluate(rows, a.directory, reviews, a.wait_seconds) else 1


if __name__ == "__main__":
    raise SystemExit(main())

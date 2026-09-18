"""Blind reading of immutable room presentation images, independent of framing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import time


FIELDS = {
    "same_stage": "Do the two image sequences visibly depict the same stage setting and its distinctive authored scenery, without scenery carried over from a different stage? Permit different camera pose, crop, zoom, resolution, lighting and rendering methods. Compare recognizable scenery and its world relationships, not pixel coordinates. A visibly different stage or extra scenery from another stage is false. Compare only world regions visible in both images. Scenery naturally outside a different camera crop does not count as missing; no particular off-camera landmark must be shown. A visibly open or empty stage region can match the corresponding region in the other image. If the visible gameplay setting itself cannot be read or compared, report unreadable.",
    "distinct_stages": "Do the two image sequences visibly depict different stage scenery? A camera, fighter, effect, lighting or UI change alone is insufficient. Identify the distinctive visible scenery that differs. If the scenery cannot be distinguished, report unreadable.",
    "visible_gameplay": "Do the images visibly depict readable fighters and their gameplay, including any visible projectiles? Blank images, scenery alone, or diagnostic text alone are false. If the relevant gameplay is too small or obscured to read, report unreadable.",
    "visible_effects": "Does this sequence visibly contain a gameplay effect, such as a projectile trail, attack flash, or impact? Infer only from visible images. Merely having a fighter or scenery is false.",
    "effect_progression": "Does this chronological sequence show at least two observably different phases of a gameplay effect, such as its appearance, growth, travel, decay, or disappearance? Camera movement, unrelated UI changes, or fighter movement alone do not establish this. A frozen effect is false. If phases cannot be read, report unreadable.",
    "same_gameplay": "At every corresponding sample index, do the two sequences depict equivalent gameplay: the same fighters, poses/actions, relative positions and facing, active projectiles, and relevant stage setting? Permit different spectator cameras, zoom, crop, resolution, lighting, and rendering methods when these facts remain readable. Compare world relationships, not screen coordinates or exact colors. A different gameplay state is false; hidden or ambiguous relevant gameplay is unreadable.",
    "same_effect_phase": "At every corresponding sample index, do the two sequences depict equivalent visible gameplay effects at equivalent phases, including which effect events are active, their location relative to fighters/projectiles, progression and remaining lifetime as observable across the sequence? Permit alternative rendering methods, camera framing, resolution and cosmetic appearance when effect identity and phase are readable. Missing, extra, restarted, or frozen effects are false. Exact assets, particle counts and pixel identity are not required. If the evidence cannot distinguish phases, report unreadable.",
    "stable_presentation": "Across this chronological sequence, is the visible gameplay and effect phase preserved without an effect restarting, being newly duplicated, disappearing prematurely or replaying? Ignore unrelated UI motion, minor rendering variation and camera changes that preserve readable gameplay. Compare visible effect events, not implementation particle counts. If the effect or its continuity cannot be read, report unreadable.",
}


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def bitmap_dimensions(data):
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("Missing BMP image evidence")
    header_size, width, height = struct.unpack_from("<Iii", data, 14)
    planes, bits = struct.unpack_from("<HH", data, 26)
    compression = struct.unpack_from("<I", data, 30)[0]
    offset = struct.unpack_from("<I", data, 10)[0]
    if header_size < 40 or width <= 0 or height == 0 or planes != 1 or bits not in (24, 32) or compression not in (0, 3):
        raise ValueError("Unsupported or invalid captured BMP")
    size = ((width * bits + 31) // 32) * 4 * abs(height)
    if offset < 14 + header_size or len(data) < offset + size:
        raise ValueError("Truncated captured BMP")
    return width, abs(height)


def packet(field, sequences, reviews):
    """Only evidence, neutral order and a definition reach the reviewer."""
    if field not in FIELDS:
        raise ValueError("Unknown rendered field")
    if len(sequences) != (2 if field.startswith("same_") or field == "distinct_stages" else 1):
        raise ValueError("Wrong number of image sequences")
    if any(not sequence for sequence in sequences):
        raise ValueError("Rendered review needs nonempty image evidence")
    if len(sequences) == 2 and len(sequences[0]) != len(sequences[1]):
        raise ValueError("Corresponding sequences have different sample counts")
    if field in ("effect_progression", "stable_presentation", "same_effect_phase") and any(len(s) < 2 for s in sequences):
        raise ValueError("Phase review needs multiple chronological samples")
    evidence = []
    for sequence in sequences:
        images = []
        for path in sequence:
            data = Path(path).read_bytes()
            width, height = bitmap_dimensions(data)
            digest = hashlib.sha256(data).hexdigest()
            name = digest + ".bmp"
            target = reviews / name
            if target.exists() and target.read_bytes() != data:
                raise ValueError("Immutable review evidence changed")
            if not target.exists():
                target.write_bytes(data)
            images.append(dict(image=name, sha256=digest, width=width, height=height))
        evidence.append(images)
    # Pair ordering carries no reference/candidate or action label.
    evidence.sort(key=lambda sequence: hashlib.sha256(encoded(sequence)).hexdigest())
    request = dict(schema=1, field=field, definition=FIELDS[field], sequences=evidence,
                   instruction="Open and inspect every supplied image. Images within each sequence are chronological; corresponding indices across sequences represent corresponding moments. Read the requested visible meaning only. No expected answer, triggering action, gameplay frame number or reference/candidate label is provided. Do not open manifests, implementations, logs or result comparisons. Give concrete visible evidence, identifying sample indices. Report unreadable when the images cannot establish the requested meaning.")
    request["request_sha256"] = hashlib.sha256(encoded(request)).hexdigest()
    return request


def validate_request(request, reviews):
    plain = {key: value for key, value in request.items() if key != "request_sha256"}
    if hashlib.sha256(encoded(plain)).hexdigest() != request.get("request_sha256"):
        raise ValueError("Changed blind request")
    if request.get("field") not in FIELDS or request.get("definition") != FIELDS[request["field"]]:
        raise ValueError("Changed field definition")
    for sequence in request["sequences"]:
        for item in sequence:
            if item["image"] != item["sha256"] + ".bmp":
                raise ValueError("Unsafe or mismatched image name")
            data = (reviews / item["image"]).read_bytes()
            if hashlib.sha256(data).hexdigest() != item["sha256"] or bitmap_dimensions(data) != (item["width"], item["height"]):
                raise ValueError("Changed image evidence")


def load_observation(request, path, reviews):
    validate_request(request, reviews)
    if not path.exists():
        return None
    response = json.loads(path.read_text())
    if response.get("request_sha256") != request["request_sha256"] or response.get("field") != request["field"]:
        raise ValueError("Review does not bind this evidence and field")
    if type(response.get("readable")) is not bool or (response["readable"] and type(response.get("actual")) is not bool):
        raise ValueError("Review needs readable status and an observed boolean")
    if not isinstance(response.get("evidence"), str) or not response["evidence"].strip():
        raise ValueError("Review needs concrete visible evidence")
    return response


def evaluate(manifest, directory, reviews, wait_seconds=90):
    directory.mkdir(parents=True, exist_ok=True)
    reviews.mkdir(parents=True, exist_ok=True)
    rows = manifest["requirements"]
    if not rows:
        raise ValueError("Rendered verification requires observations")
    requests = []
    for row in rows:
        if type(row["expected"]) is not bool:
            raise ValueError("Invalid private expected value")
        sequences = [manifest["groups"][group] for group in row["groups"]]
        request = packet(row["field"], sequences, reviews)
        request_path = reviews / (request["request_sha256"] + ".request.json")
        request_path.write_text(json.dumps(request, indent=2))
        requests.append(request)
    responses = [None] * len(requests)
    deadline = time.monotonic() + wait_seconds
    while True:
        for index, request in enumerate(requests):
            if responses[index] is None:
                responses[index] = load_observation(request, reviews / (request["request_sha256"] + ".observation.json"), reviews)
        if all(response is not None for response in responses) or time.monotonic() >= deadline:
            break
        time.sleep(min(0.25, max(0, deadline - time.monotonic())))
    passed = all(response is not None and response.get("readable") is True and response.get("actual") is row["expected"]
                 for row, response in zip(rows, responses))
    (directory / "result.json").write_text(json.dumps(dict(passed=passed, assertions=len(rows), observations=responses,
        request_hashes=[r["request_sha256"] for r in requests], reason="Actual visible gameplay and effect phases must match; missing or unreadable evidence fails"), indent=2))
    return passed


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
    reviews = a.reviews or Path(os.environ.get("OT_ROOM_RENDER_REVIEWS", str(a.directory / "blind-reviews")))
    try:
        return 0 if evaluate(json.loads(a.observations.read_text(encoding="utf-8-sig")), a.directory, reviews, a.wait_seconds) else 1
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        a.directory.mkdir(parents=True, exist_ok=True)
        (a.directory / "result.json").write_text(json.dumps(dict(passed=False, assertions=0, reason=str(error))))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

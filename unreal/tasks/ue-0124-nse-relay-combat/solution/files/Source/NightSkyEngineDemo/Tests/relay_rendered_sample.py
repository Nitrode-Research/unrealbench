"""Run a real game viewport and retain painted relay phase and seeded-episode screenshots."""

import argparse
import hashlib
import math
import shutil
import csv
import os
import json
import pathlib
import re
import struct
import subprocess
import sys
import zlib


def pixels(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    offset = 8
    encoded = b""
    while offset < len(data):
        chunk_length = struct.unpack(">I", data[offset : offset + 4])[0]
        kind = data[offset + 4 : offset + 8]
        block = data[offset + 8 : offset + 8 + chunk_length]
        offset += 12 + chunk_length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", block)
        if kind == b"IDAT":
            encoded += block
    assert (
        depth == 8 and color in (2, 6) and interlace == 0
    ), "Expected normal RGB/RGBA viewport PNG"
    channels = 4 if color == 6 else 3
    stride = width * channels
    raw = zlib.decompress(encoded)
    previous = bytearray(stride)
    rows = []
    for row in range(height):
        start = row * (stride + 1)
        kind = raw[start]
        values = bytearray(raw[start + 1 : start + 1 + stride])
        for byte_index in range(stride):
            left = values[byte_index - channels] if byte_index >= channels else 0
            above = previous[byte_index]
            corner = previous[byte_index - channels] if byte_index >= channels else 0
            if kind == 1:
                predictor = left
            elif kind == 2:
                predictor = above
            elif kind == 3:
                predictor = (left + above) // 2
            elif kind == 4:
                estimate = left + above - corner
                distances = [abs(estimate - left), abs(estimate - above), abs(estimate - corner)]
                predictor = [left, above, corner][distances.index(min(distances))]
            else:
                assert kind == 0
                predictor = 0
            values[byte_index] = (values[byte_index] + predictor) & 255
        rows.append(values)
        previous = values
    return width, height, channels, rows


def verify_scene_state(rows, frame, check):
    if frame <= 100:
        check(
            rows[0 if frame < 37 or frame == 51 or frame >= 80 else 2]["main"] == "1",
            f"Painted main handoff at frame {frame}",
        )
        check(
            rows[0]["resource"] == "100" and rows[3]["resource"] == "200",
            "Independent team resource displayed",
        )
        if frame < 54 or frame >= 80:
            check(
                rows[1]["visible"] == ("0" if frame in (37, 48, 100) else "1"),
                "Participant exposure follows actual phase",
            )
        check(
            rows[4]["eligible"] == ("0" if 51 <= frame <= 60 or frame in (83, 90) else "1")
            and rows[5]["eligible"] == ("0" if 51 <= frame <= 60 or frame in (83, 90) else "1"),
            "Reserve eligibility follows the main fighter’s actual busy state",
        )
        check(
            rows[0]["rejection"] == ("Main" if frame == 48 else "None"),
            "Displayed rejection follows the accepted/rejected request",
        )
        if frame == 51:
            check(
                rows[0]["health"] == "1" and rows[1]["health"] == "1",
                "Rendered lethal training encounter begins at authored health",
            )
        if 54 <= frame <= 60:
            check(
                rows[0]["health"] == "0" and rows[1]["health"] == "0" and rows[2]["main"] == "1",
                "Rendered actual contact KO promotes the surviving immutable slot",
            )
        if frame in (83, 90):
            check(
                rows[1]["throw_locked"] == "1" and rows[1]["visible"] == "1",
                "Thrown reserve remains exposed through its actual retained throw reaction",
            )
        if frame == 100:
            check(
                rows[1]["throw_locked"] == "0" and rows[1]["visible"] == "0",
                "Reserve leaves after the actual throw reaction finishes",
            )
    else:
        generated = 39009

        def next_value():
            nonlocal generated
            generated = (generated * 1664525 + 1013904223) & 0xFFFFFFFF
            return generated

        target = 1 + next_value() % 2
        route = 3 - target
        spacing = 600000 + next_value() % 300001
        health = [2000 + next_value() % 1001 for _ in range(6)]
        check(
            [int(r["health"]) for r in rows] == health,
            "Seeded rendered health matches generated encounter configuration",
        )
        check(
            all(int(r["recoverable"]) == 0 for r in rows),
            "Seeded encounter has no invented recovery",
        )
        check(
            int(rows[0]["resource"]) == 100 and int(rows[3]["resource"]) == 200,
            "Seeded relay pays independently",
        )
        check(
            [int(r["main"]) for r in rows[:3]]
            == [int(i == (route if frame == 139 else 0)) for i in range(3)],
            "Seeded rendered handoff follows independent frame37 boundary",
        )
        visible = {route} if frame == 139 else {0, target, route} if frame >= 120 else {0, target}
        check(
            [int(r["visible"]) for r in rows[:3]] == [int(i in visible) for i in range(3)],
            "Seeded rendered exposure follows selected route",
        )
        check(
            [int(r["cooldown"]) for r in rows[:3]] == [120 if frame == 139 else 0] * 3,
            "Seeded cooldown remains attached to participating identities",
        )


def verify_painted_hud(text, rows, check):
    """Associate visible labels with their team/slot, allowing layout and punctuation changes."""
    team_headings = list(re.finditer(r"\b(?:team|side|player)\s*[:#=\-]?\s*([12])\b", text, re.I))
    check({int(match[1]) for match in team_headings} == {1, 2}, "Painted HUD identifies both teams")
    for team in range(2):
        heading_index = next(
            index for index, match in enumerate(team_headings) if int(match[1]) == team + 1
        )
        begin = team_headings[heading_index].end()
        end = (
            team_headings[heading_index + 1].start()
            if heading_index + 1 < len(team_headings)
            else len(text)
        )
        section = text[begin:end]
        group = rows[team * 3 : team * 3 + 3]
        check(
            bool(
                re.search(
                    r"\b(?:relay|resource|gauge)\s*[:=\-]?\s*"
                    + re.escape(group[0]["resource"])
                    + r"\b",
                    section,
                    re.I,
                )
            ),
            "Painted team resource matches observed value",
        )
        rejection = group[0]["rejection"].casefold()
        # The absence of an error needs no literal "None" label.
        if rejection != "none":
            aliases = {
                "main": r"main|active|current",
                "resource": r"resource|gauge|energy|insufficient",
                "cooldown": r"cooldown|recharging|wait",
                "dead": r"dead|ko|defeated",
                "airborne": r"airborne|air|ground",
                "busy": r"busy|action|attack|reaction",
                "wrongphase": r"phase|window|unavailable",
                "missingmove": r"move|unavailable|missing",
                "visible": r"visible|exposed|screen",
            }
            check(
                bool(re.search(r"\b(?:" + aliases.get(rejection, re.escape(rejection)) + r")\b", section, re.I)),
                "Painted rejection explains the observed request outcome",
            )
        identities = sorted(
            (match.start(), row)
            for row in group
            for match in re.finditer(r"\b" + re.escape(row["identity"]) + r"\b", section)
        )
        check(len(identities) == 3, "Painted team contains each selected fighter identity")
        for index, (position, row) in enumerate(identities):
            end = identities[index + 1][0] if index + 1 < len(identities) else len(section)
            # Include the slot prefix before the fighter name, regardless of row order.
            prefix = max(section.rfind("\n", 0, position), section.rfind("===", 0, position))
            record = section[max(0, prefix) : end]
            identity = re.escape(row["identity"])
            slot_match = re.search(
                r"\b(?:slot\s*[:#=\-]?\s*)?(\d+)\s*[:.)\-]?\s*" + identity + r"\b",
                record,
                re.I,
            )
            if slot_match is None:
                slot_match = re.search(
                    r"\b" + identity + r"\s*[,;|:=\-]?\s*slot\s*[:#=\-]?\s*(\d+)\b",
                    record,
                    re.I,
                )
            check(
                slot_match is not None and int(slot_match[1]) == int(row["slot"]),
                "Painted fighter retains its immutable slot",
            )
            role = (
                "ko|dead|defeated"
                if int(row["health"]) == 0
                else "main|active|point" if int(row["main"])
                else "exposed|assist|participant" if int(row["visible"])
                else "reserve|bench|standby"
            )
            check(
                bool(re.search(r"\b(?:" + role + r")\b", record, re.I)),
                "Painted role matches actual combat role",
            )
            for aliases, field in (
                ("hp|health", "health"),
                ("recover|recoverable|recovery", "recoverable"),
                ("cd|cooldown", "cooldown"),
            ):
                check(
                    bool(
                        re.search(
                            r"\b(?:" + aliases + r")\s*[:=\-]?\s*" + row[field] + r"\b",
                            record,
                            re.I,
                        )
                    ),
                    "Painted fighter value matches " + field,
                )
            negative = bool(re.search(
                r"\b(?:not\s+(?:ready|eligible|available)|ineligible|unavailable|unready)\b"
                r"|\b(?:ready|eligible|available)\s*[:=]\s*(?:no|false|0)\b", record, re.I
            ))
            ready = not negative and bool(re.search(r"\b(?:ready|eligible|available)\b", record, re.I))
            check(
                ready == bool(int(row["eligible"])),
                "Painted readiness matches actual route eligibility",
            )



def image_digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_hud_review_packet(directory, expected):
    """Keep actual evaluator values out of the image reviewer's input packet."""
    frames = []
    blind_directory = directory / "hud-review-input"
    blind_directory.mkdir(exist_ok=True)
    for frame in expected:
        name = f"frame-{frame:02}.png"
        shutil.copyfile(directory / name, blind_directory / name)
        frames.append({"frame": frame, "image": name,
                       "sha256": image_digest(directory / name)})
    packet = {
        "schema": "relay-hud-observations-v1", "frames": frames,
        "instructions": (
            "Inspect every image. Record what the HUD visibly communicates, never infer "
            "hidden values from gameplay expectations. Any widget, layout, language, icon, "
            "bar or text presentation is allowed. Use null for absent or unreadable fields. "
            "Return the exact image hashes. Map each team and immutable selection slot. "
            "For numeric text return a number; for a graphical meter return fraction in "
            "[0,1] and resolution_pixels equal to its measured full-scale length. "
            "Record roles as main/exposed/reserve/ko and eligibility as boolean. "
            "Use rejection=none when no rejection is displayed; otherwise report the "
            "semantic reason main/dead/visible/cooldown/resource/airborne/busy/wrongphase/"
            "missingmove. Do not turn an unreadable graphic into an assumed value. "
            "For each frame supply evidence describing where each team and slot is shown."
        ),
        "field_definitions": {
            "team": "1 or 2", "slot": "1, 2 or 3, original selection order",
            "health": "current health, not combined current plus recoverable",
            "recoverable": "recoverable health separately represented",
            "cooldown": "remaining fighter cooldown, full-scale 120 frames",
            "resource": "separate team relay resource, full-scale 200",
            "eligible": "whether this fighter is currently available as a relay target",
            "role": "main, exposed non-main participant, off-screen reserve, or KO",
        },
        "selected_roster": [
            {"slot": 1, "identity": "Vanguard", "maximum_health": 10000},
            {"slot": 2, "identity": "Heavy", "maximum_health": 12000},
            {"slot": 3, "identity": "Light", "maximum_health": 9000}],
        "context": "Two selected teams undergo relay entry, synchronized attacks, routing, "
                   "handoff, a rejected command, lethal contact, training restart and throw reaction.",
        "response_shape": {"frames": [{"frame": "integer", "sha256": "image hash",
            "evidence": "visible positions and encoding explanation, no pass/fail verdict",
            "teams": [{"team": "integer", "resource": "number or graphical meter",
                "rejection": "semantic reason", "slots": [{"slot": "integer",
                    "role": "semantic role", "health": "number or graphical meter",
                    "recoverable": "number or graphical meter", "cooldown": "number or graphical meter",
                    "eligible": "boolean"}]}]}]}}
    (directory / "hud-review-packet.json").write_text(json.dumps(packet, indent=2))
    (blind_directory / "hud-review-packet.json").write_text(json.dumps(packet, indent=2))
    (directory / "hud-evaluator-state.json").write_text(json.dumps(expected, indent=2))


def compare_hud_number(observed, expected, maximum):
    if type(observed) in (int, float):
        return math.isfinite(observed) and observed == expected
    if not isinstance(observed, dict):
        return False
    fraction, resolution = observed.get("fraction"), observed.get("resolution_pixels")
    if type(fraction) not in (int, float) or type(resolution) not in (int, float):
        return False
    if not math.isfinite(fraction) or not math.isfinite(resolution):
        return False
    if not 0 <= fraction <= 1 or resolution < 1:
        return False
    # One measured pixel of quantization is allowed; a meter cannot communicate
    # a nonzero pool by displaying an empty bar or vice versa.
    if expected == 0:
        return fraction == 0
    if fraction == 0:
        return False
    return abs(fraction - expected / maximum) <= 1 / resolution + 1e-9


def verify_hud_review(directory, review_path, check):
    packet = json.loads((directory / "hud-review-packet.json").read_text())
    expected = json.loads((directory / "hud-evaluator-state.json").read_text())
    response = json.loads(pathlib.Path(review_path).read_text())
    observations = response.get("frames", [])
    check(len(observations) == len(packet["frames"]), "Reviewer observed every captured frame")
    check(len({item["frame"] for item in observations}) == len(observations), "No duplicate review frames")
    by_frame = {item["frame"]: item for item in observations}
    for captured in packet["frames"]:
        frame = captured["frame"]
        check(frame in by_frame, "Required captured frame has an observation")
        observed = by_frame[frame]
        check(observed.get("sha256") == captured["sha256"] == image_digest(directory / captured["image"]),
              "Observation is bound to the actual captured image")
        check(isinstance(observed.get("evidence"), str) and bool(observed["evidence"].strip()),
              "Image reviewer described visible evidence")
        teams = observed.get("teams", [])
        check(len(teams) == 2 and {t["team"] for t in teams} == {1, 2}, "Both teams visibly identified")
        for team in teams:
            rows = expected[str(frame)][(team["team"] - 1) * 3:team["team"] * 3]
            check(compare_hud_number(team.get("resource"), int(rows[0]["resource"]), 200), "Visible team resource agrees")
            check(team.get("rejection") == rows[0]["rejection"].lower(), "Visible rejection agrees")
            slots = team.get("slots", [])
            check(len(slots) == 3 and {v["slot"] for v in slots} == {1, 2, 3}, "All immutable slots visibly identified")
            for slot in slots:
                row = rows[slot["slot"] - 1]
                role = "ko" if int(row["health"]) == 0 else "main" if int(row["main"]) else "exposed" if int(row["visible"]) else "reserve"
                check(slot.get("role") == role, "Visible role agrees")
                check(type(slot.get("eligible")) is bool and slot["eligible"] == bool(int(row["eligible"])), "Visible eligibility agrees")
                for field in ("health", "recoverable", "cooldown"):
                    maximum = 120 if field == "cooldown" else (10000, 12000, 9000)[slot["slot"] - 1]
                    check(compare_hud_number(slot.get(field), int(row[field]), maximum), "Visible " + field + " agrees")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("project")
    parser.add_argument("directory")
    parser.add_argument("--editor")
    parser.add_argument("--review-only", action="store_true")
    parser.add_argument("--review-result")
    parser.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    args = parser.parse_args()
    output_directory = pathlib.Path(args.directory).resolve()
    output_directory.mkdir(parents=True, exist_ok=True)
    assertions = 0

    def check(value, message):
        nonlocal assertions
        assertions += 1
        if not value:
            raise AssertionError(message)

    if args.review_only:
        prior = {}
        try:
            prior = json.loads((output_directory / "result.json").read_text())
            check(prior.get("automatic_checks_passed") is True, "Automatic combat/camera checks already passed")
            check(bool(args.review_result), "Trusted independent observations are required")
            verify_hud_review(output_directory, args.review_result, check)
            pending_result = output_directory / "result-review.tmp"
            pending_result.write_text(json.dumps({
                "passed": True, "assertions": assertions + prior["assertions"],
                "automatic_checks_passed": True, "hud_review": str(pathlib.Path(args.review_result).resolve())}, indent=2))
            pending_result.replace(output_directory / "result.json")
            return 0
        except Exception as exc:
            pending_result = output_directory / "result-review.tmp"
            pending_result.write_text(json.dumps({
                "passed": False, "status": "visual_review_failed", "error": str(exc),
                "automatic_checks_passed": prior.get("automatic_checks_passed", False),
                "assertions": assertions + prior.get("assertions", 0)}, indent=2))
            pending_result.replace(output_directory / "result.json")
            print(str(exc), file=sys.stderr)
            return 1
    try:
        check(bool(args.editor), "An Unreal editor executable is required for capture")
        expected_hud = {}
        text_errors = []
        cmd = [
            args.editor,
            args.project,
            "/Engine/Maps/Entry?game=/Script/NightSkyEngine.RelayVisualProofGameMode",
            "-game",
            "-RelaySample",
            f"-RelayVisualProof={output_directory}",
            "-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/NightSkyEngine.RelayFixtureGameInstance",
            "-RenderOffScreen",
            "-AllowSoftwareRendering",
            "-unattended",
            "-nosplash",
            "-NoSound",
            "-ResX=1280",
            "-ResY=720",
            "-windowed",
            f"-Abslog={output_directory}/game.log",
        ]
        # The rendering child inherits the capability flags the parent editor was
        # started with, which come from the task's spec.yaml. Without them a
        # GPU-less host rejects the WARP adapter and the child exits during RHI init.
        inherited = args.editor_args.split()
        # A Windows default must not conflict with a caller-selected Vulkan/D3D12
        # backend or be sent to a Linux rendering child.
        if os.name == "nt" and not any(
            flag.lower() in {"-d3d11", "-d3d12", "-vulkan", "-opengl"} for flag in inherited
        ):
            inherited.append("-d3d11")
        cmd += [flag for flag in inherited if flag not in cmd]
        (output_directory / "command.json").write_text(json.dumps(cmd, indent=2))
        with (output_directory / "stdout.log").open("w") as log:
            run = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)
            try:
                run.wait(timeout=150)
            except subprocess.TimeoutExpired:
                run.terminate()
                try:
                    run.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    run.kill()
                    run.wait()
                raise AssertionError("Rendered game process exceeded its outer deadline")
        check(run.returncode == 0, "Rendered game process failed")
        for frame in (
            0,
            6,
            18,
            19,
            31,
            37,
            48,
            51,
            54,
            60,
            83,
            90,
            100,
            102,
            108,
            120,
            121,
            133,
            139,
        ):
            csvpath = output_directory / f"frame-{frame:02}.csv"
            png = output_directory / f"frame-{frame:02}.png"
            check(
                csvpath.exists() and png.exists(),
                f"Frame {frame} missing screenshot or public observations",
            )
            with csvpath.open() as f:
                rows = list(csv.DictReader(f))
            check(len(rows) == 6, "Six immutable roster rows must be displayed")
            check(
                [r["identity"] for r in rows] == ["Vanguard", "Heavy", "Light"] * 2,
                "Stable selected identities",
            )
            for row in rows:
                if int(row["visible"]):
                    check(
                        int(row["bounds_inside"])
                        and int(row["projected"])
                        and 0 <= float(row["x"]) < int(row["width"])
                        and 0 <= float(row["y"]) < int(row["height"]),
                        f"Exposed fighter outside viewport: {row}",
                    )
            verify_scene_state(rows, frame, check)
            expected_hud[frame] = rows
            # Optional textual fast path. Other visual encodings proceed to a
            # blind image observation review, not an automatic missing-text failure.
            try:
                painted = (output_directory / f"frame-{frame:02}-hud.txt").read_text(encoding="utf-8-sig")
                paint_clock, text = painted.split("\n", 1)
                paint_frame, battle_frame = map(int, paint_clock.split(","))
                def text_check(value, message):
                    if not value:
                        raise AssertionError(message)
                text_check(paint_frame == battle_frame, "HUD paint is current")
                verify_painted_hud(text, rows, text_check)
            except (OSError, ValueError, AssertionError) as exc:
                text_errors.append({"frame": frame, "diagnostic": str(exc)})
            width, height, channels, raster = pixels(png)
            visible_bodies = [
                tuple(
                    float(row[k]) for k in ("body_min_x", "body_max_x", "body_min_y", "body_max_y")
                )
                for row in rows
                if int(row["visible"])
            ]
            for index, (left, right, top, bottom) in enumerate(visible_bodies):
                check(
                    0 <= left < right < width and 0 <= top < bottom < height,
                    "Each exposed sample mesh has actual projected bounds inside viewport",
                )
                inset_x = (right - left) * 0.2
                inset_y = (bottom - top) * 0.2
                lit = 0
                for y in range(int(top + inset_y), int(bottom - inset_y)):
                    for x in range(int(left + inset_x), int(right - inset_x)):
                        if any(
                            i != index and other[0] <= x <= other[1] and other[2] <= y <= other[3]
                            for i, other in enumerate(visible_bodies)
                        ):
                            continue
                        lit += max(raster[y][x * channels : x * channels + 3]) > 100
                check(
                    lit > 30,
                    f"Frame {frame} exposed body {index} has {lit} visible pixels outside other fighter projections",
                )
            bright = sum(
                min(row[x : x + 3]) > 150
                for row in raster
                for x in range(0, width * channels, channels)
            )
            check(bright > 200, "Captured viewport must contain visible bright pixels")
            colors = {
                tuple(row[x : x + 3])
                for row in raster[120:]
                for x in range(0, len(row), channels * 8)
            }
            check(len(colors) > 20, "Battle viewport must contain nonuniform rendered scene pixels")
        if text_errors:
            write_hud_review_packet(output_directory, expected_hud)
            (output_directory / "result.json").write_text(json.dumps({
                "passed": False, "assertions": assertions, "automatic_checks_passed": True,
                "status": "visual_review_pending", "review_packet": "hud-review-packet.json",
                "error": "Independent screenshot observations required for HUD semantics",
                "text_diagnostics": text_errors}, indent=2))
            return 2
        (output_directory / "result.json").write_text(
            json.dumps({"passed": True, "assertions": assertions}, indent=2)
        )
        return 0
    except Exception as exc:
        (output_directory / "result.json").write_text(
            json.dumps({"passed": False, "assertions": assertions, "error": str(exc)}, indent=2)
        )
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

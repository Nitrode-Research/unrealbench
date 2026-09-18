"""Actual GGPO peers through delayed UDP, with confirmed-state and cold-replay checks."""

import argparse, csv, heapq, io, json, os, pathlib, re, select, socket, subprocess, sys, time
from collections import Counter
from modifier_visual_reader import verify_visible


def status(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}


def read_ledger(path):
    # A live native writer may still be appending the final, unconfirmed line.
    # Only complete newline-delimited observations are parsed; callers still
    # require every frame in the confirmed prefix with exact coverage assertions.
    data = path.read_bytes()
    if data and not data.endswith(b"\n"):
        data = data.rsplit(b"\n", 1)[0] + b"\n" if b"\n" in data else b""
    return list(csv.reader(io.StringIO(data.decode("utf-8"))))


def semantic_ledger(row):
    # HUD formatting is checked separately against actual visible text.
    return row[:3] + [
        field for field in row[4:] if not field.startswith(("hud:", "hud-frame:"))
    ]


def gameplay_outcome(row):
    """Compare gameplay only; changed input metadata is a separate observation."""
    return tuple(semantic_ledger(row)[3:])


def changed_input_frames(rows):
    """Identify actual revised nonneutral input pairs, independently of callbacks."""
    pairs_by_frame = {}
    for row in rows:
        if len(row) >= 3:
            pairs_by_frame.setdefault(int(row[0]), set()).add((int(row[1]), int(row[2])))
    return {
        frame for frame, pairs in pairs_by_frame.items()
        if len(pairs) > 1 and any(player_one or player_two for player_one, player_two in pairs)
    }


def visible_durations(text, expected):
    # Parse visible name/value rows, including duration-first rows or separate
    # controls represented by consecutive lines. Punctuation and row order vary.
    known = {
        "Conversion",
        "Drain",
        "Nested grandchild",
        "Nested parent",
        "Nested branch cancellation",
        "Restriction",
        "Shots",
        "SuddenDeath",
        "Held-out damage scale",
        "Held-out nested meter",
    }
    if any(name in text and name not in expected for name in known):
        return False
    # Revision/round labels are metadata, not remaining-frame observations.
    # Removing their numeric payload preserves row/name associations.
    text = re.sub(r"(?i)\b(?:revision|rev|round|v)\s*[:=#]?\s*\d+\b", "", text)
    text = text.replace(" / ", "\n")
    positions = sorted((text.find(name), name) for name in expected)
    if any(position < 0 for position, name in positions):
        return False
    numbers = [
        (m.start(), m.end(), int(m.group()))
        for m in re.finditer(r"[0-9]+", text)
        if not any(start <= m.start() < start + len(name) for start, name in positions)
    ]
    lines = []
    offset = 0
    for line in text.splitlines(keepends=True):
        lines.append((offset, offset + len(line), line))
        offset += len(line)
    candidates = []
    for index, (start, name) in enumerate(positions):
        row = next(
            (
                (left, right)
                for left, right, line in lines
                if left <= start < right
                and sum(other in line for other in expected) == 1
            ),
            None,
        )
        inline = [
            i
            for i, (left, right, value) in enumerate(numbers)
            if row and row[0] <= left < row[1]
        ]
        if inline:
            # An explicit same-line label/value association wins over neighboring rows.
            framed = [i for i in inline if re.match(r"\s*frames?\b", text[numbers[i][1]:], re.I)]
            choices = framed or inline
        else:
            previous = (
                positions[index - 1][0] + len(positions[index - 1][1]) if index else 0
            )
            following = (
                positions[index + 1][0] if index + 1 < len(positions) else len(text)
            )
            before = [
                i
                for i, (left, right, value) in enumerate(numbers)
                if previous <= left and right <= start
            ]
            after = [
                i
                for i, (left, right, value) in enumerate(numbers)
                if start + len(name) <= left and right <= following
            ]
            choices = before[-1:] + after[:1]
        candidates.append([i for i in choices if numbers[i][2] == expected[name]])

    def pair(index, used):
        if index == len(candidates):
            return True
        return any(
            i not in used and pair(index + 1, used | {i}) for i in candidates[index]
        )

    return pair(0, set())


def hud_text(row):
    return next((field[4:] for field in row if field.startswith("hud:")), "")


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("project")
    parser.add_argument("directory")
    parser.add_argument("--editor", required=True)
    parser.add_argument("--mismatch", action="store_true")
    parser.add_argument("--lifecycle", action="store_true")
    parser.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    parser.add_argument("--seed", type=int, default=0)
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    output_directory = pathlib.Path(arguments.directory)
    output_directory.mkdir(parents=True, exist_ok=True)
    slot = "ModifierPeer_" + output_directory.name
    replay_budget = 300 if os.name == "nt" else 90  # Process watchdog only.
    target_frame = 900 if arguments.lifecycle else 400
    editor_args = arguments.editor_args.split()
    packet_trace = (output_directory / "packet-schedule.jsonl").open("w")
    processes = []
    process_names = []
    logs = []
    assertions = 0
    boundary_observations = []
    packets = 0
    delivered = {"client_to_host": 0, "host_to_client": 0}
    pending = []
    client_address = None

    pending_visual_rounds = []

    def live_hud_or_defer(row, expected, round_number):
        if visible_durations(hud_text(row), expected):
            return True
        # Headless peers have no image. Require the corresponding confirmed cold
        # replay frame's rendered observation before this registration can pass.
        pending_visual_rounds.append(round_number)
        return True

    def check(value, message):
        nonlocal assertions
        assertions += 1
        if not value:
            raise AssertionError(message)

    def launch(command, name):
        if "-NullRHI" not in command:
            # Rendering children inherit the capability flags the parent editor
            # was started with, which come from the task's spec.yaml. Without
            # them a GPU-less host rejects the WARP adapter and the child exits
            # during RHI init.
            command = command + [a for a in editor_args if a not in command]
        (output_directory / (name + "-command.json")).write_text(
            json.dumps(command, indent=2)
        )
        stream = (output_directory / (name + "-stdout.log")).open("w")
        logs.append(stream)
        p = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT)
        processes.append(p)
        process_names.append(name)
        return p

    front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    front.bind(("127.0.0.1", 0))
    front.setblocking(False)
    back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    back.bind(("127.0.0.1", 0))
    back.setblocking(False)
    reserve = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    reserve.bind(("127.0.0.1", 0))
    hostport = reserve.getsockname()[1]
    reserve.close()
    common = [
        f"-ModifierEpisodeSeed={arguments.seed}",
        "-game",
        "-NullRHI",
        "-unattended",
        "-nosplash",
        "-NoSound",
        "-nosteam",
        "-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=Null",
        "-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/NightSkyEngine.ModifierWorkerGameInstance",
    ]
    if arguments.lifecycle:
        common += ["-ModifierLifecycle"]
    try:
        for role in ("host", "client"):
            url = (
                "/Engine/Maps/Entry?listen?game=/Script/NightSkyEngine.ModifierPeerGameMode"
                if role == "host"
                else f"127.0.0.1:{front.getsockname()[1]}"
            )
            cmd = (
                [arguments.editor, arguments.project, url]
                + common
                + [
                    f"-ModifierPeerDir={output_directory}",
                    f"-ModifierPeerRole={role}",
                    f"-Abslog={output_directory}/{role}.log",
                ]
            )
            if role == "host":
                cmd += [f"-port={hostport}", f"-ModifierSaveSlot={slot}"]
            if role == "client" and arguments.mismatch:
                cmd += ["-ModifierMismatch"]
            launch(cmd, role)
            if role == "host":
                time.sleep(4)
        deadline = time.monotonic() + (180 if arguments.lifecycle else 120)
        while time.monotonic() < deadline:
            for source in select.select([front, back], [], [], 0.005)[0]:
                packet, address = source.recvfrom(65535)
                packets += 1
                packet_trace.write(
                    json.dumps(
                        {
                            "event": "received",
                            "packet": packets,
                            "time": time.monotonic(),
                            "direction": (
                                "client_to_host"
                                if source is front
                                else "host_to_client"
                            ),
                            "bytes": len(packet),
                        }
                    )
                    + "\n"
                )
                if source is front:
                    client_address = address
                    dest = back
                    target = ("127.0.0.1", hostport)
                else:
                    if client_address is None:
                        continue
                    dest = front
                    target = client_address
                heapq.heappush(
                    pending, (time.monotonic() + 0.055, packets, dest, target, packet)
                )
            while pending and pending[0][0] <= time.monotonic():
                scheduled, sequence, dest, target, packet = heapq.heappop(pending)
                dest.sendto(packet, target)
                delivered["client_to_host" if dest is back else "host_to_client"] += 1
                packet_trace.write(
                    json.dumps(
                        {
                            "event": "delivered",
                            "packet": sequence,
                            "scheduled": scheduled,
                            "time": time.monotonic(),
                            "bytes": len(packet),
                        }
                    )
                    + "\n"
                )
            states = [
                status(output_directory / (r + "-status.json"))
                for r in ("host", "client")
            ]
            if arguments.mismatch and all(s.get("reason") for s in states):
                break
            if (
                not arguments.mismatch
                and all(s.get("confirmed", -1) >= target_frame for s in states)
                and states[0].get("saved")
            ):
                break
            if any(p.poll() is not None for p in processes):
                raise AssertionError(
                    "Peer exited before required state; inspect native logs"
                )
        states = [
            status(output_directory / (r + "-status.json")) for r in ("host", "client")
        ]
        (output_directory / "final-status.json").write_text(
            json.dumps(states, indent=2)
        )
        check(
            all(count > 0 for count in delivered.values()),
            "Real UDP packets delivered in both directions",
        )
        check(
            len({s.get("pid") for s in states}) == 2, "Distinct native peer processes"
        )
        check(
            states[0].get("mode") == 2 and states[1].get("mode") == 3,
            "Actual listen server and net client",
        )
        check(
            states[0].get("connections") == 1 and states[1].get("server_connection"),
            "Actual established engine connection",
        )
        if arguments.mismatch:
            check(
                all(
                    not s.get("accepted")
                    and not s.get("running")
                    and s.get("frame") == 0
                    for s in states
                ),
                "Mismatch prevents all battle simulation on both peers",
            )
            check(
                all("Drain" in s.get("reason", "") for s in states),
                "Mismatch names offending rule on both peers",
            )
        else:
            check(
                all(s.get("accepted") and s.get("running") for s in states),
                "Both real GGPO sessions accepted exact setup and reached running",
            )
            confirmed = min(s.get("confirmed", -1) for s in states)
            check(
                confirmed >= target_frame,
                "Confirmed play crosses modifier lifecycle and victory windows",
            )
            check(
                all(s.get("session_frame", 0) > 0 for s in states),
                "Accepted connected peers advance ordinary battle simulation",
            )
            rows = []
            ledgers = []
            raw_ledgers = []
            correction_seen = False
            corrected_frames = set()
            revised_input_frames = set()
            for role in ("host", "client"):
                r = read_ledger(output_directory / (role + "-frames.csv"))
                rows += r
                revised_input_frames.update(changed_input_frames(r))
                raw_ledgers.append({int(x[0]): x for x in r if x})
                ledgers.append({int(x[0]): semantic_ledger(x) for x in r if x})
                inputs_by_frame = {}
                outcomes_by_frame = {}
                for x in r:
                    if x:
                        outcomes_by_frame.setdefault(int(x[0]), set()).add(
                            gameplay_outcome(x)
                        )
                        inputs_by_frame.setdefault(int(x[0]), set()).add(
                            (int(x[1]), int(x[2]))
                        )
                corrected_frames.update(
                    frame
                    for frame, outcomes in outcomes_by_frame.items()
                    if len(outcomes) > 1
                )
                correction_seen |= any(
                    len(pairs) > 1
                    and any(
                        player_one or player_two for player_one, player_two in pairs
                    )
                    for pairs in inputs_by_frame.values()
                )
            check(
                correction_seen,
                "Late nonneutral input visibly corrects an earlier predicted input pair at the same simulation frame",
            )
            check(
                bool(corrected_frames),
                "Late input changes observed gameplay at an already simulated frame",
            )
            def observe_boundary(label, boundary, clock_column=0):
                nearby = {int(row[0]) for row in rows if abs(int(row[clock_column]) - boundary) <= 3}
                corrected_inputs = sorted(nearby & revised_input_frames)
                boundary_observations.append({
                    "name": label, "frame": boundary, "clock_column": clock_column,
                    "revised_input_frames": corrected_inputs,
                    "changed_gameplay_frames": sorted(nearby & corrected_frames),
                })
                return bool(corrected_inputs)

            frames = sorted(
                set(ledgers[0]) & set(ledgers[1]) & set(range(1, confirmed - 1))
            )
            check(
                frames == list(range(1, confirmed - 1)),
                "Every confirmed input frame has a complete public ledger on both peers",
            )
            for frame in frames:
                check(
                    ledgers[0][frame] == ledgers[1][frame],
                    f"Confirmed divergence at frame {frame}",
                )
            if arguments.lifecycle:
                check(
                    all(s.get("requested_rematch") for s in states),
                    "Both real controllers requested rematch through their public action",
                )
                ordered = [raw_ledgers[0][f] for f in frames]
                relative = lambda r: int(
                    next(x.split(":")[1] for x in r if x.startswith("battle-frame:"))
                )
                resets = [
                    int(b[0])
                    for a0, b in zip(ordered, ordered[1:])
                    if relative(b) < relative(a0)
                ]
                check(
                    len(resets) == 1,
                    "Exactly one confirmed rematch resets the relative battle frame despite delayed held rematch inputs",
                )
                rematch = resets[0]
                episodes = [
                    [r for r in ordered if int(r[0]) < rematch],
                    [r for r in ordered if int(r[0]) >= rematch],
                ]
                hud_expected = {
                    1: {
                        "Conversion": 4,
                        "Drain": 4,
                        "Nested grandchild": 40,
                        "Nested parent": 40,
                        "Restriction": 4,
                        "Shots": 20,
                    },
                    2: {
                        "Conversion": 2,
                        "Drain": 4,
                        "Nested grandchild": 40,
                        "Nested parent": 40,
                        "Restriction": 2,
                        "SuddenDeath": 40,
                    },
                }
                boundaries = [("rematch", rematch)]
                for episode, rs in enumerate(episodes):
                    first = next(
                        (r for r in rs if int(r[4]) == 1 and int(r[5]) == 0), None
                    )
                    check(
                        first is not None,
                        f"Episode {episode} enters its first playable modifier frame",
                    )
                    check(
                        int(first[4]) == 1
                        and int(first[5]) == 0
                        and int(first[10]) == 1000
                        and int(first[18]) == 1000,
                        f"Episode {episode} begins with fresh round-one health and rule clock",
                    )
                    check(
                        live_hud_or_defer(first, hud_expected[1], 1)
                        and "hud-frame:1:0" in first,
                        f"Episode {episode} actual HUD labels and durations match first-round authoring",
                    )
                    check(
                        sum(x.startswith("object:") for x in first) == 2,
                        f"Episode {episode} creates exactly two new rule-owned attacks",
                    )
                    check(
                        any(x.startswith("rule:Drain:") for x in first)
                        and any(x.startswith("rule:Restriction:") for x in first),
                        f"Episode {episode} installs first-round scheduled rules",
                    )
                    draw = next(
                        (r for r in rs if any(x.startswith("result:3:") for x in r)),
                        None,
                    )
                    check(
                        draw is not None,
                        f"Episode {episode} simultaneous positive contacts produce a draw",
                    )
                    check(
                        int(draw[6]) == 1 and int(draw[7]) == 1,
                        f"Episode {episode} draw awards both ordinary round counters equally",
                    )
                    check(
                        int(draw[5]) == 8
                        and int(draw[10]) == 990
                        and int(draw[18]) == 990,
                        f"Episode {episode} exact nonlethal simultaneous damage wins on activation frame eight",
                    )
                    check(
                        int(draw[11]) == 0 and int(draw[19]) == 0,
                        f"Episode {episode} cancellation discards both meter children and queued damage grandchildren",
                    )
                    check(
                        not any(x.startswith(("rule:", "object:")) for x in draw),
                        f"Episode {episode} draw immediately cleans rule displays and owned attacks",
                    )
                    second = next(
                        (r for r in rs if int(r[4]) == 2 and int(r[5]) == 0), None
                    )
                    check(
                        second is not None
                        and int(second[5]) == 0
                        and int(second[10]) == 1000
                        and int(second[18]) == 1000,
                        f"Episode {episode} real round transition resets clock and health",
                    )
                    check(
                        live_hud_or_defer(second, hud_expected[2], 2)
                        and "hud-frame:2:0" in second,
                        f"Episode {episode} actual HUD labels and durations match second-round authoring",
                    )
                    check(
                        not any(x.startswith("object:") for x in second),
                        f"Episode {episode} first-round attacks cannot leak into round two",
                    )
                    check(
                        any(x.startswith("rule:SuddenDeath:") for x in second),
                        f"Episode {episode} round-two-only sudden death activates",
                    )
                    award = next(
                        (
                            r
                            for r in rs
                            if int(r[4]) == 2 and int(r[6]) == 2 and int(r[7]) == 1
                        ),
                        None,
                    )
                    check(
                        award is not None
                        and int(award[10]) == 1000
                        and int(award[18]) == 976
                        and int(award[19]) == 20,
                        f"Episode {episode} uncanceled 21-health root plus 3-health grandchild and 20-meter child award P1 after conversion expiry",
                    )
                    complete = next((r for r in rs if "result:1:1:1" in r), None)
                    check(
                        complete is not None,
                        f"Episode {episode} reaches actual P1 match result",
                    )
                    boundaries.extend(
                        [
                            (f"draw {episode}", int(draw[0])),
                            (f"round transition {episode}", int(second[0])),
                            (f"award {episode}", int(award[0])),
                            (f"match completion {episode}", int(complete[0])),
                        ]
                    )
                for label, boundary in boundaries:
                    check(
                        observe_boundary(label, boundary),
                        f"Observed delayed-input correction crosses {label}",
                    )
                check(
                    all(
                        s.get("match_complete") and s.get("winner_side") == 1
                        for s in states
                    ),
                    "Both peers finish the second complete match after rematch",
                )
            else:
                check(
                    any(int(r[10]) < 1000 or int(r[18]) < 1000 for r in rows),
                    "Actual nonconverted contact damages fighters",
                )
                check(
                    any(int(r[11]) > 0 or int(r[19]) > 0 for r in rows),
                    "Authored conversion child changes real meter",
                )
                check(
                    any(any(x.startswith("object:") for x in r) for r in rows),
                    "Live modifier-owned projectile observed",
                )
                check(
                    any(
                        any(x.startswith("rule:Restriction:") for x in r) for r in rows
                    ),
                    "Restriction activates in actual play",
                )
                check(
                    any(
                        int(r[5]) > 68
                        and int(r[5]) < 80
                        and not any(x.startswith("rule:Restriction:") for x in r)
                        for r in rows
                    ),
                    "Authored input deactivation removes the live restriction before scheduled expiry",
                )
                for boundary in (60, 65, 90, 100, 140, 200):
                    check(
                        observe_boundary(f"lifecycle boundary {boundary}", boundary, 5),
                        f"Observed delayed-input correction crosses lifecycle boundary {boundary}",
                    )
                check(
                    all(
                        s.get("match_complete") and s.get("winner_side") == 1
                        for s in states
                    ),
                    "Confirmed full modified match reaches the expected P1 result on both peers",
                )
                for description, predicate in [
                    ("round award", lambda r: int(r[6]) + int(r[7]) > 0),
                    ("match completion", lambda r: "result:1:1:1" in r),
                ]:
                    boundary = next(
                        (f for f in frames if predicate(raw_ledgers[0][f])), None
                    )
                    check(boundary is not None, f"Confirmed {description} exists")
                    check(
                        observe_boundary(description, boundary),
                        f"Observed delayed-input correction crosses {description}",
                    )
            check(states[0].get("saved"), "Host saves ordinary replay input tape")
            for p in processes:
                p.terminate()
                p.wait(timeout=10)
            replay_common = (
                [arg for arg in common if arg != "-NullRHI"]
                # Keep the explicit capability flags as well as inheriting them:
                # a rendered child that silently loses them dies during RHI init
                # with a misleading "exited with 0".
                + ["-RenderOffScreen", "-d3d11", "-sm5", "-AllowSoftwareRendering"]
                if arguments.lifecycle
                else common
            )
            replay = launch(
                [
                    arguments.editor,
                    arguments.project,
                    "/Engine/Maps/Entry?game=/Script/NightSkyEngine.ModifierPeerGameMode",
                ]
                + replay_common
                + [
                    f"-ModifierPeerDir={output_directory}",
                    "-ModifierPeerRole=replay",
                    f"-ModifierReplaySlot={slot}",
                    f"-Abslog={output_directory}/replay.log",
                ],
                "replay",
            )
            check(
                replay.wait(timeout=replay_budget) == 0,
                "Fresh replay process completes",
            )
            check(
                status(output_directory / "replay-status.json").get("pid")
                not in {s.get("pid") for s in states},
                "Replay is a third distinct process",
            )
            replay_rows = read_ledger(output_directory / "replay-frames.csv")
            replay_ledger = {int(r[0]): semantic_ledger(r) for r in replay_rows if r}
            compared = 0
            for frame in frames:
                if frame in replay_ledger and frame <= target_frame - 10:
                    check(
                        ledgers[0][frame] == replay_ledger[frame],
                        f"Confirmed replay divergence at {frame}",
                    )
                    compared += 1
            check(
                compared == target_frame - 10,
                "Fresh replay reproduces every frame of the confirmed modifier gameplay prefix",
            )
            if arguments.lifecycle:
                captures = sorted(
                    path
                    for path in output_directory.glob("replay-hud-*.txt")
                    if not path.stem.endswith("-missing")
                )
                check(
                    len(captures) == 4,
                    "Cold replay renders four actual round-start HUD captures across rematch",
                )
                captured_rounds = []
                for capture in captures:
                    lines = capture.read_text(encoding="utf-8").splitlines()
                    round_number, frame, ink, saved = map(
                        int, lines[0].split(",")
                    )
                    captured_rounds.append(round_number)
                    check(
                        frame == 0 and ink > 300 and saved == 1,
                        f"{capture.name} contains actual glyph pixels at the requested simulation frame",
                    )
                    check(
                        visible_durations(
                            "\n".join(lines[1:]), hud_expected[round_number]
                        ) or verify_visible(capture.with_suffix(".png"), hud_expected[round_number]),
                        f"{capture.name} visible rule names and numeric durations",
                    )
                    check(
                        capture.with_suffix(".png").stat().st_size > 1000,
                        f"{capture.name} persisted a rendered PNG",
                    )
                check(
                    sorted(captured_rounds) == [1, 1, 2, 2],
                    "Both round schedules render before and after rematch",
                )
        if pending_visual_rounds:
            check(
                arguments.lifecycle and not (Counter(pending_visual_rounds) - Counter(captured_rounds)),
                "Every deferred headless HUD observation has a verified confirmed replay capture",
            )
        (output_directory / "result.json").write_text(
            json.dumps(
                {
                    "passed": True,
                    "assertions": assertions,
                    "replay_process_budget_seconds": replay_budget,
                    "transport_packets": packets,
                    "delivered_packets": delivered,
                    "boundary_observations": boundary_observations,
                    "mismatch": arguments.mismatch,
                    "seed": arguments.seed,
                    "lifecycle": arguments.lifecycle,
                },
                indent=2,
            )
        )
        return 0
    except Exception as exc:
        (output_directory / "process-exits.json").write_text(
            json.dumps(
                [
                    {"role": name, "launcher_pid": p.pid, "returncode": p.poll()}
                    for name, p in zip(process_names, processes)
                ],
                indent=2,
            )
        )
        (output_directory / "result.json").write_text(
            json.dumps(
                {
                    "passed": False,
                    "assertions": assertions,
                    "replay_process_budget_seconds": replay_budget,
                    "transport_packets": packets,
                    "delivered_packets": delivered,
                    "boundary_observations": boundary_observations,
                    "error": str(exc),
                },
                indent=2,
            )
        )
        print(str(exc), file=sys.stderr)
        return 1
    finally:
        for p in processes:
            if p.poll() is None:
                p.terminate()
        for p in processes:
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
        for stream in logs:
            stream.close()
        front.close()
        back.close()
        packet_trace.close()


if __name__ == "__main__":
    sys.exit(main())

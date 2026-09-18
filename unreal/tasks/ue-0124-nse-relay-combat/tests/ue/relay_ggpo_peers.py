"""Two real listen-server/client game processes with delayed Unreal UDP transport."""

import argparse
import csv
import os
import heapq
import json
import pathlib
import select
import socket
import subprocess
import sys
import time
import hashlib
import zipfile


def read_status(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}


def semantic_frame(row):
    """Normalize opaque pool handles/tokens while retaining logical combat state."""
    core = list(row[:83])
    fighter_ids = []
    fighter_states = []
    objects = {}
    motion = {}
    attacks = {}
    owners = {}
    for i, word in enumerate(row):
        if word == "fighter":
            fighter_ids.append(int(row[i + 1]))
            fighter_states.append(tuple(row[i + 2 : i + 11]))
        elif word == "obj":
            objects[int(row[i + 1])] = (int(row[i + 2]), row[i + 3])
        elif word == "motion":
            motion[int(row[i + 1])] = tuple(row[i + 2 : i + 8])
        elif word == "attack-state":
            attacks[int(row[i + 1])] = row[i + 2]
        elif word == "contact-owner":
            owners[int(row[i + 1])] = row[i + 2]
    logical = {identity: (index // 3, index % 3) for index, identity in enumerate(fighter_ids)}
    for index, field in enumerate((21, 32, 43, 60, 71, 82)):
        active_contact = int(core[field - 2]) > 0 or (
            int(row[row.index("locks") + 1]) & (1 << index)
        )
        if not active_contact:
            core[field] = "none"
            continue
        if index in owners:
            core[field] = owners[index]
        else:
            identity = int(core[field])
            if identity < 0:
                core[field] = "none"
            elif identity in logical:
                core[field] = ("fighter", *logical[identity])
            elif identity in objects:
                core[field] = (
                    "projectile",
                    logical.get(objects[identity][0]),
                    attacks.get(identity, "State.Relay.Projectile"),
                )
            else:
                core[field] = "expired-projectile"
    projectile_states = sorted(
        (
            logical.get(owner, (-1, -1)),
            attacks.get(identity, "State.Relay.Projectile"),
            x,
            *motion.get(identity, ()),
        )
        for identity, (owner, x) in objects.items()
    )
    return (
        tuple(core),
        tuple(fighter_states),
        tuple(projectile_states),
        tuple(row[row.index("seed") : row.index("seed") + 2]),
        tuple(row[row.index("freeze") :]),
    )


def verify_selected_input_delivery(output_directory, encounter, check):
    released = []
    late = []
    for role in ("host", "client"):
        pending = {}
        with (output_directory / f"{role}-delivery.csv").open() as stream:
            for row in csv.reader(stream):
                if not row:
                    continue
                if row[0] == "late":
                    late.append((role, row))
                elif row[0] == "hold":
                    pending[(int(row[1]), int(row[2]))] = row
                elif row[0] == "release":
                    key = (int(row[1]), int(row[2]))
                    check(key in pending, "Actual release must follow a matched withheld input batch")
                    held = pending.pop(key)
                    check(int(row[3]) - int(held[4]) == int(row[4]),
                          "Delivery lateness is measured from the actual input frame")
                    check(int(row[3]) >= int(held[3]) and int(row[3]) - int(row[2]) == int(row[5]),
                          "Release respects the gate deadline and elapsed simulation frames")
                    released.append(row)
        check(not pending, "Every selected withheld batch was released before the run ended")
    required_windows = (
        {60, 64, 78, 91} if encounter == "staggered" else {60, 78, 170, 200, 235, 240, 262, 320}
    )
    missing = required_windows - {int(row[1]) for row in released}
    check(
        not missing,
        f"Selected actual GGPO input packets were delayed and released; missing={sorted(missing)}, late_arrivals={late}",
    )
    check(
        all(2 <= int(row[4]) <= 8 for row in released),
        "Every selected input was delivered two through eight simulation frames late",
    )
    check(
        required_windows.issubset({int(row[1]) for row in released}),
        "All published strike/throw/route/freeze/KO input windows received selected delay",
    )


def read_peer_ledgers(output_directory, confirmed):
    ledgers = []
    correction_kinds = set()
    for role in ("host", "client"):
        with (output_directory / f"{role}-frames.csv").open() as f:
            rows = list(csv.reader(f))
        histories = {}
        for row in rows:
            if row:
                histories.setdefault(int(row[0]), []).append(row)
        for history in histories.values():
            if int(history[0][0]) >= confirmed - 1 or len(history) < 2:
                continue
            before = history[0][:3] + history[0][4:]
            after = history[-1][:3] + history[-1][4:]
            for index in (12, 23, 34, 51, 62, 73):
                if int(after[index]) > 0 and int(before[index]) - int(after[index]) > 1:
                    correction_kinds.add("strike")
                if int(before[index]) > 0 and int(after[index]) == 0:
                    correction_kinds.add("ko")
            if any(before[index] != after[index] and int(after[index]) == 4 for index in (5, 44)):
                correction_kinds.add("route")
            if (
                int(after[after.index("locks") + 1])
                and before[before.index("locks") + 1] != after[after.index("locks") + 1]
            ):
                correction_kinds.add("throw")
            if (
                int(after[after.index("freeze") + 1])
                and before[before.index("freeze") + 1] != after[after.index("freeze") + 1]
            ):
                correction_kinds.add("freeze")
        ledgers.append({int(row[0]): row[:3] + row[4:] for row in rows if row})
    return ledgers, correction_kinds


def verify_staggered_encounter(ledgers, frames, check):

    def get_objects(row):
        return {
            int(row[i + 1]): tuple(map(int, row[i + 2 : i + 5]))
            for i, word in enumerate(row)
            if word == "obj"
        }

    def fighters(row):
        return [int(row[i + 1]) for i, word in enumerate(row) if word == "fighter"]

    first = ledgers[0][frames[0]]
    ids = fighters(first)
    check(len(ids) == 6, "Staggered observations retain six stable fighter identities")
    a_start = next(f for f in frames if int(ledgers[0][f][1]) & (1 << 21))
    b_start = next(f for f in frames if int(ledgers[0][f][2]) & (1 << 22))
    check(b_start - a_start == 4, "Staggered accepted input starts differ by four frames")
    for f in range(a_start, b_start):
        row = ledgers[0][f]
        check(int(row[5]) == 1 and int(row[44]) == 0, "Only A has entered before B input")
    for f in range(b_start, a_start + 18):
        row = ledgers[0][f]
        for offset, start in ((5, a_start), (44, b_start)):
            age = f - start
            phase, phase_age = (1, age) if age < 6 else (2, age - 6)
            check(
                int(row[offset]) == phase and int(row[offset + 1]) == phase_age,
                "Independent staggered entry and synchronized ages",
            )
    check(
        int(ledgers[0][b_start][26]) == 1
        and int(ledgers[0][b_start][37]) == 0
        and int(ledgers[0][b_start][65]) == 0
        and int(ledgers[0][b_start][76]) == 1,
        "Teams enroll different immutable reserve slots",
    )
    hit = next(f for f in frames if int(ledgers[0][f][12]) < 10000)
    before = ledgers[0][hit - 1]
    contact = ledgers[0][hit]
    check(
        int(before[5]) == 4 and int(contact[5]) == 0 and int(contact[44]) == 3,
        "Actual projectile interrupts A follow-up while B route stays open",
    )
    previous_objects = get_objects(before)
    current_objects = get_objects(contact)

    def sequence_projectiles(row, team_ids):
        elevations = {
            int(row[i + 1]): int(row[i + 3]) for i, word in enumerate(row) if word == "motion"
        }
        return [
            (owner, x)
            for identity, (owner, x, token) in get_objects(row).items()
            if owner in team_ids and elevations.get(identity) == 500000
        ]

    a_sequence = sequence_projectiles(before, ids[:3])
    b_sequence = sequence_projectiles(before, ids[3:])
    a_after = sequence_projectiles(contact, ids[:3])
    b_after = sequence_projectiles(contact, ids[3:])
    check(
        bool(a_sequence) and bool(b_sequence),
        "Both authored sequences have live owned projectiles before contact",
    )
    check(not a_after and bool(b_after), "Cancellation removes only A sequence projectiles")
    check(
        sorted((owner, x - 6000) for owner, x in b_sequence) == sorted(b_after),
        "B projectile continues its authored motion under its originating fighter",
    )
    for i in range(3):
        slot_base = 11 + 11 * i
        check(
            int(contact[slot_base + 1]) == [9700, 11856, 356][i]
            and int(contact[slot_base + 2]) == [75, 144, 144][i],
            "Independent projectile formula gives exact damage and role-dependent recoverability",
        )
        check(
            int(contact[slot_base + 5]) == 120,
            "Actual hit terminates each enrolled A cooldown identity",
        )
        owner = int(contact[slot_base + 10])
        check(
            owner in current_objects and current_objects[owner][0] == ids[5],
            "Every real A contact retains the prelaunched B owner",
        )
    b_followup = next(f for f in frames if int(ledgers[0][f][44]) == 4)
    check(
        b_followup == b_start + 28,
        "B executes follow-up one frame after the final legal route request",
    )
    for f in range(b_followup, b_followup + 12):
        check(
            int(ledgers[0][f][44]) == 4 and int(ledgers[0][f][45]) == f - b_followup,
            "B follow-up follows independent late-route timing",
        )
    b_end = b_followup + 18
    end = ledgers[0][b_end]
    check(
        int(end[44]) == 0 and int(end[75]) == 1,
        "B independently hands control to its routed third slot",
    )
    check(
        [int(end[i]) for i in (55, 66, 77)] == [120, 0, 120],
        "B cooldown follows only its enrolled immutable identities",
    )
    check(
        int(end[14]) == 1 and int(end[7]) == 100 and int(end[46]) == 100,
        "A retains original main and both teams pay once",
    )
    start_move = ledgers[0][120]
    end_move = ledgers[0][165]
    check(
        int(end_move[17]) > int(start_move[17]) and int(end_move[78]) < int(start_move[78]),
        "Both surviving mains respond to distinct ordinary movement inputs",
    )
    for index in (28, 39, 56, 67):
        check(
            end_move[index] == start_move[index],
            "Off-screen reserves do not receive ordinary movement",
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("project")
    parser.add_argument("directory")
    parser.add_argument("--editor", required=True)
    parser.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    parser.add_argument("--spacing", type=int, default=100000)
    parser.add_argument("--legacy-delay", action="store_true")
    parser.add_argument("--encounter", choices=("corrections", "staggered"), default="corrections")
    args = parser.parse_args()
    output_directory = pathlib.Path(args.directory).resolve()
    save_slot = "RelayPeer_" + output_directory.name
    output_directory.mkdir(parents=True, exist_ok=True)
    processes = []
    logs = []
    assertions = 0
    packets = 0
    pending = []
    client_address = None

    def check(value, message):
        nonlocal assertions
        assertions += 1
        if not value:
            raise AssertionError(message)

    front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    front.bind(("127.0.0.1", 0))
    front.setblocking(False)
    back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    back.bind(("127.0.0.1", 0))
    back.setblocking(False)
    # Windows reports startup ICMP port-unreachable replies as recv errors unless disabled.
    if hasattr(socket, "SIO_UDP_CONNRESET"):
        for transport in (front, back):
            transport.ioctl(socket.SIO_UDP_CONNRESET, False)
    reserve = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    reserve.bind(("127.0.0.1", 0))
    host_port = reserve.getsockname()[1]
    reserve.close()
    try:
        project_root = pathlib.Path(args.project).resolve().parent
        (output_directory / "source-manifest.json").write_text(
            json.dumps(
                {
                    str(p.relative_to(project_root)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in (project_root / "Plugins/NightSkyEngine/Source").rglob("*")
                    if p.is_file()
                },
                indent=2,
            )
        )
        (output_directory / "driver-snapshot.py").write_text(pathlib.Path(__file__).read_text())
        with zipfile.ZipFile(
            output_directory / "source-snapshot.zip", "w", zipfile.ZIP_DEFLATED
        ) as archive:
            for source in (project_root / "Plugins/NightSkyEngine/Source").rglob("*"):
                if source.is_file():
                    archive.write(source, source.relative_to(project_root))

        # Every process this driver launches is -NullRHI, so the inherited
        # capability flags in --editor-args have nothing to apply to here.
        base = [args.editor, args.project]
        for role in ("host", "client"):
            url = (
                f"/Engine/Maps/Entry?listen?game=/Script/NightSkyEngine.RelayPeerGameMode"
                if role == "host"
                else f"127.0.0.1:{front.getsockname()[1]}"
            )
            command = base + [
                url,
                "-game",
                "-RelaySample",
                f"-RelayPeerDir={output_directory}",
                f"-RelayPeerRole={role}",
                f"-RelayPeerSpacing={args.spacing}",
                "-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/NightSkyEngine.RelayFixtureGameInstance",
                "-NullRHI",
                "-unattended",
                "-nosplash",
                "-NoSound",
                f"-Abslog={output_directory}/{role}.log",
            ]
            if not args.legacy_delay:
                command.append("-RelaySelectedInputDelay")
            if args.encounter == "staggered":
                command.append("-RelayPeerStaggered")
            if role == "host":
                command.extend([f"-port={host_port}", f"-RelaySaveSlot={save_slot}"])
            (output_directory / f"{role}-command.json").write_text(json.dumps(command, indent=2))
            log = (output_directory / f"{role}-stdout.log").open("w")
            logs.append(log)
            processes.append(subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT))
            if role == "host":
                time.sleep(4)
        deadline = time.monotonic() + 120
        previous_frames = None
        progress_time = time.monotonic()
        while time.monotonic() < deadline:
            for source in select.select([front, back], [], [], 0.005)[0]:
                packet, address = source.recvfrom(65535)
                packets += 1
                if source is front:
                    client_address = address
                    destination = back
                    target = ("127.0.0.1", host_port)
                else:
                    if client_address is None:
                        continue
                    destination = front
                    target = client_address
                heapq.heappush(
                    pending,
                    (
                        time.monotonic() + (0.055 if args.legacy_delay else 0),
                        packets,
                        destination,
                        target,
                        packet,
                    ),
                )
            while pending and pending[0][0] <= time.monotonic():
                _, _, destination, target, packet = heapq.heappop(pending)
                destination.sendto(packet, target)
            status = [
                read_status(output_directory / f"{role}-status.json") for role in ("host", "client")
            ]
            current_frames = tuple(s.get("frame", -1) for s in status)
            if current_frames != previous_frames:
                previous_frames = current_frames
                progress_time = time.monotonic()
            if all(s.get("running") for s in status) and time.monotonic() - progress_time > 8:
                raise AssertionError(
                    "Both established GGPO peers stopped advancing for eight seconds"
                )
            if all(s.get("confirmed", -1) >= 420 for s in status):
                break
            if any(p.poll() is not None for p in processes):
                raise AssertionError("A peer exited before confirmed gameplay; inspect native logs")
        check(packets > 30, "Actual UDP transport must carry packets")
        status = [
            read_status(output_directory / f"{role}-status.json") for role in ("host", "client")
        ]
        (output_directory / "final-status.json").write_text(json.dumps(status, indent=2))
        # Confirmed required input has completed. Close writers before parsing full
        # histories, so no platform can expose a partially appended CSV tail.
        for process in processes:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
        check(len({s.get("pid") for s in status}) == 2, "Two distinct native game processes")
        check(
            status[0].get("mode") == 2 and status[1].get("mode") == 3,
            "Real listen server and net client",
        )
        check(
            status[0].get("connections") == 1 and status[1].get("server_connection"),
            "Real established engine connection",
        )
        check(
            all(s.get("running") for s in status), "Both peers produced real gameplay observations"
        )
        if not args.legacy_delay:
            verify_selected_input_delivery(output_directory, args.encounter, check)

        confirmed = min(s.get("confirmed", -1) for s in status)
        check(confirmed >= 420, "Confirmed gameplay must pass both relay sequences")
        # Library callback counts in final-status.json are diagnostic only.
        ledgers, correction_kinds = read_peer_ledgers(output_directory, confirmed)
        frames = sorted(set(ledgers[0]) & set(ledgers[1]) & set(range(1, confirmed - 1)))
        check(len(frames) >= 400, "Substantial complete confirmed semantic ledger")
        for frame in frames:
            check(
                semantic_frame(ledgers[0][frame]) == semantic_frame(ledgers[1][frame]),
                f"Confirmed semantic divergence at {frame}",
            )
        check(
            any(int(ledgers[0][f][1]) & (1 << 21) for f in frames),
            "Actual accepted input tape contains relay requests",
        )
        (output_directory / "correction-kinds.json").write_text(
            json.dumps(sorted(correction_kinds), indent=2)
        )
        if args.encounter == "corrections":
            check(
                correction_kinds >= {"strike", "ko", "route", "throw", "freeze"},
                "Actual late input must correct strike, KO, route, throw and freeze semantics",
            )
        else:
            check(
                "route" in correction_kinds,
                "Staggered late route input actually changes previously observed state",
            )
        check(any(int(ledgers[0][f][7]) == 100 for f in frames), "Real relay resource was paid")
        maximums = {12: 10000, 23: 12000, 34: 500, 51: 10000, 62: 12000, 73: 500}
        check(
            any(
                int(ledgers[0][f][index]) < maximum
                for f in frames
                for index, maximum in maximums.items()
            ),
            "Confirmed online encounter includes actual combat damage",
        )
        final = ledgers[0][frames[-1]]
        team_health = [
            sum(int(final[index]) for index in indices) for indices in ((12, 23, 34), (51, 62, 73))
        ]
        expected_result = (
            3 if team_health[0] == team_health[1] else 1 if team_health[0] > team_health[1] else 2
        )
        check(
            int(final[3]) == expected_result and int(final[4]) == 0,
            "Actual match ended with independently calculated team-health timeout result",
        )
        check(
            any(int(ledgers[0][f][1]) & (1 << 21) for f in frames if f >= 380),
            "Recorded positive input after match end",
        )
        for f in frames:
            if f >= 390:
                check(
                    semantic_frame(ledgers[0][f])[0][3:] == semantic_frame(final)[0][3:],
                    "Post-match combat state remains locked",
                )
        if args.encounter == "staggered":
            verify_staggered_encounter(ledgers, frames, check)
        check(status[0].get("saved"), "Host persisted its real replay input recording")
        for process in processes:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
        command = base + [
            "/Engine/Maps/Entry?game=/Script/NightSkyEngine.RelaySampleGameMode",
            "-game",
            "-RelaySample",
            f"-RelayPeerDir={output_directory}",
            "-RelayPeerRole=replay",
            f"-RelayReplaySlot={save_slot}",
            f"-RelayPeerSpacing={args.spacing}",
            "-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/NightSkyEngine.RelayFixtureGameInstance",
            "-NullRHI",
            "-unattended",
            "-nosplash",
            "-NoSound",
            f"-Abslog={output_directory}/replay.log",
        ]
        if args.encounter == "staggered":
            command.append("-RelayPeerStaggered")
        (output_directory / "replay-command.json").write_text(json.dumps(command, indent=2))
        log = (output_directory / "replay-stdout.log").open("w")
        logs.append(log)
        replay = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        processes.append(replay)
        check(replay.wait(timeout=90) == 0, "Fresh saved replay game process completed")
        replay_status = read_status(output_directory / "replay-status.json")
        check(
            replay_status.get("pid") not in {x.get("pid") for x in status},
            "Replay loaded in a distinct fresh process",
        )
        with (output_directory / "replay-frames.csv").open() as f:
            rows = list(csv.reader(f))
        replay_ledger = {int(row[0]): row[:3] + row[4:] for row in rows if row}
        compared = 0
        for frame in frames:
            if frame in replay_ledger and frame <= 410:
                check(
                    semantic_frame(ledgers[0][frame]) == semantic_frame(replay_ledger[frame]),
                    f"Persisted replay semantic divergence at {frame}",
                )
                compared += 1
        check(compared >= 400, "Persisted replay reproduces a substantial confirmed prefix")
        (output_directory / "result.json").write_text(
            json.dumps(
                {
                    "passed": True,
                    "assertions": assertions,
                    "transport_packets": packets,
                    "confirmed": confirmed,
                    "compared_confirmed_frames": len(frames),
                    "replayed_frames": compared,
                    "match_result": expected_result,
                    "final_team_health": team_health,
                    "encounter": args.encounter,
                    "correction_kinds": sorted(correction_kinds),
                    "peer_pids": [x["pid"] for x in status],
                    "replay_pid": replay_status.get("pid"),
                    "rollback_loads": sum(x["loads"] for x in status),
                    "resimulated_frames": sum(x["resimulated"] for x in status),
                },
                indent=2,
            )
        )
        return 0
    except Exception as exc:
        (output_directory / "result.json").write_text(
            json.dumps(
                {
                    "passed": False,
                    "assertions": assertions,
                    "transport_packets": packets,
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
        for log in logs:
            log.close()
        front.close()
        back.close()


if __name__ == "__main__":
    sys.exit(main())

"""Native Unreal worker processes and an observed UDP delay transport.

This module does not decide gameplay outcomes. Task drivers send public operations
and inspect the emitted actor observations. All subprocesses must be launched under
implementation/run_locked.py, including callers importing WorkerGroup.
"""

from __future__ import annotations
import argparse
import csv
import hashlib
import heapq
import json
import os
from pathlib import Path
from dataclasses import dataclass
import random
import selectors
import signal
import socket
import subprocess
import sys
import threading
import time


def force_kill_authority(pid):
    """Ungraceful death of the exact worker PID reported by its public heartbeat."""
    if os.name == "nt":
        result = subprocess.run(
            ["taskkill", "/PID", str(pid), "/T", "/F"],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if result.returncode:
            raise RuntimeError(
                "Forced authority termination failed: " + result.stdout + result.stderr
            )
        return "taskkill /T /F"
    os.kill(pid, signal.SIGKILL)
    return "SIGKILL"


def windows_process_is_running(pid):
    """Check exact recorded owned PIDs after a failed tree-termination call."""
    result = subprocess.run(
        ["tasklist", "/FI", f"PID eq {pid}", "/FO", "CSV", "/NH"],
        capture_output=True,
        text=True,
        timeout=30,
        check=True,
    )
    return any(
        len(row) > 1 and row[1] == str(pid)
        for row in csv.reader(result.stdout.splitlines())
    )


def observed_corrections(events):
    """Find actual same-frame gameplay changes followed by confirmed convergence."""
    unconfirmed = {}
    corrections = []
    for event in events:
        frame = event.get("prediction_frame", -1)
        state = event.get("prediction_state")
        if frame < 0 or state is None:
            continue
        if event.get("prediction_confirmed", -1) < frame:
            unconfirmed.setdefault(frame, []).append(state)
        elif any(previous != state for previous in unconfirmed.get(frame, [])):
            corrections.append(
                dict(frame=frame, before=unconfirmed[frame], after=state)
            )
    return corrections


@dataclass(frozen=True)
class ProxyRoute:
    generation: int
    frontend: tuple
    upstream: socket.socket


class PacketProxy:
    def __init__(self, server_port: int, directory: Path, name: str, seed: int = 2002):
        self.front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.front.bind(("127.0.0.1", 0))
        self.server = ("127.0.0.1", server_port)
        self.routes = {}
        self.frontend_routes = {}
        self.port = self.front.getsockname()[1]
        self.delay_ms = {"to_server": 0, "to_client": 0}
        self.jitter_ms = 0
        self.drop_every = 0
        self.duplicate_every = 0
        self.reorder_every = 0
        self.random = random.Random(seed)
        self.fault_epoch = None
        self.finite_faults = None
        self.finite_settings = None
        self.finite_epoch = 0
        self.finite_required_routes = set()
        self.alive = True
        self.schedule = []
        self.sequence = 0
        self.simulation_time = None
        self.block_client = False
        self.blocked_client_packets = []
        self.client_spacing_seconds = 0.0
        self.next_client_delivery = 0.0
        self.log = (directory / f"packets-{name}.jsonl").open("w")
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def configure(
        self,
        to_server=0,
        to_client=0,
        jitter=0,
        drop_every=0,
        duplicate_every=0,
        reorder_every=0,
        context=None,
        finite_faults=False,
        finite_directions=("to_server",),
        finite_drop_count=1,
    ):
        if (
            min(to_server, to_client, jitter) < 0
            or max(to_server, to_client) + jitter > 300
        ):
            raise ValueError("Delivery schedule must stay within 0..300 ms")
        if finite_drop_count not in (1, 2) or not set(finite_directions) <= {"to_server", "to_client"}:
            raise ValueError("Finite faults require known directions and one/two dropped datagrams")
        if self.finite_faults and any(w.get("held") for w in self.finite_faults.values()):
            raise RuntimeError("Cannot reset an unfinished finite packet witness")
        self.finite_epoch += 1
        self.finite_settings = (tuple(finite_directions), finite_drop_count) if finite_faults else None
        # Require all configured directions on the current connection and any
        # newly opened routes, independently of witnesses that actually start.
        self.finite_required_routes = {max(self.routes)} if finite_faults and self.routes else set()
        self.finite_faults = {} if finite_faults else None
        self.delay_ms = {"to_server": to_server, "to_client": to_client}
        self.jitter_ms, self.drop_every = jitter, drop_every
        self.duplicate_every, self.reorder_every = duplicate_every, reorder_every
        self.fault_epoch = (
            {"id": context, "sequence": 0, "random": random.Random(context)}
            if context is not None
            else None
        )

    def finite_faults_complete(self):
        if not self.finite_settings or not self.finite_required_routes or not self.finite_faults:
            return False
        directions, _ = self.finite_settings
        if any((generation, direction) not in self.finite_faults
               for generation in self.finite_required_routes for direction in directions):
            return False
        # Genuine partial witnesses on older routes still require completion.
        return all(witness["count"] >= witness["drops"] + 2 and
                   witness["delivered"] == {"held", "overtake", "duplicate"}
                   for witness in self.finite_faults.values())

    def throttle_client(self, spacing_seconds=0.0):
        """External bandwidth restriction for an interrupted-transfer scenario."""
        if not 0 <= spacing_seconds <= 0.03:
            raise ValueError("Packet spacing outside fixture bound")
        self.client_spacing_seconds = spacing_seconds
        if not spacing_seconds:
            self.next_client_delivery = 0.0

    def _write(self, value):
        self.log.write(json.dumps(value, separators=(",", ":")) + "\n")
        self.log.flush()

    def _run(self):
        selector = selectors.DefaultSelector()
        selector.register(self.front, selectors.EVENT_READ, ("to_server", None))
        try:
            while self.alive:
                for key, _ in selector.select(0.001):
                    try:
                        payload, peer = key.fileobj.recvfrom(65535)
                    except ConnectionResetError as error:
                        # Windows reports an ICMP port-unreachable notification here
                        # when a travelling endpoint briefly closes its UDP socket.
                        if getattr(error, "winerror", None) != 10054:
                            raise
                        self._write(
                            {
                                "action": "udp-receive-reset",
                                "direction": key.data[0],
                                "received": time.monotonic(),
                                "winerror": 10054,
                            }
                        )
                        continue
                    direction, route = key.data
                    if direction == "to_server":
                        route = self.frontend_routes.get(peer)
                        if route is None:
                            upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                            upstream.bind(("127.0.0.1", 0))
                            route = ProxyRoute(len(self.routes) + 1, peer, upstream)
                            self.routes[route.generation] = route
                            self.frontend_routes[peer] = route
                            if self.finite_settings is not None:
                                self.finite_required_routes.add(route.generation)
                            selector.register(upstream, selectors.EVENT_READ, ("to_client", route))
                            self._write(dict(action="route-open", direction="route", route_generation=route.generation,
                                             frontend=list(peer), upstream=list(upstream.getsockname()),
                                             received=time.monotonic()))
                    self.sequence += 1
                    epoch = self.fault_epoch
                    if epoch is not None:
                        epoch["sequence"] += 1
                    fault_sequence = (
                        epoch["sequence"] if epoch is not None else self.sequence
                    )
                    now = time.monotonic()
                    record = {
                        "sequence": self.sequence,
                        "direction": direction,
                        "received": now,
                        "bytes": len(payload),
                        "sha256": hashlib.sha256(payload).hexdigest(),
                        "route_generation": route.generation,
                        "frontend": list(route.frontend),
                        "upstream": list(route.upstream.getsockname()),
                    }
                    if direction == "to_client" and self.block_client:
                        self.blocked_client_packets.append(record)
                        self._write(dict(record, action="interrupted-transfer-drop"))
                        continue
                    if epoch is not None:
                        record.update(
                            fault_context=epoch["id"], fault_sequence=fault_sequence
                        )
                    # Keep each burst/held/overtaking witness within one route.
                    # A new connection cannot release an old connection's held packet.
                    if self.finite_settings is not None:
                        directions, drops = self.finite_settings
                        if direction in directions:
                            self.finite_faults.setdefault((route.generation, direction),
                                {"count": 0, "drops": drops, "held": None, "delivered": set()})
                    witness = (self.finite_faults or {}).get((route.generation, direction))
                    if witness is not None and witness["count"] < witness["drops"] + 2:
                        witness["count"] += 1
                        record.update(finite_drop_count=witness["drops"], finite_epoch=self.finite_epoch)
                        if witness["count"] <= witness["drops"]:
                            self._write(dict(record, action="drop", finite_drop_index=witness["count"]))
                            continue
                        if witness["count"] == witness["drops"] + 1:
                            witness["held"] = (payload, record)
                            self._write(dict(record, action="held"))
                            continue
                        clock = now if self.simulation_time is None else self.simulation_time
                        held_payload, held_record = witness["held"]
                        witness["held"] = None
                        for offset, body, row, label in (
                            (0.020, payload, record, "overtake"),
                            (0.023, payload, dict(record, duplicate=True), "duplicate"),
                            (0.026, held_payload, held_record, "held"),
                        ):
                            row = dict(row, scheduled_delay=offset, finite_witness=label)
                            heapq.heappush(self.schedule, (clock + offset, row["sequence"], body, row))
                            self._write(dict(row, action="queued"))
                        continue
                    # Loss is observable, but an application-level retry/superseding
                    # state still has to be proven by the task's own observations.
                    if self.drop_every and fault_sequence % self.drop_every == 0:
                        if self.simulation_time is not None:
                            record["simulation_received"] = self.simulation_time
                        self._write(dict(record, action="drop"))
                        continue
                    delay = (
                        max(
                            0,
                            self.delay_ms[direction]
                            + (
                                epoch["random"] if epoch is not None else self.random
                            ).uniform(-self.jitter_ms, self.jitter_ms),
                        )
                        / 1000
                    )
                    if self.reorder_every and fault_sequence % self.reorder_every == 0:
                        delay += 0.04
                        record["reorder_delayed"] = True
                    record["scheduled_delay"] = delay
                    clock = (
                        now if self.simulation_time is None else self.simulation_time
                    )
                    if self.simulation_time is not None:
                        record["simulation_received"] = clock
                    if direction == "to_client" and self.client_spacing_seconds:
                        deadline = max(clock + delay, self.next_client_delivery)
                        self.next_client_delivery = (
                            deadline + self.client_spacing_seconds
                        )
                        delay = deadline - clock
                        record["scheduled_delay"] = delay
                        record["bandwidth_spacing"] = self.client_spacing_seconds
                    heapq.heappush(
                        self.schedule, (clock + delay, self.sequence, payload, record)
                    )
                    self._write(dict(record, action="queued"))
                    if (
                        self.duplicate_every
                        and fault_sequence % self.duplicate_every == 0
                    ):
                        duplicate = dict(record, duplicate=True)
                        heapq.heappush(
                            self.schedule,
                            (
                                clock + delay + 0.003,
                                self.sequence + 0.5,
                                payload,
                                duplicate,
                            ),
                        )
                now = (
                    time.monotonic()
                    if self.simulation_time is None
                    else self.simulation_time
                )
                while self.schedule and self.schedule[0][0] <= now:
                    _, _, payload, record = heapq.heappop(self.schedule)
                    # Routes remain alive until proxy close, including after a
                    # replacement frontend appears. Queues never use mutable peers.
                    route = self.routes[record["route_generation"]]
                    outbound, destination = (
                        (route.upstream, self.server)
                        if record["direction"] == "to_server"
                        else (self.front, route.frontend)
                    )
                    outbound.sendto(payload, destination)
                    if self.simulation_time is not None:
                        record["simulation_delivered"] = self.simulation_time
                    self._write(
                        dict(record, action="deliver", delivered=time.monotonic())
                    )
                    if (record.get("finite_witness") and self.finite_faults is not None
                            and record.get("finite_epoch") == self.finite_epoch):
                        self.finite_faults[(record["route_generation"], record["direction"])]["delivered"].add(
                            record["finite_witness"])
        finally:
            selector.close()

    def close(self):
        self.alive = False
        self.thread.join(timeout=2)
        self.front.close()
        for route in self.routes.values():
            route.upstream.close()
        self.log.close()


def inherited_editor_args(argv=None):
    """RHI capability flags the parent editor forwarded via --editor-args."""
    argv = list(sys.argv if argv is None else argv)
    if "--editor-args" in argv:
        index = argv.index("--editor-args") + 1
        if index < len(argv):
            return argv[index].split()
    return []


class WorkerGroup:
    def __init__(
        self,
        editor: Path,
        project: Path,
        directory: Path,
        editor_args=None,
        battle_seed=9009,
        extra_args=(),
        rendering_roles=(),
    ):
        self.extra_args = list(extra_args)
        self.rendering_roles = set(rendering_roles)
        # Default from argv rather than requiring every construction site to
        # thread it through: helper drivers construct groups too, and a site
        # that silently forgets produces a rendered child with no RHI flags,
        # which dies during init with a confusing "exited with 0".
        self.editor_args = (
            list(editor_args) if editor_args is not None else inherited_editor_args()
        )
        self.editor, self.project = editor.resolve(), project.resolve()
        self.directory = directory.resolve()
        self.directory.mkdir(parents=True, exist_ok=False)
        self.processes = {}
        self.proxies = []
        self.logs = []
        self.next_id = 0
        self._event_streams = {}
        self._command_sends = {}
        self.battle_seed = battle_seed
        self.logical_time = None
        self.closed = False
        self.expected_integrity_errors = set()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            self.port = reservation.getsockname()[1]

    def _launch(self, role, address, server=False, listen=False):
        args = [
            str(self.editor),
            str(self.project),
            address,
            "-game",
            "-unattended",
            "-nosplash",
            "-NoSound",
            "-ResX=640",
            "-ResY=360",
            "-windowed",
            f"-RoomBattleSeed={self.battle_seed}",
            f"-RoomWorkerDir={self.directory}",
            f"-RoomWorkerRole={role}",
            "-nosteam",
            "-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/NightSkyEngine.RoomWorkerGameInstance",
            "-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=Null",
            "-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GlobalDefaultGameMode=/Script/NightSkyEngine.RoomWorkerGameMode",
            "-ini:Engine:[/Script/Engine.Engine]:GameViewportClientClassName=/Script/Engine.GameViewportClient",
            "-ini:Engine:[/Script/Engine.Engine]:LocalPlayerClassName=/Script/Engine.LocalPlayer",
            # Network/travel timers use elapsed time even below the rendering cap.
            # Combat still advances only through explicit fixed-step commands.
            "-ini:Engine:[/Script/Engine.Engine]:bUseFixedFrameRate=False",
            "-ExecCmds=t.MaxFPS 60",
            f"-abslog={self.directory / (role + '.log')}",
        ]
        args += [value for value in self.extra_args
                 if not (role in self.rendering_roles and value.casefold() == "-nullrhi")]
        if not server:
            args += ["-RenderOffscreen"]
        if server:
            args += ["-server", "-NullRHI", f"-port={self.port}"]
        elif listen:
            args += [f"-port={self.port}"]
        if "-NullRHI" not in args:
            # Rendering children inherit the capability flags the parent editor
            # was started with, which come from the task's spec.yaml. Without
            # them a GPU-less host rejects the WARP adapter and the child exits
            # during RHI init.
            args += self.editor_args
        log = (self.directory / f"stdout-{role}.log").open("w")
        self.logs.append(log)
        self.processes[role] = subprocess.Popen(
            args, stdout=log, stderr=subprocess.STDOUT
        )
        (self.directory / f"launch-{role}.json").write_text(json.dumps(args, indent=2))

    def start(self, clients=2, mode="/Script/NightSkyEngine.RoomWorkerGameMode"):
        self._launch("server", f"/Engine/Maps/Entry?game={mode}", server=True)
        self.wait_for(
            lambda: any(e.get("net_mode") == 1 for e in self.events("server")),
            "dedicated server startup",
        )
        for _ in range(clients):
            self.add_client()
        self.wait_for(
            lambda: any(
                e.get("connections", 0) >= clients for e in self.events("server")
            ),
            "server connections",
        )
        for index in range(clients):
            role = f"client{index}"
            self.wait_for(
                lambda role=role: any(
                    e.get("server_connection") and e.get("net_mode") == 3
                    for e in self.events(role)
                ),
                role + " connection",
            )
        pids = {self.processes[r].pid for r in self.processes}
        if len(pids) != clients + 1:
            raise RuntimeError("Workers must be separate processes")

    def add_client(self):
        index = len(self.proxies)
        role = f"client{index}"
        proxy = PacketProxy(self.port, self.directory, role)
        self.proxies.append(proxy)
        self._launch(role, f"127.0.0.1:{proxy.port}")
        return role

    def _repair_interrupted_event_tail(self, role):
        # A killed worker may leave an incomplete IPC line. Keep every complete
        # observation and archive the fragment before its replacement appends.
        path = self.directory / f"events-{role}.jsonl"
        if not path.exists():
            return
        data = path.read_bytes()
        end = data.rfind(b"\n") + 1
        if end == len(data):
            return
        (
            self.directory / f"events-{role}-interrupted-tail-{time.time_ns()}.bin"
        ).write_bytes(data[end:])
        with path.open("r+b") as stream:
            stream.truncate(end)
        state = self._event_streams.get(role)
        if state and state["offset"] > end:
            state["offset"] = end
            state["tail"] = b""

    def restart(self, role, proxy_index, force=False, after_shutdown=None, reconnect=None):
        """Restart an owned viewer process; preserve its transport route and audit log."""
        process = self.processes[role]
        old_pid = next(e["pid"] for e in reversed(self.events(role)) if "pid" in e)
        offset = len(self.events(role))
        if force or os.name == "nt":
            force_kill_authority(old_pid)
        else:
            process.terminate()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=10)
        if after_shutdown is not None:
            after_shutdown()
        self._repair_interrupted_event_tail(role)
        commands = self.directory / f"commands-{role}.jsonl"
        if commands.exists():
            (
                self.directory
                / f"commands-{role}-before-restart-{time.time_ns()}.jsonl"
            ).write_bytes(commands.read_bytes())
            commands.write_text("")
        self._launch(role, f"127.0.0.1:{self.proxies[proxy_index].port}")
        next_authentication_probe = 0.0

        def ready():
            nonlocal next_authentication_probe
            if not any(
                e.get("pid") != old_pid
                and e.get("server_connection")
                and e.get("owner_channel_ready")
                for e in self.events(role)[offset:]
            ):
                return False
            if reconnect is None:
                return True
            # A killed UDP peer can remain owned until the authority's normal
            # transport timeout. Retry login on the fresh channel within the
            # existing process-readiness budget, preserving every response.
            now = time.monotonic()
            if now < next_authentication_probe:
                return False
            next_authentication_probe = now + 1.0
            return reconnect()

        self.wait_for(
            ready,
            role + " fresh process connection and replacement authentication"
            if reconnect is not None else role + " fresh process connection",
            timeout=120,
        )

    def send(self, role, op, **arguments):
        self.next_id += 1
        identifier = self.next_id
        command = dict(arguments, id=identifier, op=op)
        self._command_sends[(role, identifier)] = (
            time.monotonic(),
            op,
            arguments.get("operation"),
        )
        with (self.directory / f"commands-{role}.jsonl").open("a") as stream:
            stream.write(json.dumps(command, separators=(",", ":")) + "\n")
            stream.flush()
        return identifier

    def reply(self, role, identifier):
        def available():
            self.events(role)
            return identifier in self._event_streams.get(role, {}).get(
                "reply_by_id", {}
            )

        self.wait_for(available, f"{role} reply {identifier}", timeout=30)
        reply = self._event_streams[role]["reply_by_id"][identifier]
        sent = self._command_sends.pop((role, identifier), None)
        if sent:
            with (self.directory / "command-timings.jsonl").open("a") as stream:
                stream.write(
                    json.dumps(
                        dict(
                            role=role,
                            identifier=identifier,
                            operation=sent[1],
                            room_operation=sent[2],
                            send_to_reply_seconds=time.monotonic() - sent[0],
                            native_reply_seconds=reply.get("seconds"),
                        )
                    )
                    + "\n"
                )
        if not reply.get("ok"):
            raise RuntimeError(f"Operation failed: {role}: {reply}")
        return reply

    def command(self, role, op, **arguments):
        return self.reply(role, self.send(role, op, **arguments))

    def control_clock(self):
        paused = {role: self.command(role, "clock_pause") for role in self.processes}
        self.logical_time = paused["server"]["epoch"]
        for role in self.processes:
            reply = self.command(role, "clock_sync", epoch=self.logical_time)
            if abs(reply["clock"] - self.logical_time) > 1e-6:
                raise RuntimeError("Clock epoch synchronization failed")
        self.wait_for(
            lambda: all(not proxy.schedule for proxy in self.proxies),
            "zero-delay transport drain",
            timeout=10,
        )
        for proxy in self.proxies:
            proxy.simulation_time = self.logical_time
        return self.logical_time

    def step(self, delta=1 / 60):
        if self.logical_time is None or not 0 < delta <= 1 / 60:
            raise ValueError(
                "A controlled clock and <=1/60 simulation step are required"
            )
        self.logical_time += delta
        for proxy in self.proxies:
            proxy.simulation_time = self.logical_time
        # Let sockets deliver every due datagram before advancing the next frame.
        self.wait_for(
            lambda: all(
                not p.schedule or p.schedule[0][0] > self.logical_time
                for p in self.proxies
            ),
            "due datagram delivery",
            timeout=10,
        )
        pending = {
            role: self.send(role, "clock_step", delta=delta) for role in self.processes
        }
        replies = {
            role: self.reply(role, identifier) for role, identifier in pending.items()
        }
        for role, reply in replies.items():
            if abs(reply["server_time"] - self.logical_time) > 1e-5:
                raise RuntimeError(
                    f"Simulation barrier drift: {role}: {reply['server_time']} != {self.logical_time}"
                )
        return replies

    def events(self, role):
        path = self.directory / f"events-{role}.jsonl"
        if not path.exists():
            return []
        state = self._event_streams.setdefault(
            role,
            {
                "offset": 0,
                "tail": b"",
                "events": [],
                "reply_by_id": {},
                "delivery_by_nonce": {},
                "presentation_by_nonce": {},
            },
        )
        if path.stat().st_size < state["offset"]:
            state.update(
                offset=0,
                tail=b"",
                events=[],
                reply_by_id={},
                delivery_by_nonce={},
                presentation_by_nonce={},
            )
        with path.open("rb") as stream:
            stream.seek(state["offset"])
            incoming = stream.read()
            state["offset"] = stream.tell()
        lines = (state["tail"] + incoming).split(b"\n")
        state["tail"] = lines.pop()
        for line in lines:
            if line:
                event = json.loads(line.decode("utf-8"))
                state["events"].append(event)
                if event.get("type") == "reply":
                    state["reply_by_id"][event["id"]] = event
                if event.get("nonce") and event.get("type") in (
                    "delivery",
                    "presentation",
                ):
                    state[event["type"] + "_by_nonce"][event["nonce"]] = event
        return state["events"]

    def delivery(self, role, nonce):
        self.events(role)
        return self._event_streams.get(role, {}).get("delivery_by_nonce", {}).get(nonce)

    def presentation(self, role, nonce):
        self.events(role)
        return (
            self._event_streams.get(role, {})
            .get("presentation_by_nonce", {})
            .get(nonce)
        )

    def wait_for(self, condition, label, timeout=120):
        began = time.monotonic()
        deadline = began + timeout
        polls = 0
        condition_seconds = 0.0
        completed = False
        try:
            while True:
                probe = time.monotonic()
                ready = condition()
                condition_seconds += time.monotonic() - probe
                polls += 1
                if ready:
                    completed = True
                    return
                for role, process in self.processes.items():
                    recent = self.events(role)
                    if (
                        role not in self.expected_integrity_errors
                        and recent
                        and recent[-1].get("integrity_failed")
                    ):
                        raise AssertionError(
                            f"Native room reported integrity error on {role}; see {self.directory}"
                        )
                    if process.poll() is not None:
                        raise RuntimeError(
                            f"{role} exited with {process.returncode}; see {self.directory}"
                        )
                if time.monotonic() >= deadline:
                    raise TimeoutError(
                        label + "; no passing inference from absent evidence"
                    )
                time.sleep(0.01)
        finally:
            with (self.directory / "wait-timings.jsonl").open("a") as stream:
                stream.write(
                    json.dumps(
                        dict(
                            label=label,
                            elapsed_seconds=time.monotonic() - began,
                            polls=polls,
                            condition_seconds=condition_seconds,
                            completed=completed,
                        )
                    )
                    + "\n"
                )

    def close(self):
        if self.closed:
            return
        self.closed = True
        for role, process in self.processes.items():
            owned_pids = {process.pid} | {
                event["pid"] for event in self.events(role) if "pid" in event
            }
            if process.poll() is None:
                if os.name == "nt":
                    try:
                        force_kill_authority(process.pid)
                    except RuntimeError:
                        # The launcher may exit after its children are killed,
                        # before taskkill reaches the launcher itself.
                        if process.poll() is None or any(
                            windows_process_is_running(pid) for pid in owned_pids
                        ):
                            raise
                else:
                    process.terminate()
        for process in self.processes.values():
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        for proxy in self.proxies:
            proxy.close()
        for log in self.logs:
            log.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    parser.add_argument("--clients", type=int, default=2)
    args = parser.parse_args()
    group = WorkerGroup(
        args.editor, args.project, args.directory, editor_args=args.editor_args.split()
    )
    try:
        group.start(args.clients)
        for role in group.processes:
            group.command(role, "snapshot")
        print(
            json.dumps(
                {
                    "bootstrap": "passed",
                    "processes": {r: p.pid for r, p in group.processes.items()},
                    "gameplay_assertions": 0,
                }
            )
        )
    finally:
        group.close()


if __name__ == "__main__":
    main()

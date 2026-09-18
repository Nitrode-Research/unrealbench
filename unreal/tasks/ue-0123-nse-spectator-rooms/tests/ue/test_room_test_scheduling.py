"""Host-only checks of test scheduling; does not execute or replace Unreal tests."""
import json
from pathlib import Path
import socket
import tempfile
import time
import unittest
from types import SimpleNamespace

from room_combined_lifecycle import CombinedLifecycle
from room_navigation_fuzz import make_plan
from room_network_workers import PacketProxy


from room_response_contract import annotate_response, response_matches


class ResponseContractTests(unittest.TestCase):
    def test_success_uses_effect_without_vocabulary(self):
        self.assertTrue(response_matches(annotate_response(
            {"status": "", "locked": True}, {"locked": False}, "lock", {}), "accepted"))
        self.assertFalse(response_matches(annotate_response(
            {"status": "accepted", "locked": False}, {"locked": False}, "lock", {}), "accepted"))

    def test_start_preserves_organizer_selected_history(self):
        before = {"match": "retained-history", "frame": 20}
        started = annotate_response(dict(before, active_match=True, status=""), before, "start", {})
        self.assertTrue(response_matches(started, "accepted"))
        self.assertFalse(response_matches(dict(started, active_match=False), "accepted"))

    def test_rejection_requires_diagnostic_and_unchanged_state(self):
        before = {"frame": 4, "match": "m", "mode": "paused", "assignment": "a", "locked": False}
        rejected = annotate_response(dict(before, status="Permission denied"), before, "lock", {})
        self.assertTrue(response_matches(rejected, "unauthorized"))
        self.assertFalse(response_matches(dict(rejected, locked=True), "unauthorized"))
        self.assertFalse(response_matches(dict(rejected, status=""), "unauthorized"))

    def test_export_uses_bytes_and_duplicate_uses_acknowledgement(self):
        self.assertTrue(response_matches({"status": "", "replay": "bytes"}, "exported"))
        self.assertFalse(response_matches({"status": "exported", "replay": ""}, "exported"))
        self.assertTrue(response_matches({"status": "Not yet", "replay": ""}, "pending"))
        self.assertFalse(response_matches({"status": "pending", "replay": "bytes"}, "pending"))
        duplicate = annotate_response({"ack": 9}, {"ack": 9}, "queue", {})
        self.assertTrue(response_matches(duplicate, "duplicate"))
        self.assertFalse(response_matches(dict(duplicate, ack=10), "duplicate"))


from room_ui_review import automatic_observation, evaluate, packet


class DisplayMeaningTests(unittest.TestCase):
    def test_stale_ready_never_counts_as_displayed_state(self):
        for field in ("organizer_role", "selection_locked", "replay_listing", "replay_playing", "error_visible",
                      "room_created", "endpoint_listening"):
            self.assertIsNone(automatic_observation(field, "Ready"))
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.assertFalse(evaluate([dict(field="selection_locked", text="Ready", expected=True)],
                                     root / "result", root / "reviews", 0))
            request = json.loads(next((root / "reviews").glob("*.request.json")).read_text())
            self.assertNotIn("expected", request)
            self.assertNotIn("action", request)

    def test_displayed_boolean_meanings_and_negation(self):
        self.assertTrue(automatic_observation("selection_locked", "Selection locked"))
        self.assertFalse(automatic_observation("selection_locked", "Selection unlocked"))
        self.assertFalse(automatic_observation("selection_locked", "locked: false"))
        self.assertFalse(automatic_observation("replay_playing", "Not playing"))
        self.assertFalse(automatic_observation("error_visible", "No errors"))
        self.assertFalse(automatic_observation("endpoint_listening", "Not listening"))
        self.assertFalse(automatic_observation("room_created", "Room not created"))

    def test_blind_observation_binds_text_and_is_reloaded(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            text = "선택 잠김"
            row = dict(field="selection_locked", text=text, expected=True)
            self.assertFalse(evaluate([row], root / "result", root / "reviews", 0))
            request = packet(row["field"], text)
            response = dict(request_sha256=request["request_sha256"], field=row["field"],
                            readable=True, actual=True, evidence="The Korean text says selection is locked.")
            path = root / "reviews" / (request["request_sha256"] + ".selection_locked.observation.json")
            path.write_text(json.dumps(response))
            self.assertTrue(evaluate([row], root / "result", root / "reviews", 0))
            response["request_sha256"] = "wrong"
            path.write_text(json.dumps(response))
            with self.assertRaises(ValueError):
                evaluate([row], root / "result", root / "reviews", 0)


class SchedulingTests(unittest.TestCase):
    def test_navigation_has_all_classes_and_each_viewer(self):
        plan = make_plan()
        self.assertEqual(plan, make_plan())
        self.assertEqual(len(plan["actions"]), 45)
        for choice in range(15):
            rows = [row for row in plan["actions"] if row["choice"] == choice]
            self.assertEqual({row["role"] for row in rows}, {0, 1, 2})
        self.assertEqual(sum(row["restart"] for row in plan["actions"]), 3)

    def test_lifecycle_classes_are_complete_at_every_checkpoint(self):
        with tempfile.TemporaryDirectory() as directory:
            scenario = CombinedLifecycle(SimpleNamespace(directory=Path(directory)), None)
            self.assertEqual(len(scenario.plan), 144)
            for generation in range(3):
                for phase in range(4):
                    self.assertEqual(
                        {a["choice"] for a in scenario.plan
                         if a["generation"] == generation and a["phase"] == phase},
                        set(range(12)))

    def test_finite_real_udp_loss_duplicate_and_reorder(self):
        with tempfile.TemporaryDirectory() as directory, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            server.bind(("127.0.0.1", 0))
            server.settimeout(2)
            proxy = PacketProxy(server.getsockname()[1], Path(directory), "probe")
            try:
                proxy.configure(finite_faults=True, context="unit")
                for payload in (b"lost", b"held", b"overtake"):
                    client.sendto(payload, ("127.0.0.1", proxy.port))
                received = [server.recvfrom(1024)[0] for _ in range(3)]
                self.assertEqual(received, [b"overtake", b"overtake", b"held"])
                deadline = time.monotonic() + 2
                while not proxy.finite_faults_complete() and time.monotonic() < deadline:
                    time.sleep(0.001)
                self.assertTrue(proxy.finite_faults_complete())
                client.sendto(b"healthy", ("127.0.0.1", proxy.port))
                self.assertEqual(server.recvfrom(1024)[0], b"healthy")
                proxy.configure()
            finally:
                proxy.close()
            rows = [json.loads(s) for s in (Path(directory) / "packets-probe.jsonl").read_text().splitlines()]
            self.assertEqual(sum(row["action"] == "drop" for row in rows), 1)
            delivered = [row["sequence"] for row in rows if row["action"] == "deliver"]
            self.assertTrue(any(a > b for a, b in zip(delivered, delivered[1:])))

    def test_bidirectional_burst_faults_then_healthy_transport(self):
        with tempfile.TemporaryDirectory() as directory, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            server.bind(("127.0.0.1", 0)); server.settimeout(2); client.settimeout(2)
            proxy = PacketProxy(server.getsockname()[1], Path(directory), "burst")
            try:
                proxy.configure(finite_faults=True, finite_drop_count=2,
                                finite_directions=("to_server", "to_client"), context="duplex-burst")
                for payload in (b"lost1", b"lost2", b"held", b"overtake"):
                    client.sendto(payload, ("127.0.0.1", proxy.port))
                upstream = [server.recvfrom(1024) for _ in range(3)]
                self.assertEqual([row[0] for row in upstream], [b"overtake", b"overtake", b"held"])
                self.assertFalse(proxy.finite_faults_complete(), "absent configured downstream cannot pass")
                for payload in (b"lost1", b"lost2", b"held", b"overtake"):
                    server.sendto(payload, upstream[0][1])
                self.assertEqual([client.recvfrom(1024)[0] for _ in range(3)],
                                 [b"overtake", b"overtake", b"held"])
                deadline = time.monotonic() + 2
                while not proxy.finite_faults_complete() and time.monotonic() < deadline:
                    time.sleep(0.001)
                self.assertTrue(proxy.finite_faults_complete())
                client.sendto(b"healthy-up", ("127.0.0.1", proxy.port))
                self.assertEqual(server.recvfrom(1024)[0], b"healthy-up")
                server.sendto(b"healthy-down", upstream[0][1])
                self.assertEqual(client.recvfrom(1024)[0], b"healthy-down")
            finally:
                proxy.close()
            rows = [json.loads(s) for s in (Path(directory) / "packets-burst.jsonl").read_text().splitlines()]
            for direction in ("to_server", "to_client"):
                drops = [row for row in rows if row["direction"] == direction and row["action"] == "drop"]
                self.assertEqual([row["finite_drop_index"] for row in drops], [1, 2])
                self.assertTrue(all(row["fault_context"] == "duplex-burst" for row in drops))

    def test_replacement_endpoint_keeps_queued_packets_on_original_route(self):
        with tempfile.TemporaryDirectory() as directory, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as old, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as new:
            server.bind(("127.0.0.1", 0))
            for endpoint in (server, old, new):
                endpoint.settimeout(2)
            proxy = PacketProxy(server.getsockname()[1], Path(directory), "routes")
            try:
                proxy.simulation_time = 0
                old.sendto(b"old-hello", ("127.0.0.1", proxy.port))
                payload, old_source = server.recvfrom(1024)
                self.assertEqual(payload, b"old-hello")
                proxy.configure(to_server=100, to_client=100)
                old.sendto(b"delayed-old-up", ("127.0.0.1", proxy.port))
                server.sendto(b"delayed-old-down", old_source)
                deadline = time.monotonic() + 2
                while len(proxy.schedule) < 2 and time.monotonic() < deadline:
                    time.sleep(0.001)
                self.assertEqual(len(proxy.schedule), 2)
                proxy.configure()
                new.sendto(b"new-hello", ("127.0.0.1", proxy.port))
                payload, new_source = server.recvfrom(1024)
                self.assertEqual(payload, b"new-hello")
                self.assertNotEqual(new_source, old_source)
                server.sendto(b"new-response", new_source)
                self.assertEqual(new.recvfrom(1024)[0], b"new-response")
                proxy.simulation_time = 0.2
                self.assertEqual(server.recvfrom(1024), (b"delayed-old-up", old_source))
                self.assertEqual(old.recvfrom(1024)[0], b"delayed-old-down")
                # A late datagram from the old endpoint keeps its old source;
                # it cannot switch the replacement route back to the old client.
                old.sendto(b"late-old", ("127.0.0.1", proxy.port))
                self.assertEqual(server.recvfrom(1024), (b"late-old", old_source))
                server.sendto(b"still-new", new_source)
                self.assertEqual(new.recvfrom(1024)[0], b"still-new")
                new.settimeout(0.03)
                with self.assertRaises(socket.timeout):
                    new.recvfrom(1024)
            finally:
                proxy.close()
            rows = [json.loads(s) for s in (Path(directory) / "packets-routes.jsonl").read_text().splitlines()]
            self.assertEqual(len([row for row in rows if row["action"] == "route-open"]), 2)
            self.assertTrue(all(row.get("route_generation") in (1, 2) for row in rows))

    def test_finite_fault_witnesses_never_cross_endpoint_routes(self):
        with tempfile.TemporaryDirectory() as directory, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as old, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as new:
            server.bind(("127.0.0.1", 0)); server.settimeout(2)
            proxy = PacketProxy(server.getsockname()[1], Path(directory), "route-faults")
            try:
                proxy.configure(finite_faults=True, context="route-local")
                for endpoint, prefix in ((old, b"old"), (new, b"new")):
                    endpoint.sendto(prefix + b"-lost", ("127.0.0.1", proxy.port))
                    endpoint.sendto(prefix + b"-held", ("127.0.0.1", proxy.port))
                deadline = time.monotonic() + 2
                while len(proxy.routes) < 2 and time.monotonic() < deadline:
                    time.sleep(0.001)
                self.assertEqual(len(proxy.routes), 2)
                old.sendto(b"old-overtake", ("127.0.0.1", proxy.port))
                old_rows = [server.recvfrom(1024) for _ in range(3)]
                self.assertEqual([row[0] for row in old_rows], [b"old-overtake", b"old-overtake", b"old-held"])
                self.assertEqual(len({row[1] for row in old_rows}), 1)
                self.assertFalse(proxy.finite_faults_complete())
                new.sendto(b"new-overtake", ("127.0.0.1", proxy.port))
                new_rows = [server.recvfrom(1024) for _ in range(3)]
                self.assertEqual([row[0] for row in new_rows], [b"new-overtake", b"new-overtake", b"new-held"])
                self.assertEqual(len({row[1] for row in new_rows}), 1)
                self.assertNotEqual(old_rows[0][1], new_rows[0][1])
                deadline = time.monotonic() + 2
                while not proxy.finite_faults_complete() and time.monotonic() < deadline:
                    time.sleep(0.001)
                self.assertTrue(proxy.finite_faults_complete())
            finally:
                proxy.close()

    def test_late_old_downstream_cannot_invent_upstream_witness(self):
        with tempfile.TemporaryDirectory() as directory, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as old, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as new:
            server.bind(("127.0.0.1", 0)); server.settimeout(2); old.settimeout(2)
            proxy = PacketProxy(server.getsockname()[1], Path(directory), "late-direction")
            try:
                old.sendto(b"old-hello", ("127.0.0.1", proxy.port))
                _, old_source = server.recvfrom(1024)
                new.sendto(b"new-hello", ("127.0.0.1", proxy.port))
                server.recvfrom(1024)
                proxy.configure(finite_faults=True, context="new-upstream")
                self.assertFalse(proxy.finite_faults_complete())
                for payload in (b"lost", b"held", b"overtake"):
                    new.sendto(payload, ("127.0.0.1", proxy.port))
                self.assertEqual([server.recvfrom(1024)[0] for _ in range(3)],
                                 [b"overtake", b"overtake", b"held"])
                deadline = time.monotonic() + 2
                while not proxy.finite_faults_complete() and time.monotonic() < deadline:
                    time.sleep(0.001)
                self.assertTrue(proxy.finite_faults_complete())
                server.sendto(b"late-old-response", old_source)
                self.assertEqual(old.recvfrom(1024)[0], b"late-old-response")
                self.assertTrue(proxy.finite_faults_complete(), "unconfigured old downstream is harmless")
                # Genuine old upstream activity DOES start an independent witness.
                old.sendto(b"old-lost", ("127.0.0.1", proxy.port))
                deadline = time.monotonic() + 2
                while proxy.finite_faults_complete() and time.monotonic() < deadline:
                    time.sleep(0.001)
                self.assertFalse(proxy.finite_faults_complete())
                for payload in (b"old-held", b"old-overtake"):
                    old.sendto(payload, ("127.0.0.1", proxy.port))
                self.assertEqual([server.recvfrom(1024) for _ in range(3)],
                                 [(b"old-overtake", old_source), (b"old-overtake", old_source),
                                  (b"old-held", old_source)])
                deadline = time.monotonic() + 2
                while not proxy.finite_faults_complete() and time.monotonic() < deadline:
                    time.sleep(0.001)
                self.assertTrue(proxy.finite_faults_complete())
            finally:
                proxy.close()

    def test_pending_start_waits_for_public_success_without_status_words(self):
        from room_response_contract import await_successful_start
        observed = iter([
            {"active_match": False, "status": "準備中", "match": "older-selected-timeline"},
            {"active_match": True, "status": "", "match": "older-selected-timeline"},
        ])
        class Group:
            def events(self, role):
                raise AssertionError("unrelated historical events must not establish success")
            def wait_for(self, condition, label, timeout):
                self_outer.assertEqual(timeout, 30)
                self_outer.assertFalse(condition())
                self_outer.assertTrue(condition())
        self_outer = self
        before = {"match": "older-selected-timeline", "frame": 5}
        pending = annotate_response({"status": "準備中", "active_match": False}, before, "start", {})
        def observe(role, operation):
            self.assertEqual((role, operation), ("server", "observe"))
            return next(observed)
        actual = await_successful_start(Group(), "server", pending, observe)
        self.assertTrue(response_matches(actual, "accepted"))
        self.assertEqual(actual["match"], "older-selected-timeline")
        self.assertEqual(actual["_before"], before)

    def test_immediate_success_does_not_wait(self):
        from room_response_contract import await_successful_start
        started = annotate_response({"active_match": True, "status": ""}, {}, "start", {})
        def unexpected(*args):
            self.fail("already successful start must not require another observation")
        self.assertIs(await_successful_start(None, "server", started, unexpected), started)

    def test_pending_start_never_becomes_success_by_diagnostic_text(self):
        from room_response_contract import await_successful_start
        class Group:
            def wait_for(self, condition, label, timeout):
                self_outer.assertEqual(timeout, 2)
                self_outer.assertFalse(condition())
                raise TimeoutError(label)
        self_outer = self
        pending = annotate_response({"active_match": False, "status": "accepted"}, {}, "start", {})
        with self.assertRaises(TimeoutError):
            await_successful_start(Group(), "server", pending,
                                   lambda *args: {"active_match": False, "status": "accepted"}, timeout=2)


if __name__ == "__main__":
    unittest.main()

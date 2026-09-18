"""Delayed fixture receipts reproduce the live160/start versus paused103 baseline bug."""
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

import room_navigation_fuzz
from room_fuzz_batch import run_batch


class BatchComplete(BaseException):
    """Stop after the scheduling seam, before unrelated battle/proxy scenarios."""


class DelayedReceipts:
    def __init__(self, directory, malicious=False):
        self.directory, self.malicious = directory, malicious
        self.proxies = [None, None, SimpleNamespace(configure=lambda **_: None)]
        self.rows, self.pending, self.trace, self.queue_nonces = {}, {}, [], set()

    def events(self, role):
        return self.rows.setdefault(role, [dict(type="delivery", match="m", frame=103, mode="paused",
            edge=160, ack=20, status="accepted", roster=["host", role], membership=role,
            assignment="", offer="", member_role="spectator", locked=False, paused=False)])

    def send(self, role, op, **values):
        identifier = len(self.pending) + 1
        self.pending[identifier] = (role, values)
        self.trace.append(("send", role, values["operation"], values["nonce"]))
        return identifier

    def reply(self, role, identifier):
        sent_role, values = self.pending[identifier]
        assert sent_role == role
        operation, nonce = values["operation"], values["nonce"]
        self.trace.append(("reply", role, operation, nonce))
        before = next(row for row in reversed(self.events(role)) if row["type"] == "delivery")
        delivery = dict(before, nonce=nonce)
        if operation == "live":
            delivery.update(frame=160, mode="live")
        elif operation == "start":
            delivery["status"] = "unauthorized"
            if self.malicious:
                delivery.update(frame=159, mode="paused")
        elif operation == "queue" and nonce not in self.queue_nonces:
            self.queue_nonces.add(nonce)
            delivery["ack"] += 1
        self.events(role).extend([delivery, dict(type="presentation", nonce=nonce,
            frame=delivery["frame"], x=delivery["frame"], health=10000)])

    def wait_for(self, condition, label, timeout):
        assert timeout == 30
        assert condition(), label


class FuzzRoleOrderTests(unittest.TestCase):
    def scenario(self, malicious=False):
        with tempfile.TemporaryDirectory() as temporary:
            group = DelayedReceipts(Path(temporary), malicious)
            roles = ["fighter0", "fighter1", "client2restart", "client3", "client4"]
            actions = [dict(seed=24001, step=step, role=role, choice=choice, number=0)
                       for step, (role, choice) in enumerate([(0, 3), (1, 7), (0, 9)])]
            batches = []

            def check(condition, label):
                self.assertTrue(condition, label)

            def execute(batch, *args):
                batches.append([action["choice"] for action in batch])
                run_batch(batch, *args)
                if sum(map(len, batches)) == len(actions):
                    raise BatchComplete()

            with patch.object(room_navigation_fuzz, "run_batch", side_effect=execute):
                with self.assertRaises(BatchComplete):
                    room_navigation_fuzz.run_fuzz(group,
                        lambda role, operation: group.events(role)[-1], check, roles, "m", ["a", "b"],
                        {103: (103, 10000), 160: (160, 10000)}, lambda _: None, {},
                        plan=dict(format=1, delay=120, seeds=[24001], actions=actions, inputs=[]))
            observations = [json.loads(line) for line in
                            (group.directory / "fuzz-observations.jsonl").read_text().splitlines()]
            return group.trace, batches, observations

    def test_delayed_same_role_receipt_precedes_next_action_without_losing_cross_role_batch(self):
        trace, batches, observations = self.scenario()
        self.assertEqual(batches, [[3, 7], [9]])
        self.assertEqual([(phase, role, op) for phase, role, op, _ in trace[:3]],
                         [("send", "client2restart", "live"), ("send", "client3", "queue"),
                          ("send", "client3", "queue")])
        self.assertEqual(trace[1][3], trace[2][3], "duplicate queue preserves its one nonce")
        start_send = next(i for i, row in enumerate(trace) if row[:3] == ("send", "client2restart", "start"))
        live_reply = next(i for i, row in enumerate(trace) if row[:3] == ("reply", "client2restart", "live"))
        self.assertGreater(start_send, live_reply)
        self.assertEqual([row["action"]["choice"] for row in observations], [3, 7, 9])
        self.assertEqual(observations[-1]["actual"]["frame"], 160)
        self.assertEqual(observations[-1]["actual"]["mode"], "live")

    def test_rejected_start_that_changes_cursor_or_mode_still_fails_existing_outcome_check(self):
        with self.assertRaisesRegex(AssertionError, "action 9 outcome"):
            self.scenario(malicious=True)


if __name__ == "__main__":
    unittest.main()

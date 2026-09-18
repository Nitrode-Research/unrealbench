"""Synthetic image/clock checks of external reader timing, not Unreal evidence."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

import room_render_review
import room_ui_review
from test_room_render_review import bmp


def png(path):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(bytes([0, 40, 0, 0]))) + chunk(b"IEND", b""))
    return str(path)


class ObserverDeadlineTests(unittest.TestCase):
    def run_review(self, render, actual, arrival):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            reviews = root / "trusted"
            module = room_render_review if render else room_ui_review
            if render:
                evidence = dict(groups={"a": [bmp(root / "a.bmp")], "b": [bmp(root / "b.bmp")]},
                                requirements=[dict(field="same_gameplay", groups=["a", "b"], expected=True)])
            else:
                evidence = [dict(field="buffering", image=png(root / "widget.png"), expected=True)]
            clock = [0.0]
            sent = [False]

            def sleep(seconds):
                clock[0] += seconds
                if arrival is not None and clock[0] >= arrival and not sent[0]:
                    requests = list(reviews.glob("*.request.json"))
                    self.assertEqual(len(requests), 1)
                    request = json.loads(requests[0].read_text())
                    self.assertNotIn("expected", request)
                    response = dict(request_sha256=request["request_sha256"], field=request["field"],
                                    readable=True, actual=actual, evidence="Synthetic first verdict.")
                    destination = requests[0].with_name(requests[0].name.replace(".request.json", ".observation.json"))
                    destination.write_text(json.dumps(response))
                    sent[0] = True

            with patch.object(module.time, "monotonic", side_effect=lambda: clock[0]), \
                    patch.object(module.time, "sleep", side_effect=sleep):
                passed = module.evaluate(evidence, root / "output", reviews)
            return passed, clock[0]

    def test_valid_first_verdict_after_old_deadline_keeps_its_value(self):
        for render in (False, True):
            for actual in (False, True):
                with self.subTest(render=render, actual=actual):
                    passed, elapsed = self.run_review(render, actual, 50)
                    self.assertEqual(passed, actual)
                    self.assertEqual(elapsed, 50)

    def test_missing_observer_still_fails_at_one_global_ninety_second_deadline(self):
        for render in (False, True):
            with self.subTest(render=render):
                passed, elapsed = self.run_review(render, True, None)
                self.assertFalse(passed)
                self.assertEqual(elapsed, 90)


if __name__ == "__main__":
    unittest.main()

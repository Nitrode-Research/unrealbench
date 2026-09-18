import unittest

from room_response_contract import annotate_response, response_matches, seek_then_pause


class SeekContractTests(unittest.TestCase):
    def seek(self, **changes):
        reply = dict(match="retained", frame=12, mode="playing", ack=3)
        reply.update(changes)
        return annotate_response(reply, {}, "seek", dict(match="retained", number=12))

    def test_seek_can_preserve_playing_mode(self):
        self.assertTrue(response_matches(self.seek(), "accepted"))

    def test_seek_still_requires_exact_destination(self):
        self.assertFalse(response_matches(self.seek(frame=13), "accepted"))
        self.assertFalse(response_matches(self.seek(match="other"), "accepted"))

    def test_explicit_playback_commands_keep_mode_obligations(self):
        for operation, mode in (("pause", "paused"), ("resume", "playing"), ("live", "live")):
            good = annotate_response(dict(mode=mode), {}, operation, {})
            bad = annotate_response(dict(mode="wrong"), {}, operation, {})
            self.assertTrue(response_matches(good, "accepted"))
            self.assertFalse(response_matches(bad, "accepted"))


class SeekPauseBoundaryTests(unittest.TestCase):
    def run_boundary(self, seek_mode, paused_frame=14, paused_mode="paused"):
        operations = []
        def room(role, operation, **args):
            operations.append(operation)
            data = dict(match="retained", frame=12 if operation == "seek" else paused_frame,
                        mode=seek_mode if operation == "seek" else paused_mode, edge=20)
            return annotate_response(data, {}, operation, args)
        sought, paused = seek_then_pause(room, self.assertTrue, "viewer", "retained", 12, 20)
        self.assertEqual(operations, ["seek", "pause"])
        return sought, paused

    def test_preserving_or_resuming_seek_allows_intervening_playback(self):
        for mode in ("paused", "playing", "live"):
            with self.subTest(mode=mode):
                expected_pause = 12 if mode == "paused" else 14
                sought, paused = self.run_boundary(mode, expected_pause)
                self.assertEqual(sought["frame"], 12)
                self.assertEqual(paused["frame"], expected_pause)

    def test_paused_baseline_must_stay_within_released_forward_range(self):
        for frame in (11, 21):
            with self.subTest(frame=frame), self.assertRaises(AssertionError):
                self.run_boundary("playing", frame)
        with self.assertRaises(AssertionError):
            self.run_boundary("playing", 14, "playing")
        with self.assertRaises(AssertionError):
            self.run_boundary("paused", 14)


if __name__ == "__main__":
    unittest.main()

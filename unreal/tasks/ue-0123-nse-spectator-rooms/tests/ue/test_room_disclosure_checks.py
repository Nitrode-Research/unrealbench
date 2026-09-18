import unittest

from room_disclosure_checks import active_delivery_disclosure_errors


class DisclosureChecks(unittest.TestCase):
    def errors(self, delivery, tick=130):
        return active_delivery_disclosure_errors(
            delivery, tick, 120, {0: 0, 1: 10, 2: 20},
            {0: (0, 0), 1: (8, 4), 2: (32, 0)})

    def test_buffered_payload_cannot_hide_behind_negative_cursor(self):
        for field in ("gameplay", "replay", "outcome"):
            with self.subTest(field=field):
                self.assertTrue(self.errors(dict(frame=-1, **{field: "secret"})))

    def test_buffered_input_fields_allow_non_gameplay_sentinels(self):
        for marker in (-1, -2):
            self.assertEqual(self.errors(dict(frame=-1, input1=marker, input2=marker)), [])

    def test_exact_release_boundary(self):
        released = dict(frame=1, gameplay="encoded", input1=8, input2=4)
        self.assertTrue(self.errors(released, tick=129))
        self.assertEqual(self.errors(released, tick=130), [])

    def test_released_inputs_cannot_be_replaced_with_future_pair(self):
        self.assertTrue(self.errors(dict(frame=1, gameplay="encoded", input1=32, input2=0)))

    def test_omitted_gameplay_acknowledgement_is_allowed(self):
        self.assertEqual(self.errors(dict(frame=1, input1=-1, input2=-1)), [])

    def test_lobby_without_clock_sample_is_allowed(self):
        self.assertEqual(self.errors(dict(frame=-1), tick=None), [])

    def test_initialized_frame_does_not_require_an_input_pair(self):
        self.assertEqual(self.errors(dict(frame=0, gameplay="initial", input1=-1, input2=-1)), [])


if __name__ == "__main__":
    unittest.main()

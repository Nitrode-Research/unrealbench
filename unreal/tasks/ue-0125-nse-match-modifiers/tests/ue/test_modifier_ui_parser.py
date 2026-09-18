"""Presentation-format regressions for the native HUD observation parser."""

import unittest
from modifier_ggpo_peers import visible_durations


class VisibleDurationFormats(unittest.TestCase):
    def test_equivalent_layouts(self):
        expected = {"Drain": 3, "Restriction": 1}
        for text in (
            "Drain | 3 frames\nRestriction | 1 frames",
            "Drain — 3 frames\nRestriction — 1 frame",
            "3 frames Drain\n1 frame Restriction",
            "Drain\n3 frames\nRestriction\n1 frame",
            "3 frames\nDrain\n1 frame\nRestriction",
            "Drain\n3 frames\n1 frame\nRestriction",
            "1 frame\nRestriction\n3 frames\nDrain",
        ):
            with self.subTest(text=text):
                self.assertTrue(visible_durations(text, expected))

    def test_revision_and_round_metadata_are_not_durations(self):
        expected = {"Drain": 3, "Restriction": 1}
        for text in (
            "Drain rev 7, round 2: 3 frames\nRestriction revision 8, round 2: 1 frame",
            "Drain (v7) 3 frames\nRestriction (v8) 1 frame",
            "Drain round 2: 3\nRestriction round 2: 1",
        ):
            with self.subTest(text=text):
                self.assertTrue(visible_durations(text, expected))
        self.assertFalse(visible_durations(
            "Drain rev 3: 8 frames\nRestriction rev 1: 9 frames", expected))

    def test_wrong_values_missing_values_and_inactive_labels(self):
        expected = {"Drain": 3, "Restriction": 1}
        for text in (
            "Drain | 1 frame\nRestriction | 3 frames",
            "1 frame\nDrain\n3 frames\nRestriction",
            "Drain\n1 frame\nRestriction\n3 frames",
            "Drain\n3 frames\nRestriction",
            "Drain | 3 frames\nRestriction | 1 frame\nShots | 4 frames",
        ):
            with self.subTest(text=text):
                self.assertFalse(visible_durations(text, expected))

    def test_one_number_cannot_describe_two_separate_controls(self):
        self.assertFalse(
            visible_durations(
                "Drain\n3 frames\nRestriction", {"Drain": 3, "Restriction": 3}
            )
        )


if __name__ == "__main__":
    unittest.main()

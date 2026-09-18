import unittest

from modifier_ggpo_peers import gameplay_outcome, changed_input_frames


class CorrectionObservations(unittest.TestCase):
    def test_input_change_alone_is_not_gameplay_correction(self):
        before = ["20", "0", "0", "0", "1", "19", "health:1000", "hud:Drain"]
        after = ["20", "16", "0", "1", "1", "19", "health:1000", "hud:Drain"]
        self.assertEqual(gameplay_outcome(before), gameplay_outcome(after))

    def test_changed_health_is_a_gameplay_correction(self):
        before = ["20", "0", "0", "0", "1", "19", "health:1000"]
        after = ["20", "16", "0", "1", "1", "19", "health:979"]
        self.assertNotEqual(gameplay_outcome(before), gameplay_outcome(after))


class BoundaryInputObservations(unittest.TestCase):
    def test_ignored_button_can_correct_without_changing_outcome(self):
        rows = [["9", "0", "0", "0"], ["9", "0", "64", "1"]]
        self.assertEqual(changed_input_frames(rows), {9})

    def test_repetition_and_callback_flag_do_not_prove_input_correction(self):
        rows = [["9", "0", "64", "0"], ["9", "0", "64", "1"]]
        self.assertEqual(changed_input_frames(rows), set())

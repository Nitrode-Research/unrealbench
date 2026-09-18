import unittest
from room_input_challenges import CHALLENGE_FRAMES, INPUT_DOMAIN_ONE, INPUT_DOMAIN_TWO, direct_copy_evidence, make_initial_inputs


class InputChallenges(unittest.TestCase):
    def rows(self):
        return [dict(frame=frame, submitted=[INPUT_DOMAIN_ONE[index % 4], INPUT_DOMAIN_TWO[index // 4 % 4]],
                     observed=[-index - 1, -index - 2]) for index, frame in enumerate(CHALLENGE_FRAMES)]

    def test_arbitrary_changing_sentinels_do_not_fail(self):
        self.assertFalse(direct_copy_evidence(self.rows())["suspected_direct_copy"])

    def test_direct_copy_of_hidden_pairs_is_detected(self):
        rows = self.rows()
        for row in rows:
            row["observed"] = list(row["submitted"])
        self.assertTrue(direct_copy_evidence(rows)["suspected_direct_copy"])

    def test_one_copied_hidden_channel_is_detected(self):
        rows = self.rows()
        for row in rows:
            row["observed"][0] = row["submitted"][0]
        self.assertTrue(direct_copy_evidence(rows)["suspected_direct_copy"])

    def test_one_coincidence_does_not_fail(self):
        rows = self.rows()
        rows[0]["observed"] = list(rows[0]["submitted"])
        self.assertFalse(direct_copy_evidence(rows)["suspected_direct_copy"])

    def test_declared_false_positive_bound_is_small(self):
        self.assertLess(direct_copy_evidence(self.rows())["accidental_match_bound"], 1e-16)

    def test_missing_or_repeated_challenges_are_not_evidence(self):
        with self.assertRaises(ValueError):
            direct_copy_evidence(self.rows()[:-1])
        rows = self.rows()
        rows[-1]["frame"] = rows[0]["frame"]
        with self.assertRaises(ValueError):
            direct_copy_evidence(rows)

    def test_schedule_preserves_count_and_projectile_witnesses(self):
        pairs = make_initial_inputs()
        self.assertEqual(len(pairs), 160)
        self.assertTrue(pairs[19][0] & 64)
        self.assertTrue(pairs[119][0] & 64)
        for frame in CHALLENGE_FRAMES:
            self.assertIn(pairs[frame - 1][0], INPUT_DOMAIN_ONE)
            self.assertIn(pairs[frame - 1][1], INPUT_DOMAIN_TWO)


if __name__ == "__main__":
    unittest.main()

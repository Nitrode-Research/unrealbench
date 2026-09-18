"""Python-only checks for semantic HUD evidence parsing."""
import unittest

from relay_rendered_sample import verify_painted_hud


class PaintedHudTests(unittest.TestCase):
    def rows(self):
        return [dict(identity=name, slot=str(slot), health="100", main=str(slot == 1 and 1 or 0),
                     visible=str(slot == 1 and 1 or 0), recoverable="0", cooldown="0",
                     eligible=str(slot != 1 and 1 or 0), resource="200", rejection="None")
                for _ in range(2) for slot, name in enumerate(("Vanguard", "Heavy", "Light"), 1)]

    def painted(self):
        return "\n".join(
            f"Team {team}\nResource: 200\n"
            "1 Vanguard active health: 100 recovery: 0 cooldown: 0 not ready\n"
            "2 Heavy bench health: 100 recovery: 0 cooldown: 0 available\n"
            "3 Light standby health: 100 recovery: 0 cooldown: 0 eligible"
            for team in (1, 2)
        )

    def check(self, value, message):
        self.assertTrue(value, message)

    def test_semantic_aliases_and_negative_readiness(self):
        verify_painted_hud(self.painted(), self.rows(), self.check)

    def test_false_health_still_fails(self):
        with self.assertRaises(AssertionError):
            verify_painted_hud(self.painted().replace("health: 100", "health: 99", 1), self.rows(), self.check)

    def test_false_positive_readiness_still_fails(self):
        with self.assertRaises(AssertionError):
            verify_painted_hud(self.painted().replace("not ready", "ready", 1), self.rows(), self.check)

    def test_wrong_role_still_fails(self):
        with self.assertRaises(AssertionError):
            verify_painted_hud(self.painted().replace("Vanguard active", "Vanguard bench", 1), self.rows(), self.check)


class HudObservationTests(unittest.TestCase):
    def test_numeric_observation_is_exact(self):
        from relay_rendered_sample import compare_hud_number
        self.assertTrue(compare_hud_number(500, 500, 10000))
        self.assertFalse(compare_hud_number(501, 500, 10000))
        self.assertFalse(compare_hud_number(True, 1, 200))
        self.assertFalse(compare_hud_number(None, 0, 200))

    def test_graphical_observation_allows_pixel_quantization(self):
        from relay_rendered_sample import compare_hud_number
        self.assertTrue(compare_hud_number({"fraction": .505, "resolution_pixels": 200}, 5000, 10000))
        self.assertFalse(compare_hud_number({"fraction": .7, "resolution_pixels": 200}, 5000, 10000))
        self.assertFalse(compare_hud_number({"fraction": 0, "resolution_pixels": 200}, 100, 10000))
        self.assertFalse(compare_hud_number({"fraction": .01, "resolution_pixels": 200}, 0, 10000))
        self.assertFalse(compare_hud_number({"fraction": float("nan"), "resolution_pixels": 200}, 0, 10000))

    def test_blind_packet_hides_actual_state_and_binds_images(self):
        import json
        import pathlib
        import tempfile
        from relay_rendered_sample import write_hud_review_packet
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            (directory / "frame-00.png").write_bytes(b"test image bytes")
            write_hud_review_packet(directory, {0: [{"health": "4321"}]})
            packet_text = (directory / "hud-review-packet.json").read_text()
            self.assertNotIn("4321", packet_text)
            packet = json.loads(packet_text)
            self.assertEqual(len(packet["frames"][0]["sha256"]), 64)
            self.assertEqual(json.loads((directory / "hud-evaluator-state.json").read_text())["0"][0]["health"], "4321")

    def test_review_rejects_wrong_hash_missing_field_and_wrong_value(self):
        import copy
        import json
        import pathlib
        import tempfile
        from relay_rendered_sample import write_hud_review_packet, verify_hud_review
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            (directory / "frame-00.png").write_bytes(b"test image bytes")
            rows = PaintedHudTests().rows()
            write_hud_review_packet(directory, {0: rows})
            packet = json.loads((directory / "hud-review-packet.json").read_text())
            response = {"frames": [{"frame": 0, "sha256": packet["frames"][0]["sha256"],
                "evidence": "Each team shows its three slot rows beside the resource meter.",
                "teams": [{"team": team, "resource": 200, "rejection": "none",
                    "slots": [{"slot": slot, "role": "main" if slot == 1 else "reserve",
                        "health": 100, "recoverable": 0, "cooldown": 0, "eligible": slot != 1}
                        for slot in (1, 2, 3)]} for team in (1, 2)]}]}
            path = directory / "observations.json"
            def check(value, message):
                self.assertTrue(value, message)
            path.write_text(json.dumps(response))
            verify_hud_review(directory, path, check)
            for defect in ("hash", "missing", "value"):
                bad = copy.deepcopy(response)
                if defect == "hash":
                    bad["frames"][0]["sha256"] = "wrong"
                elif defect == "missing":
                    del bad["frames"][0]["teams"][0]["slots"][0]["health"]
                else:
                    bad["frames"][0]["teams"][0]["slots"][0]["health"] = 99
                path.write_text(json.dumps(bad))
                with self.assertRaises(AssertionError):
                    verify_hud_review(directory, path, check)


class InputDeliveryEvidenceTests(unittest.TestCase):
    def verify(self, rows):
        import pathlib
        import tempfile
        from relay_ggpo_peers import verify_selected_input_delivery
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            (directory / "host-delivery.csv").write_text(rows)
            (directory / "client-delivery.csv").write_text("")
            verify_selected_input_delivery(directory, "staggered", self.assertTrue)

    def evidence(self, first_lateness=7):
        rows = []
        for window in (60, 64, 78, 91):
            input_frame = window + 2
            lateness = first_lateness if window == 60 else 5
            release = input_frame + lateness
            start = release if window == 60 else input_frame
            rows += [f"hold,{window},{start},{release},{input_frame},4194304",
                     f"release,{window},{start},{release},{lateness},{release - start}"]
        return "\n".join(rows) + "\n"

    def test_already_late_batch_can_release_without_simulation_advancing(self):
        self.verify(self.evidence(7))
        self.verify(self.evidence(8))

    def test_delivery_outside_bound_fails(self):
        with self.assertRaises(AssertionError):
            self.verify(self.evidence(9))

    def test_release_without_actual_hold_fails(self):
        with self.assertRaises(AssertionError):
            self.verify("\n".join(self.evidence().splitlines()[1:]) + "\n")

    def test_missing_selected_window_fails(self):
        with self.assertRaises(AssertionError):
            self.verify("\n".join(self.evidence().splitlines()[2:]) + "\n")


if __name__ == "__main__":
    unittest.main()

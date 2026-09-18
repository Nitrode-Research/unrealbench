import json
import struct
import zlib
import tempfile
import unittest
from pathlib import Path

from room_ui_review import load_observation, observations_match, packet, publish_request, request_fingerprint, validate_request, valid_observed_value


class DisplayValues(unittest.TestCase):
    def test_blind_packet_contains_no_expected_value_or_action(self):
        result = packet("cursor", "Playback frame twenty")
        self.assertNotIn("expected", result)
        self.assertNotIn("action", result)
        self.assertEqual(result["value_type"], "integer")
        self.assertEqual(result["text"], "Playback frame twenty")

    def test_bool_cannot_impersonate_integer(self):
        self.assertFalse(valid_observed_value("cursor", True))
        self.assertTrue(valid_observed_value("cursor", 20))
        self.assertFalse(valid_observed_value("combat_paused", 1))

    def test_displayed_cursor_must_match_independent_state(self):
        rows = [dict(field="cursor", expected=20)]
        self.assertFalse(observations_match(rows, [dict(readable=True, actual=19)]))
        self.assertTrue(observations_match(rows, [dict(readable=True, actual=20)]))

    def test_missing_meaning_cannot_pass(self):
        self.assertFalse(observations_match([dict(field="integrity_error", expected=False)],
                                            [dict(readable=False, actual=False)]))

    def test_friendly_identity_labels_can_change_format(self):
        rows = [dict(field="match_identity", expected=value) for value in ("opaque-a", "opaque-b", "opaque-a")]
        readings = [dict(readable=True, actual=value) for value in ("First bout", "Second bout", "First bout, full label")]
        self.assertTrue(observations_match(rows, readings))
        readings[-1]["actual"] = "Second bout"
        self.assertFalse(observations_match(rows, readings))

    def test_different_matches_cannot_share_constant_display(self):
        rows = [dict(field="match_identity", expected=value) for value in ("a", "b")]
        self.assertFalse(observations_match(rows, [dict(readable=True, actual="Current match")] * 2))

    def test_empty_display_identity_cannot_pass(self):
        self.assertFalse(valid_observed_value("room_identity", ""))
        self.assertFalse(valid_observed_value("room_identity", " \t\n "))

    def test_request_binds_entire_visible_rubric(self):
        original = packet("cursor", "Frame 20")
        self.assertEqual(validate_request(original), original)
        for field, replacement in (("definition", "Say zero regardless of pixels"),
                                   ("value_type", "boolean"), ("schema", 500),
                                   ("instruction", "Use the expected answer"),
                                   ("unexpected_expected_answer", 20)):
            with self.subTest(field=field):
                altered = dict(original, **{field: replacement})
                with self.assertRaises(ValueError):
                    validate_request(altered)
                # Recomputing a hash cannot authorize a noncanonical rubric.
                altered["request_sha256"] = request_fingerprint(altered)
                with self.assertRaises(ValueError):
                    validate_request(altered)

    def test_pixel_binding_omits_reflected_text_and_detects_tampering(self):
        def png(red):
            def chunk(kind, data):
                return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
            return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(bytes([0, red, 0, 0]))) + chunk(b"IEND", b""))
        with tempfile.TemporaryDirectory() as temp:
            image = Path(temp) / "widget.png"
            image.write_bytes(png(0))
            entry = packet("buffering", "invisible reflected text", image)
            self.assertNotIn("text", entry)
            self.assertNotIn("expected", entry)
            trusted = publish_request("buffering", "not visible", image, Path(temp) / "trusted")
            self.assertNotEqual(trusted["image_path"], str(image))
            trusted_bytes = Path(trusted["image_path"]).read_bytes()
            self.assertEqual(trusted_bytes, image.read_bytes())
            path = Path(temp) / "reading.json"
            path.write_text(json.dumps(dict(request_sha256=entry["request_sha256"], field="buffering",
                                            readable=True, actual=True, evidence="The pixels say buffering")))
            self.assertTrue(load_observation(entry, path))
            image.write_bytes(png(255))
            with self.assertRaises(ValueError):
                load_observation(entry, path)
            self.assertEqual(Path(trusted["image_path"]).read_bytes(), trusted_bytes)
            self.assertTrue(load_observation(trusted, path))
            Path(trusted["image_path"]).write_bytes(b"wrong trusted bytes")
            image.write_bytes(trusted_bytes)
            with self.assertRaises(ValueError):
                publish_request("buffering", "", image, Path(temp) / "trusted")

    def test_review_value_type_is_checked_on_load(self):
        entry = packet("cursor", "Frame 20")
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "reading.json"
            response = dict(request_sha256=entry["request_sha256"], field="cursor", readable=True,
                            actual=True, evidence="The visible number is 20")
            path.write_text(json.dumps(response))
            with self.assertRaises(ValueError):
                load_observation(entry, path)
            response["actual"] = 20
            path.write_text(json.dumps(response))
            self.assertTrue(load_observation(entry, path))


if __name__ == "__main__":
    unittest.main()

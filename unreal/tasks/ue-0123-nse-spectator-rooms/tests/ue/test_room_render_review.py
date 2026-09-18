"""Protocol checks use synthetic pixels and scripted verdicts, never UE evidence."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
from room_render_review import evaluate, packet, validate_request


def bmp(path, width=2, value=70):
    stride = (width * 3 + 3) // 4 * 4
    pixels = bytes([value]) * stride * 2
    header = b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, width, 2, 1, 24, 0, len(pixels), 0, 0, 0, 0)
    path.write_bytes(header + pixels)
    return str(path)


class ReviewProtocolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.reviews = self.root / "trusted"
        self.reviews.mkdir()
        self.a = bmp(self.root / "ordinary_frame15_secret.bmp")
        self.b = bmp(self.root / "candidate_seek_secret.bmp", width=4)

    def manifest(self, expected=True):
        return dict(groups={"private_action": [self.a], "private_reference": [self.b]},
                    requirements=[dict(field="same_gameplay", groups=["private_action", "private_reference"], expected=expected)])

    def response(self, request, actual=True, readable=True):
        response = dict(request_sha256=request["request_sha256"], field=request["field"],
                        actual=actual, readable=readable, evidence="Synthetic verdict only, for protocol testing.")
        (self.reviews / (request["request_sha256"] + ".observation.json")).write_text(json.dumps(response))

    def test_packet_contains_actual_images_without_private_labels_and_allows_resolution_change(self):
        request = packet("same_gameplay", [[self.a], [self.b]], self.reviews)
        text = json.dumps(request)
        for hidden in ("ordinary_frame15", "candidate_seek", '"expected":', "private_action"):
            self.assertNotIn(hidden, text)
        self.assertEqual({s[0]["width"] for s in request["sequences"]}, {2, 4})
        validate_request(request, self.reviews)
        self.assertEqual(len(list(self.reviews.glob("*.bmp"))), 2)

    def test_missing_review_fails_even_when_pixels_are_identical(self):
        manifest = self.manifest()
        manifest["groups"]["private_reference"] = [self.a]
        self.assertFalse(evaluate(manifest, self.root / "out", self.reviews, 0))

    def test_private_expectation_does_not_change_request_or_reviewed_value(self):
        request = packet("same_gameplay", [[self.a], [self.b]], self.reviews)
        self.response(request)
        self.assertTrue(evaluate(self.manifest(True), self.root / "yes", self.reviews, 0))
        self.assertFalse(evaluate(self.manifest(False), self.root / "no", self.reviews, 0))
        self.assertEqual(len(list(self.reviews.glob("*.request.json"))), 1)

    def test_unreadable_review_fails(self):
        request = packet("same_gameplay", [[self.a], [self.b]], self.reviews)
        self.response(request, None, False)
        self.assertFalse(evaluate(self.manifest(), self.root / "out", self.reviews, 0))

    def test_changed_image_fails(self):
        request = packet("same_gameplay", [[self.a], [self.b]], self.reviews)
        image = self.reviews / request["sequences"][0][0]["image"]
        image.write_bytes(image.read_bytes()[:-1])
        with self.assertRaises(ValueError):
            validate_request(request, self.reviews)

    def test_changed_field_or_wrong_verdict_binding_fails(self):
        request = packet("same_gameplay", [[self.a], [self.b]], self.reviews)
        self.response(request)
        path = self.reviews / (request["request_sha256"] + ".observation.json")
        response = json.loads(path.read_text())
        response["field"] = "visible_effects"
        path.write_text(json.dumps(response))
        with self.assertRaises(ValueError):
            evaluate(self.manifest(), self.root / "out", self.reviews, 0)
        request["definition"] = "Changed"
        with self.assertRaises(ValueError):
            validate_request(request, self.reviews)

    def test_effect_phase_requires_multiple_samples(self):
        for field, groups in (("effect_progression", [[self.a]]), ("stable_presentation", [[self.a]]),
                              ("same_effect_phase", [[self.a], [self.b]])):
            with self.assertRaises(ValueError):
                packet(field, groups, self.reviews)

    def test_stage_comparison_uses_two_image_sequences_without_frame_or_projection(self):
        for field in ("same_stage", "distinct_stages"):
            request = packet(field, [[self.a], [self.b]], self.reviews)
            self.assertEqual(len(request["sequences"]), 2)
            validate_request(request, self.reviews)
            with self.assertRaises(ValueError):
                packet(field, [[self.a]], self.reviews)

    def test_all_requests_published_before_missing_review_failure(self):
        manifest = self.manifest()
        manifest["requirements"].append(dict(field="visible_effects", groups=["private_action"], expected=True))
        self.assertFalse(evaluate(manifest, self.root / "out", self.reviews, 0))
        self.assertEqual(len(list(self.reviews.glob("*.request.json"))), 2)


if __name__ == "__main__":
    unittest.main()

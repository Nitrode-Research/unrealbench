import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from modifier_visual_reader import verify_visible


class BlindVisualReview(unittest.TestCase):
    def test_reader_receives_no_expected_values_and_is_image_bound(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / 'capture.png'
            image.write_bytes(b'independent image bytes')
            digest = hashlib.sha256(image.read_bytes()).hexdigest()
            def read(command, **kwargs):
                request = json.loads(kwargs['input'])
                self.assertNotIn('expected', request)
                self.assertNotIn('Secret rule', kwargs['input'])
                self.assertEqual(request['image_sha256'], digest)
                return subprocess.CompletedProcess(command, 0, json.dumps(dict(image_sha256=digest, readable=True, actual={'Secret rule': 17}, evidence='Visible label and graphical countdown')), '')
            with patch.dict(os.environ, {'NSE_MODIFIER_VISUAL_READER': json.dumps([sys.executable])}), patch('modifier_visual_reader.subprocess.run', side_effect=read):
                self.assertTrue(verify_visible(image, {'Secret rule': 17}))
                self.assertFalse(verify_visible(image, {'Secret rule': 18}))

    def test_missing_review_does_not_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            request = {'image_sha256': 'abc', 'field': 'active_match_rules'}
            result = subprocess.run([sys.executable, str(Path(__file__).with_name('modifier_visual_reader.py')), '--reviews', directory, '--wait-seconds', '0'], input=json.dumps(request), text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(json.loads(result.stdout)['readable'])

    def test_same_invocation_reloads_review_delivered_while_waiting(self):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(__file__).with_name('modifier_visual_reader.py')
            process = subprocess.Popen([sys.executable, str(script), '--reviews', directory, '--wait-seconds', '5'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            request = {'image_sha256': 'abc', 'field': 'active_match_rules'}
            process.stdin.write(json.dumps(request))
            process.stdin.close()
            import time
            deadline = time.monotonic() + 3
            while not (Path(directory) / 'abc.active_match_rules.request.json').exists() and time.monotonic() < deadline:
                time.sleep(.02)
            observation = dict(image_sha256='abc', readable=True, actual={'Rule': 2}, evidence='Visible graphic')
            (Path(directory) / 'abc.active_match_rules.observation.json').write_text(json.dumps(observation))
            process.wait(timeout=3)
            self.assertEqual(process.returncode, 0)
            self.assertEqual(json.loads(process.stdout.read()), observation)
            process.stdout.close()
            process.stderr.close()

"""Blind screenshot observation transport; expected values never reach the reader."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def verify_visible(image, expected):
    image = Path(image).resolve()
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    request = {
        'schema': 1, 'image': str(image), 'image_sha256': digest,
        'field': 'active_match_rules',
        'instruction': 'Read every active match modifier name and exact remaining simulation frames from the image. Any readable graphic, layout, font or label is permitted. Return actual as an object mapping visible rule names to integer remaining frames; use -1 for a rule lasting to round end. Return readable=false if this cannot be determined. Do not guess.',
    }
    raw = os.environ.get('NSE_MODIFIER_VISUAL_READER')
    if raw:
        command = json.loads(raw)
        if not isinstance(command, list) or not command or not all(isinstance(x, str) for x in command) or not Path(command[0]).is_absolute():
            raise ValueError('NSE_MODIFIER_VISUAL_READER must be an absolute grader-owned JSON argv array')
    else:
        reviews = os.environ.get('NSE_MODIFIER_VISUAL_REVIEWS')
        if not reviews:
            image.with_suffix('.visual-request.json').write_text(json.dumps(request, indent=2))
            raise RuntimeError('Blind review required; configure NSE_MODIFIER_VISUAL_REVIEWS or NSE_MODIFIER_VISUAL_READER. Request saved beside PNG.')
        command = [sys.executable, str(Path(__file__).resolve()), '--reviews', reviews, '--wait-seconds', '50']
    process = subprocess.run(command, input=json.dumps(request), text=True, capture_output=True, timeout=55, shell=False)
    response = json.loads(process.stdout)
    receipt = image.with_suffix('.visual-receipt.json')
    receipt.write_text(json.dumps({'request': request, 'response': response, 'returncode': process.returncode, 'stderr': process.stderr}, indent=2))
    if process.returncode or response.get('image_sha256') != digest or hashlib.sha256(image.read_bytes()).hexdigest() != digest:
        raise RuntimeError('Invalid or changed screenshot observation: ' + str(receipt))
    actual = response.get('actual')
    if response.get('readable') is not True or not isinstance(actual, dict) or not all(isinstance(k, str) and type(v) is int and v >= -1 for k, v in actual.items()):
        raise RuntimeError('Missing readable rule observation: ' + str(receipt))
    if not isinstance(response.get('evidence'), str) or not response['evidence'].strip():
        raise RuntimeError('Observation requires visible evidence: ' + str(receipt))
    return actual == expected


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--reviews', type=Path)
    parser.add_argument('--wait-seconds', type=float, default=50)
    parser.add_argument('--verify', type=Path)
    parser.add_argument('--name')
    parser.add_argument('--frames', type=int)
    args = parser.parse_args()
    if args.verify:
        try:
            accepted = verify_visible(args.verify, {args.name: args.frames})
            print('Verified visible state' if accepted else 'Visible state differs')
            return 0 if accepted else 1
        except Exception as error:
            print(str(error), file=sys.stderr)
            return 2
    if not args.reviews or not 0 <= args.wait_seconds <= 50:
        parser.error('reader mode requires --reviews and wait-seconds within 0..50')
    request = json.load(sys.stdin)
    key = request['image_sha256'] + '.active_match_rules'
    args.reviews.mkdir(parents=True, exist_ok=True)
    (args.reviews / (key + '.request.json')).write_text(json.dumps(request, indent=2))
    observation = args.reviews / (key + '.observation.json')
    deadline = time.monotonic() + args.wait_seconds
    while time.monotonic() <= deadline:
        try:
            value = json.loads(observation.read_text())
            if value.get('image_sha256') == request['image_sha256']:
                print(json.dumps(value))
                return 0
        except (OSError, ValueError):
            pass
        time.sleep(0.25)
    print(json.dumps({'image_sha256': request['image_sha256'], 'readable': False, 'evidence': 'Independent review missing at bounded deadline'}))
    return 2


if __name__ == '__main__':
    sys.exit(main())

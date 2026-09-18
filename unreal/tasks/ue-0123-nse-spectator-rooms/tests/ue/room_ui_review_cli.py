"""Record a grader's blind reading of captured room display text."""
import argparse
import hashlib
import json
from pathlib import Path
from room_ui_review import validate_request, valid_observed_value


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--request', type=Path, required=True)
    p.add_argument('--reviews', type=Path, required=True)
    p.add_argument('--actual', required=True, help='JSON boolean/integer/string, or unreadable')
    p.add_argument('--evidence', required=True)
    a = p.parse_args()
    request = json.loads(a.request.read_text())
    try:
        current = validate_request(request)
    except (ValueError, KeyError, OSError) as error:
        p.error(str(error))
    if not a.evidence.strip():
        p.error('Missing visible evidence')
    digest = current['request_sha256']
    try:
        actual = None if a.actual == 'unreadable' else json.loads(a.actual)
    except json.JSONDecodeError:
        p.error('Actual must be a JSON value or unreadable')
    if a.actual != 'unreadable' and not valid_observed_value(request['field'], actual):
        p.error('Actual does not match the blind request value type')
    response = dict(request_sha256=digest, field=request['field'], readable=a.actual != 'unreadable',
                    actual=actual, evidence=a.evidence)
    a.reviews.mkdir(parents=True, exist_ok=True)
    path = a.reviews / (digest + '.' + request['field'] + '.observation.json')
    if path.exists():
        p.error('Observation already exists; preserve review history')
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(response, indent=2))
    temporary.replace(path)
    print(path)


if __name__ == '__main__':
    main()

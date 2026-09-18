"""Record a trusted reviewer's reading after inspecting the supplied images."""
import argparse
import json
from pathlib import Path
from room_render_review import validate_request


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--request", type=Path, required=True)
    p.add_argument("--reviews", type=Path, required=True)
    p.add_argument("--actual", choices=["true", "false", "unreadable"], required=True)
    p.add_argument("--evidence", required=True)
    a = p.parse_args()
    request = json.loads(a.request.read_text())
    validate_request(request, a.request.parent)
    if not a.evidence.strip():
        p.error("Describe the actual visible evidence")
    response = dict(request_sha256=request["request_sha256"], field=request["field"], readable=a.actual != "unreadable",
                    actual={"true": True, "false": False, "unreadable": None}[a.actual], evidence=a.evidence)
    a.reviews.mkdir(parents=True, exist_ok=True)
    path = a.reviews / (request["request_sha256"] + ".observation.json")
    if path.exists():
        p.error("Observation already exists; preserve review history")
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(response, indent=2))
    temporary.replace(path)
    print(path)


if __name__ == "__main__":
    main()

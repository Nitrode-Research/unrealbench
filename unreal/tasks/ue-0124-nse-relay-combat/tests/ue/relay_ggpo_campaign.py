"""Run both reviewed online/replay encounters and require distinct outcomes."""

import argparse
import csv
import json
import pathlib
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("project")
    parser.add_argument("directory")
    parser.add_argument("--editor", required=True)
    parser.add_argument(
        "--editor-args",
        default="",
        help="RHI capability flags inherited from the parent editor",
    )
    args = parser.parse_args()
    output_directory = pathlib.Path(args.directory).resolve()
    output_directory.mkdir(parents=True, exist_ok=True)
    assertions = 0
    results = []
    failures = []
    process = None
    try:
        for encounter, spacing in (("corrections", 100000), ("staggered", 600000)):
            directory = output_directory / encounter
            command = [
                sys.executable,
                str(pathlib.Path(__file__).with_name("relay_ggpo_peers.py")),
                args.project,
                str(directory),
                "--editor",
                args.editor,
                "--editor-args=" + args.editor_args,
                "--encounter",
                encounter,
                "--spacing",
                str(spacing),
            ]
            (output_directory / f"{encounter}-command.json").write_text(
                json.dumps(command, indent=2)
            )
            with (output_directory / f"{encounter}-driver.log").open("w") as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
                code = process.wait(timeout=300)
            result = json.loads((directory / "result.json").read_text())
            assertions += result.get("assertions", 0)
            results.append(result)
            if code != 0 or not result.get("passed"):
                failures.append(f'{encounter}: {result.get("error","missing successful result")}')
        if failures:
            raise AssertionError("; ".join(failures))
        assertions += 1
        if results[0]["final_team_health"] == results[1]["final_team_health"]:
            raise AssertionError(
                "Distinct ordinary input encounters must produce different confirmed team health"
            )
        assertions += 1
        if results[0]["match_result"] == results[1]["match_result"]:
            raise AssertionError("Analytic alternate encounter must change the confirmed winner")
        tapes = []
        for encounter in ("corrections", "staggered"):
            with (output_directory / encounter / "host-frames.csv").open() as f:
                rows = list(csv.reader(f))
            final = {int(row[0]): row[1:3] for row in rows if row}
            tapes.append([final[f] for f in sorted(final) if f <= 410])
        assertions += 1
        if tapes[0] == tapes[1]:
            raise AssertionError("Variant must change actual recorded ordinary inputs")
        (output_directory / "result.json").write_text(
            json.dumps({"passed": True, "assertions": assertions, "encounters": results}, indent=2)
        )
        return 0
    except Exception as exc:
        (output_directory / "result.json").write_text(
            json.dumps(
                {
                    "passed": False,
                    "assertions": assertions,
                    "error": str(exc),
                    "encounters": results,
                },
                indent=2,
            )
        )
        print(str(exc), file=sys.stderr)
        return 1
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    sys.exit(main())

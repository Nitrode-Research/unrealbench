#!/usr/bin/env python3
"""Capture changed submitted files before verifier injection, without using Git.

The baseline is generated from the public starter at package time. A submission
is a changed-file overlay plus deletions against its baseline hash, not a full
project/engine copy. Generated build trees are explicitly outside this scope.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path

EXCLUDED = {".git", "Binaries", "Intermediate", "Saved", "DerivedDataCache", "node_modules", ".venv", "__pycache__"}
TEXT_SUFFIXES = {".cpp", ".h", ".hpp", ".c", ".cs", ".py", ".ini", ".json", ".toml", ".md", ".txt", ".usf", ".ush", ".uproject", ".uplugin", ".yaml", ".yml", ".sh"}


def inventory(root: Path) -> tuple[dict, list[str]]:
    if not root.is_dir():
        raise ValueError("Project must be an existing directory")
    records, errors = {}, []
    def on_error(error: OSError) -> None:
        errors.append(f"Directory scan failed: {error.filename}")

    for parent, dirs, files in os.walk(root, followlinks=False, onerror=on_error):
        for name in list(dirs):
            path = Path(parent) / name
            if name in EXCLUDED:
                dirs.remove(name)
            elif path.is_symlink() or path.resolve() != path.absolute():
                dirs.remove(name)
                errors.append(f"Unsafe directory: {path.relative_to(root).as_posix()}")
        dirs.sort()
        for name in sorted(files):
            path = Path(parent) / name
            relative = path.relative_to(root).as_posix()
            try:
                if path.is_symlink() or path.resolve() != path.absolute() or not path.is_file():
                    raise ValueError("unsafe file")
                digest = hashlib.sha256()
                with path.open("rb") as stream:
                    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                        digest.update(chunk)
                record = {"bytes": path.stat().st_size, "sha256": digest.hexdigest()}
                if path.suffix.lower() in TEXT_SUFFIXES and record["bytes"] <= 1024 * 1024:
                    try:
                        record["text"] = path.read_text(encoding="utf-8")
                    except UnicodeError:
                        pass
                records[relative] = record
            except (OSError, ValueError):
                errors.append(f"Unreadable/unsafe file: {relative}")
    return records, errors


def create_baseline(root: Path, output: Path) -> None:
    root, output = root.resolve(), output.resolve()
    if output.is_relative_to(root):
        raise ValueError("Baseline must be outside the starter")
    files, errors = inventory(root)
    if errors:
        raise ValueError("Cannot create a complete starter baseline: " + "; ".join(errors))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({"schemaVersion": "1.0", "files": files}, sort_keys=True), encoding="utf-8")


def capture(root: Path, baseline_path: Path, output: Path, max_bytes: int = 512 * 1024 * 1024) -> dict:
    root, baseline_path, output = root.resolve(), baseline_path.resolve(), output.resolve()
    if output.is_relative_to(root) or root.is_relative_to(output):
        raise ValueError("Capture destination must be separate from the project")
    if output.exists():
        raise ValueError("Capture destination already exists; do not overwrite earlier evidence")
    baseline_bytes = baseline_path.read_bytes()
    baseline = json.loads(baseline_bytes)
    if baseline.get("schemaVersion") != "1.0":
        raise ValueError("Unsupported baseline")
    original = baseline["files"]
    current, errors = inventory(root)
    # A partial scan cannot distinguish deleted files from unreadable files.
    # Do not emit a misleading overlay/deletion list in that case.
    if errors:
        raise ValueError("Cannot capture a complete project inventory: " + "; ".join(errors))
    output.mkdir(parents=True)
    changes, patches, used = [], [], 0
    for relative in sorted(original.keys() | current.keys()):
        # Validate even verifier-owned manifest paths before constructing destinations.
        if relative.startswith("/") or "\\" in relative or ":" in relative or any(part in {"", ".", ".."} for part in relative.split("/")):
            raise ValueError("Unsafe baseline path")
        before, after = original.get(relative), current.get(relative)
        if before and after and before["sha256"] == after["sha256"]:
            continue
        change = {"path": relative, "status": "deleted" if after is None else "modified" if before else "added"}
        if after:
            change.update(bytes=after["bytes"], sha256=after["sha256"], captured=False)
            if used + after["bytes"] > max_bytes:
                errors.append(f"Capture size budget exceeded: {relative}")
            else:
                destination = output / "files" / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                try:
                    source = root / relative
                    if source.is_symlink() or source.resolve() != source.absolute():
                        raise ValueError("source changed to symlink")
                    digest, size = hashlib.sha256(), 0
                    with source.open("rb") as reader, destination.open("xb") as writer:
                        for chunk in iter(lambda: reader.read(1024 * 1024), b""):
                            size += len(chunk)
                            if used + size > max_bytes:
                                raise ValueError("file grew beyond budget")
                            writer.write(chunk)
                            digest.update(chunk)
                    used += size
                    if size != after["bytes"] or digest.hexdigest() != after["sha256"]:
                        raise ValueError("file changed during capture")
                    change["captured"] = True
                except (OSError, ValueError):
                    errors.append(f"Capture failed or source changed: {relative}")
        if (before is None or "text" in before) and (after is None or "text" in after):
            old, new = (before or {}).get("text", ""), (after or {}).get("text", "")
            diff = list(difflib.unified_diff(old.splitlines(keepends=True), new.splitlines(keepends=True),
                        fromfile=f"a/{relative}" if before else "/dev/null", tofile=f"b/{relative}" if after else "/dev/null"))
            # Preserve no-newline-at-EOF information in an applyable unified patch.
            patches.extend(line if line.endswith("\n") else line + "\n\\ No newline at end of file\n" for line in diff)
            change.update(additions=sum(line.startswith("+") for line in diff[2:]),
                          deletions=sum(line.startswith("-") for line in diff[2:]))
        changes.append(change)
    manifest = {"schemaVersion": "1.0", "status": "incomplete" if errors else "complete",
                "baselineSha256": hashlib.sha256(baseline_bytes).hexdigest(),
                "excludedDirectories": sorted(EXCLUDED), "changes": changes, "errors": errors}
    (output / "model.patch").write_text("".join(patches), encoding="utf-8", newline="\n")
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["baseline", "capture"])
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.mode == "baseline":
        create_baseline(args.project, args.baseline)
        return 0
    if not args.output:
        parser.error("capture requires --output")
    return 0 if capture(args.project, args.baseline, args.output)["status"] == "complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())

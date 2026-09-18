import hashlib, json, pathlib, sys

project = pathlib.Path(sys.argv[1])
baseline = pathlib.Path(sys.argv[2])
package = pathlib.Path(sys.argv[3])
inventory = json.loads((package / "REFERENCE_INVENTORY.json").read_text())
protected = json.loads((package / "PROTECTED_PATHS.json").read_text())
excluded = {"Binaries", "Intermediate", "Saved", "DerivedDataCache", ".git", "__pycache__"}

def digest(path):
    data = path.read_bytes()
    if path.suffix.lower() in {".cpp", ".h", ".cs", ".ini", ".uproject"}:
        data = data.replace(b"\r\n", b"\n")
    return hashlib.sha256(data).hexdigest()

violations = []
for rel in protected:
    actual, frozen = project / rel, baseline / rel
    if not actual.is_file() or actual.is_symlink() or not frozen.is_file() or digest(actual) != digest(frozen):
        violations.append("protected:" + rel)
for path in project.rglob("*"):
    if not path.is_file():
        continue
    rel = path.relative_to(project)
    if set(rel.parts) & excluded:
        continue
    if rel.as_posix() not in inventory:
        violations.append("unexpected:" + rel.as_posix())
report = {"valid": not violations, "violations": sorted(set(violations))}
pathlib.Path(sys.argv[4]).write_text(json.dumps(report, indent=2) + "\n")
if violations:
    print("VERDICT: submission boundary violation", *report["violations"][:20], sep="\n")
    raise SystemExit(2)
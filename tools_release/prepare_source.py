"""Prepare a source snapshot without changing the Git index or contacting a remote."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE_NAME = "OKWW-DualPC-source"
SOURCE_ARCHIVE = "OK-WW-DualPC-source.zip"
PRIVATE_ROOTS = {
    ".agents", ".git", ".local-archive", ".venv", "build-native", "cache",
    "configs", "configs-dualpc", "dist-native", "dist-dualpc", "logs",
    "native-full-data", "ok_tasks", "ok_import", "ok_templates", "deleted-scripts", "workshop-library", "release-review", "screenshots", "validation-dualpc",
}
BUILD_SUFFIXES = {".exe", ".dll", ".pyd", ".pyc", ".obj", ".pdb", ".lib", ".zip", ".7z", ".okscript"}
TEXT_SUFFIXES = {
    ".py", ".cpp", ".hpp", ".h", ".c", ".ps1", ".cmd", ".cs", ".md", ".txt",
    ".json", ".toml", ".yml", ".yaml", ".ini", ".xml", ".svg",
}
CONTENT_RULES = {
    "absolute_user_path": re.compile(r"[A-Z]:[\\/]+Users[\\/]+(?!Public\b|Default\b)", re.I),
    "private_key": re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    "access_token": re.compile(r"(?:ghp_|github_pat_|sk-proj-)[A-Za-z0-9_]{20,}"),
}
REQUIRED = {
    "LICENSE.txt", "README.md", "Build-Native.ps1", "UPSTREAM.lock.json",
    "native/CMakeLists.txt", "native/src/runtime/workshop.cpp",
    "native/bridge/workshop_worker.py", "src/gui/CharacterCodeTab.py",
    "vendor/ok-script/LICENSE.txt", "vendor/ok-script/ok/__init__.py",
}


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def git_files() -> list[str]:
    command = ["git", "-C", str(ROOT)]
    top = Path(subprocess.check_output(command + ["rev-parse", "--show-toplevel"], text=True).strip())
    if top.resolve() != ROOT:
        raise RuntimeError("Run this tool in its own repository, not an enclosing checkout.")
    raw = subprocess.check_output(command + ["ls-files", "--cached", "--others", "--exclude-standard", "-z"])
    return sorted({name.decode("utf-8").replace("\\", "/") for name in raw.split(b"\0") if name})


def forbidden_path(name: str) -> bool:
    path = PurePosixPath(name)
    return (
        path.is_absolute() or ".." in path.parts or path.parts[0] in PRIVATE_ROOTS
        or path.name in {"dualpc.json", "native-device-settings.json"} or name.startswith("native/personal/")
        or path.suffix.lower() in BUILD_SUFFIXES
        or any(part in {".git", "__pycache__", ".pytest_cache", "node_modules"} for part in path.parts)
        or path.name.lower() in {".env", "piptoken.txt"}
    )


def inspect(names: list[str]) -> tuple[list[dict], list[dict]]:
    files, findings = [], []
    for name in names:
        path = ROOT / name
        if forbidden_path(name) or path.is_symlink() or not path.is_file():
            findings.append({"path": name, "kind": "private_generated_or_missing"})
            continue
        size = path.stat().st_size
        # Keep large models and recordings out of accidental source additions.
        if size > 50 * 1024 * 1024:
            findings.append({"path": name, "kind": "file_over_source_budget"})
        if path.suffix.lower() in TEXT_SUFFIXES:
            try:
                content = path.read_text(encoding="utf-8-sig")
            except UnicodeError:
                # Clipper's original copyright header uses a legacy encoding.
                # Preserve the upstream file while still scanning its ASCII text.
                if name.startswith("native/third_party/"):
                    content = path.read_bytes().decode("latin-1")
                else:
                    findings.append({"path": name, "kind": "invalid_utf8"})
                    content = ""
            for number, line in enumerate(content.splitlines(), 1):
                for kind, pattern in CONTENT_RULES.items():
                    if pattern.search(line):
                        # Report locations, never echo a possible secret.
                        findings.append({"path": name, "line": number, "kind": kind})
        files.append({"path": name, "bytes": size, "sha256": digest(path)})
    for name in sorted(REQUIRED - set(names)):
        findings.append({"path": name, "kind": "required_source_missing"})
    return files, findings


def owned_directory(path: Path) -> None:
    """Only replace the exact source directory previously created by this tool."""
    if path.name != SOURCE_NAME or path.parent == ROOT or ROOT not in path.parents:
        raise RuntimeError("The review output must be a subdirectory of this repository.")
    marker = path / ".source-review.json"
    if path.exists():
        if not marker.is_file() or json.loads(marker.read_text(encoding="utf-8")).get("owner") != SOURCE_NAME:
            raise RuntimeError("Existing output is not owned by this tool; choose an empty review directory.")
        shutil.rmtree(path)
    path.mkdir(parents=True)
    marker.write_text(json.dumps({"owner": SOURCE_NAME}), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Audit only; do not create a source archive")
    parser.add_argument("--output", type=Path, default=ROOT / "release-review")
    args = parser.parse_args()
    files, findings = inspect(git_files())
    report = {"ok": not findings, "files": len(files), "bytes": sum(f["bytes"] for f in files),
              "findings": findings, "git_index_changed": False, "network_used": False}
    if findings:
        print(json.dumps(report, ensure_ascii=False, indent=2))
        raise SystemExit(1)
    if not args.check:
        output = args.output.resolve()
        source = output / SOURCE_NAME
        owned_directory(source)
        for entry in files:
            target = source / entry["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / entry["path"], target)
        manifest = {"version": "0.2.0", "channel": "preview", "files": files}
        (source / "SOURCE-MANIFEST.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        archive = output / SOURCE_ARCHIVE
        temporary = archive.with_suffix(".zip.tmp")
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=5) as zipped:
            for path in sorted(source.rglob("*")):
                if path.is_file() and path.name != ".source-review.json":
                    zipped.write(path, SOURCE_NAME + "/" + path.relative_to(source).as_posix())
        temporary.replace(archive)
        report["archive_sha256"] = digest(archive)
        (output / "source-audit.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()

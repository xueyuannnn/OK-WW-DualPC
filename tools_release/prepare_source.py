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
    ".agents", ".git", ".local-archive", ".venv", ".rust-tools", "target", "build-rust", "build-native", "cache",
    "configs", "configs-dualpc", "dist-native", "dist-dualpc", "logs",
    "native-full-data", "ok_tasks", "ok_import", "ok_templates", "deleted-scripts", "workshop-library", "release-review", "screenshots", "validation-dualpc",
}
BUILD_SUFFIXES = {".exe", ".dll", ".pyd", ".pyc", ".obj", ".pdb", ".lib", ".zip", ".7z", ".okscript"}
TEXT_SUFFIXES = {
    ".py", ".cpp", ".hpp", ".h", ".c", ".ps1", ".cmd", ".cs", ".md", ".txt",
    ".json", ".toml", ".yml", ".yaml", ".ini", ".xml", ".svg", ".rs", ".def",
}
CONTENT_RULES = {
    "absolute_user_path": re.compile(r"[A-Z]:[\\/]+Users[\\/]+(?!Public\b|Default\b)", re.I),
    "private_key": re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    "access_token": re.compile(r"(?:ghp_|github_pat_|sk-proj-)[A-Za-z0-9_]{20,}"),
}
REQUIRED = {
    "LICENSE.txt", "README.md", "Build-Native.ps1", "UPSTREAM.lock.json",
    "native/CMakeLists.txt", "native/host/runtime/workshop.cpp",
    "workshop/bridge/workshop_worker.py", "src/gui/CharacterCodeTab.py",
    "vendor/ok-script/LICENSE.txt", "vendor/ok-script/ok/__init__.py",
    "Cargo.toml", "Cargo.lock", "rust-toolchain.toml", "crates/okww-core/Cargo.toml", "crates/okww-core/src/lib.rs",
    "crates/okww-core/build.rs", "crates/okww-core/abi/rotation.def", "native/cmake/RustCore.cmake",
    "native/host/runtime/rotation_abi.hpp",
    "crates/okww-core/abi/taskflow.def", "native/host/runtime/task_abi.hpp", "native/host/runtime/task_bridge.cpp",
    "native/host/runtime/task_text_bridge.cpp", "crates/okww-core/src/taskflow.rs", "crates/okww-core/src/gauge.rs",
    "native/host/runtime/task_image_bridge.cpp", "crates/okww-core/src/taskflow/image.rs",
    "crates/okww-core/abi/engine.def", "crates/okww-core/src/engine.rs", "native/host/runtime/engine_abi.hpp",
    "crates/okww-core/abi/commands.def", "crates/okww-core/src/engine/commands.rs", "crates/okww-core/src/engine/scripts.rs",
    "crates/okww-core/abi/session.def", "crates/okww-core/abi/session_state.def", "crates/okww-core/src/session.rs",
    "native/host/runtime/session_abi.hpp", "native/host/runtime/session_bridge.cpp",
    "crates/okww-core/abi/visionflow.def", "crates/okww-core/src/visionflow.rs", "native/host/runtime/vision_abi.hpp",
    "crates/okww-core/abi/workflow.def", "crates/okww-core/src/services/mod.rs", "native/host/runtime/workflow_abi.hpp",
    "crates/okww-core/src/garden.rs", "crates/okww-core/src/services/tasks/garden_vision.rs",
    "crates/okww-core/src/services/tasks/garden.rs", "crates/okww-core/src/services/tasks/garden_shop.rs",
    "crates/okww-core/src/services/tasks/daily.rs", "crates/okww-core/src/services/tasks/daily_actions.rs",
    "crates/okww-core/src/services/tasks/daily_run.rs", "crates/okww-core/src/services/tasks/accounts.rs",
    "crates/okww-core/src/services/tasks/world.rs", "crates/okww-core/src/services/tasks/world_movement.rs",
    "crates/okww-core/src/services/tasks/echo_farm.rs",
    "crates/okww-core/src/services/tasks/echo_travel.rs",
    "crates/okww-core/src/services/tasks/echo_combat.rs", "crates/okww-core/src/services/tasks/echo_run.rs",
    "crates/okww-core/src/echo_text.rs", "crates/okww-core/src/services/tasks/echo_stats.rs",
    "crates/okww-core/src/services/tasks/echo_operations.rs",
    "crates/okww-core/src/services/input/input_control.rs",
    "crates/okww-core/src/services/vision/inference.rs",
    "crates/okww-core/src/services/application/scripts.rs",
    "crates/okww-core/src/services/vision/template_store.rs",
    "crates/okww-core/src/services/workshop/state.rs",
    "crates/okww-core/src/services/workshop/files.rs",
    "crates/okww-core/src/services/workshop/runtime.rs",
    "crates/okww-core/src/services/application/scheduler.rs",
    "crates/okww-core/src/services/teams/rotation.rs",
    "crates/okww-core/src/services/teams/store.rs",
    "crates/okww-core/src/services/teams/modules.rs",
    "crates/okww-core/src/services/teams/catalog.rs",
    "crates/okww-core/src/services/teams/mutations.rs",
    "crates/okww-core/src/services/teams/editor.rs",
    "crates/okww-core/src/services/application/dispatcher.rs", "crates/okww-core/src/task_ids.rs",
    "crates/okww-core/src/update_version.rs",
    "crates/okww-core/src/services/updates/policy.rs",
    "crates/okww-core/src/services/updates/client.rs",
    "crates/okww-core/src/services/notifications/config.rs",
    "crates/okww-core/src/services/notifications/transport.rs",
    "crates/okww-core/src/services/notifications/queue.rs",
    "crates/okww-core/src/services/notifications/desktop.rs",
    "crates/okww-core/src/services/notifications/messenger.rs",
    "crates/okww-core/src/services/notifications/delivery.rs",
    "crates/okww-core/src/services/notifications/clipboard.rs",
    "crates/okww-core/src/services/notifications/windows.rs",
    "crates/okww-core/src/services/application/common_config.rs",
    "crates/okww-core/src/services/vision/common_preview.rs",
    "crates/okww-core/src/services/application/resource_failure.rs",
    "crates/okww-core/src/services/application/diagnostics.rs",
    "crates/okww-core/src/services/input/desktop_platform.rs",
    "crates/okww-core/src/services/input/desktop_identity.rs",
    "crates/okww-core/src/services/input/desktop_targets.rs",
    "crates/okww-core/src/services/input/desktop_physical.rs",
    "crates/okww-core/src/services/input/controller_protocol.rs",
    "crates/okww-core/src/services/input/controller_lifecycle.rs",
    "crates/okww-core/src/services/input/controller_input.rs",
    "crates/okww-core/src/services/input/controller_local.rs",
    "crates/okww-core/src/services/runtime/lifecycle.rs",
    "crates/okww-core/src/services/runtime/frames.rs",
    "crates/okww-core/src/services/runtime/control.rs", "crates/okww-core/src/services/runtime/monitor.rs",
    "crates/okww-core/src/services/runtime/dispatch.rs",
    "crates/okww-core/src/services/runtime/serve.rs",
    "crates/okww-core/src/services/vision/combat_images.rs",
    "crates/okww-core/src/services/vision/text_helpers.rs", "crates/okww-core/src/backend_state.rs", "native/include/backend_state_abi.hpp", "crates/okww-core/src/backend_pixels.rs", "native/include/backend_pixels_abi.hpp",
    "crates/okww-core/src/controller_math.rs", "native/include/controller_math_abi.hpp",
    "crates/okww-core/src/services/capture/types.rs",
    "crates/okww-core/src/services/capture/capabilities.rs",
    "crates/okww-core/src/services/capture/flow.rs",
    "crates/okww-core/src/services/capture/devices.rs",
    "crates/okww-core/src/services/capture/modes.rs",
    "crates/okww-core/src/services/capture/dshow.rs",
    "crates/okww-core/src/services/capture/mf.rs",
    "crates/okww-core/src/services/capture/desktop.rs",
    "crates/okww-core/src/services/capture/duplication.rs",
    "crates/okww-core/src/services/capture/desktop_run.rs",
    "crates/okww-core/src/capture_math.rs", "native/include/capture_math_abi.hpp",
    "native/include/diagnostic_abi.hpp",
    "native/include/desktop_input_abi.hpp", "crates/okww-core/src/desktop_input.rs",
    "crates/okww-core/src/frame_pixels.rs", "native/include/common_pixels_abi.hpp",
    "native/include/update_version_abi.hpp",
    "native/include/workflow_types.hpp",
    "native/include/platform_workflow.hpp", "native/host/runtime/task_registry_abi.hpp",
    "crates/okww-core/src/services/application/profile.rs",
    "crates/okww-core/src/services/vision/templates.rs",
    "crates/okww-core/src/services/vision/imaging.rs", "crates/okww-core/src/image_pixels.rs", "native/host/runtime/image_pixels_abi.hpp",
    "crates/okww-core/src/image_geometry.rs", "native/host/runtime/image_geometry_abi.hpp",
    "crates/okww-core/src/services/vision/ocr_geometry.rs", "native/host/runtime/ocr_geometry_host.hpp",
    "crates/okww-core/src/services/vision/ocr.rs", "crates/okww-core/src/ocr_numeric.rs", "native/host/runtime/ocr_numeric_abi.hpp",
    "crates/okww-core/src/services/vision/echo_detection.rs", "crates/okww-core/src/echo_detection.rs",
    "native/host/runtime/echo_detection_abi.hpp", "native/host/runtime/echo_detection_host.hpp",
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

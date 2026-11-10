"""Assemble a public package from rebuilt applications and verified dependencies."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import zipfile

from prepare_source import ROOT, SOURCE_NAME, SOURCE_ARCHIVE, digest

PACKAGE_NAME = "OKWW-DualPC-full"
RUNTIME_FOLDERS = {"generic", "imageformats", "networkinformation", "platforms", "styles", "tls", "licenses"}
APPLICATIONS = {"OKWW-DualPC-Native-Dev.exe", "native_runtime_check.exe"}
ENGINE_FOLDERS = ("src", "dualpc", "vendor", "assets", "icons", "i18n")
ENGINE_FILES = ("config.py", "main_dualpc.py", "LICENSE.txt", "THIRD-PARTY-DUALPC.md", "UPSTREAM.lock.json")
COPY_IGNORE = shutil.ignore_patterns(
    "__pycache__", "*.pyc", "*.pyo", ".git", ".pytest_cache", ".source-review.json",
    "objects-Debug", "objects-RelWithDebInfo", "*.obj", "*.pdb", "*.lib", "*.exp", "*.ilk",
    # pip records the source machine's install path here; imports do not need it.
    "direct_url.json", "*.egg-link",
)


def windows_path(path: Path) -> Path:
    """Some Qt runtime resource names exceed the legacy Windows path limit."""
    path = path.absolute()
    if os.name == "nt" and not str(path).startswith("\\\\?\\"):
        return Path("\\\\?\\" + str(path))
    return path


def copy_tree(source: Path, target: Path) -> None:
    shutil.copytree(windows_path(source), windows_path(target), dirs_exist_ok=True, ignore=COPY_IGNORE)


def source_snapshot(review: Path) -> Path:
    source = review / SOURCE_NAME
    manifest = json.loads((source / "SOURCE-MANIFEST.json").read_text(encoding="utf-8"))
    for entry in manifest["files"]:
        if digest(source / entry["path"]) != entry["sha256"]:
            raise RuntimeError("Source snapshot has changed; prepare it again before packaging.")
        if digest(ROOT / entry["path"]) != entry["sha256"]:
            raise RuntimeError("Working source has changed since the snapshot was prepared.")
    return source


def verify_runtime(runtime: Path) -> dict:
    manifest = json.loads((runtime / "PACKAGE.json").read_text(encoding="utf-8-sig"))
    if manifest.get("variant") != "full" or manifest.get("runtime") != "C++ + Python workshop":
        raise RuntimeError("Select a verified ordinary hybrid package, not a personal profile.")
    for name in ("native-full-data", "native-teams", "native/personal", "dualpc.json", "native-device-settings.json"):
        if (runtime / name).exists():
            raise RuntimeError("The runtime contains a profile or imported teams.")
    for entry in manifest["files"]:
        file = windows_path(runtime / entry["path"])
        if digest(file) != entry["sha256"].lower():
            raise RuntimeError("Runtime manifest mismatch: " + entry["path"])
    if not (runtime / "workshop-engine/runtime/python.exe").is_file():
        raise RuntimeError("Portable workshop Python is missing.")
    return manifest


def prepare_directory(stage: Path, review: Path) -> None:
    if stage.resolve().parent != review or ROOT not in review.parents or stage.name != PACKAGE_NAME:
        raise RuntimeError("Unexpected package directory.")
    marker = stage / ".public-package.json"
    if stage.exists():
        if not marker.is_file() or json.loads(marker.read_text(encoding="utf-8")).get("owner") != PACKAGE_NAME:
            raise RuntimeError("Existing directory is not owned by this packager.")
        def remove_readonly(operation, name, error):
            # OpenVINO can mark its generated cache files read-only. Change only
            # files inside this already verified, tool-owned staging directory.
            target = Path(name).resolve()
            scope = windows_path(stage).resolve()
            if not isinstance(error, PermissionError) or (target != scope and scope not in target.parents):
                raise error
            target.chmod(target.stat().st_mode | stat.S_IWRITE)
            operation(name)

        shutil.rmtree(windows_path(stage), onexc=remove_readonly)
    stage.mkdir(parents=True)
    marker.write_text(json.dumps({"owner": PACKAGE_NAME}), encoding="utf-8")


def write_manifest(stage: Path, source_exe_hash: str) -> list[Path]:
    root = windows_path(stage)
    files = sorted(p for p in root.rglob("*") if p.is_file() and p.name != "PACKAGE.json")
    manifest = {
        "version": "0.2.0", "channel": "preview", "variant": "full",
        "runtime": "C++ + Python workshop", "status": "candidate-awaiting-hardware-acceptance",
        "exe_sha256": source_exe_hash.lower(), "personal_profiles": 0,
        "files": [{"path": p.relative_to(root).as_posix(), "bytes": p.stat().st_size,
                   "sha256": digest(p)} for p in files],
    }
    (stage / "PACKAGE.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return files + [root / "PACKAGE.json"]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--native-build", type=Path, required=True,
                        help="Release directory containing the rebuilt application and native_runtime_check")
    parser.add_argument("--review", type=Path, default=ROOT / "release-review")
    args = parser.parse_args()
    runtime, review = args.runtime.resolve(), args.review.resolve()
    native_build = args.native_build.resolve()
    for name in APPLICATIONS:
        if not (native_build / name).is_file():
            raise RuntimeError("Rebuild the native application first: " + name)
    source = source_snapshot(review)
    verify_runtime(runtime)
    stage = review / PACKAGE_NAME
    if runtime == stage or stage in runtime.parents:
        raise RuntimeError("The source runtime must be separate from the output.")
    prepare_directory(stage, review)

    # Only known runtime folders are copied. Profiles and historical notes are
    # deliberately outside this list, even if the source directory gains them.
    for item in runtime.iterdir():
        if item.is_file() and item.suffix.lower() == ".dll":
            shutil.copy2(item, stage / item.name)
        elif item.name in RUNTIME_FOLDERS and item.is_dir():
            copy_tree(item, stage / item.name)
    for name in APPLICATIONS:
        shutil.copy2(native_build / name, stage / name)
    executable_hash = digest(stage / "OKWW-DualPC-Native-Dev.exe")
    for name in ("assets", "icons"):
        copy_tree(source / name, stage / name)
    copy_tree(runtime / "assets/ocr", stage / "assets/ocr")
    copy_tree(source / "native/resources", stage / "native/resources")
    copy_tree(source, stage / "source")

    engine = stage / "workshop-engine"
    for name in ENGINE_FOLDERS:
        copy_tree(source / name, engine / name)
    for name in ENGINE_FILES:
        shutil.copy2(source / name, engine / name)
    copy_tree(source / "native/bridge", engine / "native/bridge")
    copy_tree(runtime / "workshop-engine/runtime", engine / "runtime")
    (engine / "ok_tasks").mkdir(exist_ok=True)
    for name in ("LICENSE.txt", "THIRD-PARTY-DUALPC.md", "UPSTREAM.lock.json"):
        shutil.copy2(source / name, stage / name)
    shutil.copy2(source / "docs/release-notes.md", stage / "ReleaseNotes.md")
    (stage / "README.md").write_text(
        "# OK-WW-DualPC · OK-WW双机版 0.2.0 预览版\n\n"
        "鸣潮双机自动化工具，使用采集卡读取画面、键鼠盒子发送操作。"
        "C++ 主程序执行内置任务与角色，独立 Python 引擎运行工坊连招。\n\n"
        "解压整个目录，运行 `OKWW-DualPC-Native-Dev.exe`。"
        "配置好采集卡和盒子后点击“开始”，自动连接并运行任务；连接后可用“测试移动”检查鼠标输出。F9 暂停或取消准备。\n\n"
        "- [完整功能介绍](source/README.md)\n"
        "- [设备与预览](source/docs/devices.md)\n"
        "- [任务使用](source/docs/usage.md)\n"
        "- [角色代码工坊](source/docs/workshop.md)\n"
        "- [源码构建](source/docs/development.md)\n\n"
        "配置保存在 `native-full-data/`，首次运行自动生成。"
        "更新前建议备份此目录。\n\n"
        "反馈邮箱：[lidada0202@gmail.com](mailto:lidada0202@gmail.com)。"
        "请附软件版本、设备型号、复现步骤和相关日志。\n", encoding="utf-8")

    files = write_manifest(stage, executable_hash)
    archive = review / "OK-WW-DualPC-full.zip"
    temporary = archive.with_suffix(".zip.tmp")
    root = windows_path(stage)
    with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=5) as zipped:
        for path in files:
            zipped.write(path, PACKAGE_NAME + "/" + path.relative_to(root).as_posix())
    temporary.replace(archive)
    archives = [review / SOURCE_ARCHIVE, archive]
    (review / "SHA256SUMS.txt").write_text(
        "".join(digest(path) + "  " + path.name + "\n" for path in archives), encoding="utf-8")
    report = {"files": len(files), "bytes": sum(p.stat().st_size for p in files),
              "archive_bytes": archive.stat().st_size, "archive_sha256": digest(archive),
              "exe_sha256": executable_hash, "personal_profiles": 0}
    (review / "binary-audit.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()

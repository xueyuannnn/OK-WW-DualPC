import json
from pathlib import Path
import zipfile

import pytest

from tools_dualpc.prepare_update import extract_snapshot, prepare

ROOT = Path(__file__).absolute().parents[1]


def test_snapshot_path_traversal_is_rejected(tmp_path):
    archive = tmp_path / "unsafe.zip"
    with zipfile.ZipFile(archive, "w") as bundle:
        bundle.writestr("snapshot/../../escape.py", "bad")
    with pytest.raises(ValueError, match="Unsafe"):
        extract_snapshot(archive, tmp_path / "candidate")
    assert not (tmp_path / "escape.py").exists()


@pytest.mark.parametrize("new_native_input", [False, True])
def test_upgrade_preserves_adapter_and_marks_static_only(tmp_path, new_native_input):
    # Small but structurally complete upstream fixture, with the original game
    # file read from the tracked source and the actual pinned framework copied.
    archive = tmp_path / "snapshot.zip"
    files = ("config.py", "pyproject.toml", "src/combat/CombatCheck.py", "src/task/DailyTask2.py",
             "src/task/TacetTask2.py", "src/task/MouseResetTask.py", "src/task/GardenTask.py", "assets/coco_annotations.json")
    with zipfile.ZipFile(archive, "w") as bundle:
        for relative in files:
            bundle.write(ROOT / relative, "snapshot/" + relative)
        if new_native_input:
            bundle.writestr("snapshot/src/task/NewNativeTask.py", "import win32api\nwin32api.SetCursorPos((0, 0))\n")
    lock = json.loads((ROOT / "UPSTREAM.lock.json").read_text(encoding="utf-8"))
    candidate = tmp_path / "candidate"
    if new_native_input:
        with pytest.raises(RuntimeError, match="native input"):
            prepare(archive, candidate, "a" * 40, lock)
        assert json.loads((candidate / "UPDATE-REPORT.json").read_text())["status"].startswith("FAILED")
        return
    report = prepare(archive, candidate, "a" * 40, lock)
    assert "runtime and hardware validation still required" in report["status"]
    assert (candidate / "dualpc/controllers.py").is_file()
    assert (candidate / "dualpc.json").read_bytes() == (ROOT / "dualpc.json").read_bytes()
    assert json.loads((candidate / "UPSTREAM.lock.json").read_text())["upstream"]["sha"] == "a" * 40
    with pytest.raises(ValueError, match="already exists"):
        prepare(archive, candidate, "a" * 40, lock)


def test_update_rejects_wrong_commit_before_creating_candidate(tmp_path):
    archive = tmp_path / "wrong.zip"
    with zipfile.ZipFile(archive, "w") as bundle:
        bundle.writestr("snapshot/config.py", "")
        bundle.comment = ("b" * 40).encode()
    with pytest.raises(ValueError, match="commit"):
        prepare(archive, tmp_path / "candidate", "a" * 40, {})
    assert not (tmp_path / "candidate").exists()

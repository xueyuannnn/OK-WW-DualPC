import importlib.util
import json
from pathlib import Path


def test_public_manifest_excludes_generated_engine_data_and_keeps_runtime(tmp_path):
    module_file = Path(__file__).resolve().parents[1] / "tools_release/prepare_binary.py"
    import sys
    sys.path.insert(0, str(module_file.parent))
    try:
        spec = importlib.util.spec_from_file_location("release_binary_test", module_file)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    finally:
        sys.path.pop(0)
    included = {"workshop-engine/runtime/python.exe", "workshop-engine/config.py",
                "workshop-engine/assets/configs/model.json", "source/docs/usage.md"}
    excluded = {f"workshop-engine/{folder}/local-data.txt"
                for folder in ("cache", "configs", "logs", "screenshots", "__pycache__")}
    for relative in included | excluded:
        target = tmp_path / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("runtime or local data", encoding="utf-8")
    files = module.write_manifest(tmp_path, "ABC123")
    manifest = json.loads((tmp_path / "PACKAGE.json").read_text(encoding="utf-8"))
    assert {entry["path"] for entry in manifest["files"]} == included
    root = module.windows_path(tmp_path)
    assert {p.relative_to(root).as_posix() for p in files} == included | {"PACKAGE.json"}

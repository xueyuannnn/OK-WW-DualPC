"""Authoring utility: import uncropped game UI portraits; never used at runtime.

Run with the repository venv. The reviewed index records the public asset page
and exact image URL. Images are decoded and saved losslessly as PNG so the C++
host needs neither network access nor an additional image-format plugin.
"""
import concurrent.futures
import argparse
import hashlib
import json
from pathlib import Path
from PySide6.QtGui import QImage

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "validation-dualpc/portrait-20261109"
OUT = ROOT / "native/resources/portraits"
ALIASES = {
    "YangYangSp": "Yangyang: Xuanling", "ShoreKeeper": "The Shorekeeper",
    "Xiangliyao": "Xiangli Yao", "Douling": "Buling", "Linnai": "Lynae",
    "Xigelika": "Sigrika", "Luhesi": "Luuk", "JingRan": "Jingran",
    "Lucy": "Lucy (Cyberpunk Collab)", "Rebecca": "Rebecca (Cyberpunk Collab)",
}
characters = json.loads((ROOT / "native/resources/characters.json").read_text(encoding="utf-8"))["characters"]
index = json.loads((REPORT / "portrait-index.json").read_text(encoding="utf-8-sig"))
by_name = {}
for entry in index:
    by_name.setdefault(entry["name"], entry)
# The first Lynae entry in the catalog is a full-body render. The second is
# the actual square character icon, reviewed alongside the other portraits.
by_name["Lynae"] = [entry for entry in index if entry["name"] == "Lynae"][1]
by_name["Rover"] = {
    "name": "Rover", "page": "https://wutheringwaves.kurogames.com/en/main",
    "url": "https://wutheringwaves.kurogames.com/static4.0/assets/female-3a65e809.png",
}
jobs = []
for character in characters:
    class_name = character["class"]
    name = ALIASES.get(class_name, class_name)
    if name not in by_name:
        raise ValueError(f"No reviewed portrait for {class_name} ({name})")
    jobs.append((class_name, by_name[name]))
parser = argparse.ArgumentParser()
parser.add_argument("--prepare", action="store_true")
arguments = parser.parse_args()
(REPORT / "portrait-downloads.json").write_text(json.dumps([
    {"class": class_name, **entry} for class_name, entry in jobs
], ensure_ascii=False, indent=2), encoding="utf-8")
if arguments.prepare:
    print(json.dumps({"reviewed_downloads": len(jobs)}))
    raise SystemExit(0)
OUT.mkdir(exist_ok=True, parents=True)

def import_one(job):
    class_name, entry = job
    data = (REPORT / "portrait-sources" / f"{class_name}.source").read_bytes()
    if len(data) > 4 * 1024 * 1024:
        raise ValueError(f"Oversized portrait: {class_name}")
    image = QImage.fromData(data)
    if image.isNull() or min(image.width(), image.height()) < 80:
        raise ValueError(f"Invalid portrait: {class_name}")
    path = OUT / f"{class_name}.png"
    if not image.save(str(path), "PNG"):
        raise RuntimeError(f"Cannot save {class_name}")
    return {
        "class": class_name, "file": path.name,
        "source_page": entry["page"], "source_url": entry["url"],
        "source_sha256": hashlib.sha256(data).hexdigest().upper(),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest().upper(),
        "width": image.width(), "height": image.height(), "crop": "none",
    }

with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
    portraits = list(pool.map(import_one, jobs))
manifest = {
    "purpose": "UI-only complete portraits; recognition templates are independent",
    "copyright": "Game artwork belongs to its respective rights holders; not relicensed as project code",
    "portraits": portraits,
}
(OUT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({"portraits": len(portraits), "crop": "none", "network_required_at_runtime": False}))

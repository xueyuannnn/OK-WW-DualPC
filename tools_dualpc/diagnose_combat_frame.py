"""Inspect combat recognition on a saved image without opening input or capture."""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import sys
from types import SimpleNamespace

ROOT = Path(__file__).absolute().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "vendor/ok-script"))
sys.argv[0] = str(ROOT / "main_dualpc.py")

import cv2
import numpy as np


def inspect_frame(frame, config, color_compat=False):
    from ok import og
    from ok.feature.FeatureSet import FeatureSet
    from src.task.AutoCombatTask import AutoCombatTask
    from src.combat.CombatCheck import CombatCheck, enemy_health_color_red

    template = dict(config["template_matching"])
    template["coco_json"] = template.pop("coco_feature_json")
    features = FeatureSet(debug=False, **template)
    defaults = {option.name: deepcopy(option.default_config) for option in config["global_configs"]}
    height, width = frame.shape[:2]
    executor = SimpleNamespace(scene=None, frame=frame, feature_set=features, text_fix={}, debug=False,
                               method=SimpleNamespace(width=width, height=height),
                               global_config=SimpleNamespace(get_config=defaults.__getitem__),
                               device_manager=SimpleNamespace(supported_ratio=16 / 9,
                                   get_preferred_device=lambda: {"device": "windows"}))
    original_app = og.my_app
    og.my_app = SimpleNamespace(logged_in=False)
    try:
        task = AutoCombatTask(executor, None)
        standard_detector = getattr(CombatCheck.has_health_bar, "_dualpc_original", CombatCheck.has_health_bar)
        report = {"frame_size": [width, height], "in_team": task.in_team(),
                  "has_health_bar": bool(standard_detector(task)), "boss_marker": bool(task.is_boss()),
                  "configured_red_range": enemy_health_color_red, "target_checks": []}
        if color_compat:
            from ok import Logger
            from onnxocr.onnx_paddleocr import ONNXPaddleOcr
            from dualpc.combat import shifted_health_bar, shifted_bar_candidates, install_combat_hook
            ocr = ONNXPaddleOcr(use_angle_cls=False, logger=Logger.get_logger(__name__),
                               use_npu=False, use_openvino=True)

            executor.paused = False
            executor.config = {"ocr": {"default": {"lib": "onnxocr"}}}
            executor.ocr_po_translation = None
            executor.ocr_lib = lambda name: ocr
            executor.interaction = SimpleNamespace(is_remote=True)
            executor.device_manager.capture_method = SimpleNamespace(reader=object())
            report["shifted_bar_candidates"] = [[b.x, b.y, b.width, b.height]
                                                 for b in shifted_bar_candidates(frame)]
            report["color_compat_health_bar"] = shifted_health_bar(task, frame) if report["in_team"][0] else False
            install_combat_hook()
            report["runtime_health_bar"] = bool(task.has_health_bar())
        has_name, no_name = task.get_target_names()
        regions = [("normal", task.get_box_by_name(has_name).scale(1.1)),
                   ("long", task.get_box_by_name("box_target_enemy_long")),
                   ("long2", task.get_box_by_name("target_box_long2")),
                   ("tall_fallback", task.get_box_by_name(has_name).scale(1.1, 2.0))]
        for label, box in regions:
            best = task.find_best_match_in_box(box, [has_name, no_name], threshold=0.6)
            report["target_checks"].append({"area": label, "match": best.name if best else None,
                                             "confidence": float(best.confidence) if best else None})
        report["scope"] = "saved image only; no task run, keyboard/mouse output or hardware access"
        return report
    finally:
        og.my_app = original_app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--color-compat", action="store_true", help="Also check the shifted-color fallback with local OCR")
    parser.add_argument("--output", type=Path, default=ROOT / "validation-dualpc/combat-frame-check.json")
    args = parser.parse_args()
    # Framework class defaults must not write into the live user's config.
    from ok.util.config import Config
    Config.config_folder = str(ROOT / "validation-dualpc/combat-diagnostics/configs")
    from config import config
    frame = cv2.imdecode(np.fromfile(str(args.image), dtype=np.uint8), cv2.IMREAD_COLOR)
    if frame is None:
        raise ValueError("Cannot read the selected image")
    report = inspect_frame(frame, config, args.color_compat)
    report["image"] = str(args.image.absolute())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()

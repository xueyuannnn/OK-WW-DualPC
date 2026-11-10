"""Replay Garden recognition against images only; no capture or input devices."""
import argparse
import json
from pathlib import Path
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'vendor/ok-script'))


def image_task(frame, engine):
    import numpy as np
    from ok import Box
    h, w = frame.shape[:2]

    def ocr(x, y, to_x, to_y, threshold=.75, target_height=0, frame_processor=None, **unused):
        import cv2
        sx, sy, ex, ey = round(x*w), round(y*h), round(to_x*w), round(to_y*h)
        scale = target_height/h if target_height > 0 and h >= 1.5*target_height else 1
        crop = frame[sy:ey, sx:ex]
        if scale != 1:
            crop = cv2.resize(crop, None, fx=scale, fy=scale)
        if frame_processor:
            crop = frame_processor(crop)
        result = engine.ocr(crop)
        boxes = []
        for points, (name, confidence) in result[0] or []:
            if confidence < threshold:
                continue
            points = np.asarray(points)/scale
            left, top = np.min(points, axis=0)
            right, bottom = np.max(points, axis=0)
            boxes.append(Box(round(left+sx), round(top+sy), round(right-left), round(bottom-top),
                             confidence=confidence, name=name))
        return boxes
    return SimpleNamespace(frame=frame, ocr=ocr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('images', nargs='+', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    import cv2
    import numpy as np
    from ok import Logger
    from onnxocr.onnx_paddleocr import ONNXPaddleOcr
    from dualpc.garden import modern_reward_choice
    from dualpc.garden_shop import read_money, inventory
    engine = ONNXPaddleOcr(use_angle_cls=False, logger=Logger.get_logger(__name__),
                          use_npu=False, use_openvino=True)
    rows = []
    for path in args.images:
        frame = cv2.imdecode(np.fromfile(path, dtype=np.uint8), cv2.IMREAD_COLOR)
        task = image_task(frame, engine)
        choice = modern_reward_choice(task)
        rows.append({'file': str(path), 'choice': str(choice), 'money': read_money(task),
                     'items': [{'index': item.index+1, 'price': item.price, 'recommended': item.recommended}
                               for item in inventory(task)]})
    args.output.write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(rows, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

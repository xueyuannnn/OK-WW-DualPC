"""Compare Garden template matches on a saved frame without hardware input."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).absolute().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'vendor/ok-script'))
sys.argv[0] = str(ROOT / 'main_dualpc.py')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    import cv2
    import numpy as np
    from config import config
    from ok.feature.FeatureSet import FeatureSet
    frame = cv2.imdecode(np.fromfile(args.image, dtype=np.uint8), cv2.IMREAD_COLOR)
    if frame is None:
        raise ValueError('Cannot read frame')
    options = dict(config['template_matching'])
    options['coco_json'] = options.pop('coco_feature_json')
    features = FeatureSet(debug=False, **options)
    report = {'scope': 'saved image only; no device access or input', 'matches': {}}
    for name in ('garden_get_skip', 'garden_get_confirm_gray', 'garden_get_gold', 'garden_get_purple'):
        report['matches'][name] = {}
        for gray in (False, True):
            matches = features.find_feature(frame, name, horizontal_variance=.9 if 'gold' in name or 'purple' in name else 0,
                                            threshold=.001, use_gray_scale=gray, limit=1)
            report['matches'][name]['gray' if gray else 'color'] = [
                {'confidence': round(float(b.confidence), 4), 'x': b.x, 'y': b.y} for b in matches]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

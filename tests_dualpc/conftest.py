from pathlib import Path
import sys

ROOT = Path(__file__).absolute().parents[1]
sys.path.insert(0, str(ROOT / "vendor/ok-script"))
sys.path.insert(0, str(ROOT))

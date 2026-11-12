import json
from dataclasses import asdict

import pytest

from dualpc.settings import Settings


def test_native_timing_fields_and_legacy_profiles_load_in_python(tmp_path):
    path = tmp_path / "dualpc.json"
    config = asdict(Settings())
    config["kmbox"].update(position_settle=0.12, ui_click_hold=0.12)
    path.write_text(json.dumps(config), encoding="utf-8")
    loaded = Settings.load(path)
    assert loaded.kmbox.position_settle == 0.12
    assert loaded.kmbox.ui_click_hold == 0.12
    del config["kmbox"]["position_settle"]
    del config["kmbox"]["ui_click_hold"]
    path.write_text(json.dumps(config), encoding="utf-8")
    assert Settings.load(path).kmbox.position_settle == 0.12


@pytest.mark.parametrize("seconds", [0, 0.1, float("nan"), float("inf")])
def test_positioning_buffer_cannot_be_100ms_or_less(seconds):
    settings = Settings()
    settings.kmbox.position_settle = seconds
    with pytest.raises(ValueError):
        settings.validate()

"""Controller selection and migration checks; all input uses dry-run transports."""
import json

import pytest

from dualpc.controllers import SUPPORTED, create_controller
from dualpc.settings import Settings


@pytest.mark.parametrize("protocol", ["ch9329", "kvm_card_mini", "esp32_legacy", "unknown"])
@pytest.mark.parametrize("dry_run", [False, True])
def test_removed_protocol_is_rejected_before_connecting(protocol, dry_run):
    settings = Settings()
    settings.kmbox.protocol = protocol
    settings.kmbox.dry_run = dry_run
    with pytest.raises(ValueError, match="Unknown input protocol"):
        settings.validate()
    with pytest.raises(ValueError, match="Unsupported controller protocol"):
        create_controller(settings.kmbox)


@pytest.mark.parametrize("protocol", ["kmbox_net", "makcu_serial"])
def test_protocol_factory_dry_run(protocol):
    assert set(SUPPORTED) == {"kmbox_net", "makcu_serial"}
    settings = Settings()
    settings.kmbox.protocol = protocol
    settings.validate()
    client = create_controller(settings.kmbox)
    try:
        client.connect()
        assert not client.supports_absolute
        client.move(40, 40)
        client.key(4, True)
        client.button("left", True)
        client.release_all()
        assert not client.keys and not client.buttons
    finally:
        client.close()


@pytest.mark.parametrize("protocol", ["kmbox_net", "makcu_serial"])
def test_existing_profiles_load_without_retired_usb_fields(tmp_path, protocol):
    path = tmp_path / "dualpc.json"
    settings = Settings()
    settings.kmbox.protocol = protocol
    settings.kmbox.serial_port = "COM7"
    settings.save(path)
    data = json.loads(path.read_text(encoding="utf-8"))
    data["kmbox"].update(hid_vid=16701, hid_pid=8455, hid_path="retired-device")
    path.write_text(json.dumps(data), encoding="utf-8")
    loaded = Settings.load(path)
    assert loaded == settings
    loaded.save(path)
    saved = json.loads(path.read_text(encoding="utf-8"))["kmbox"]
    assert not set(saved) & {"hid_vid", "hid_pid", "hid_path"}

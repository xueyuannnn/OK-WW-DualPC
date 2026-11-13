import pytest

from dualpc.settings import Settings


@pytest.mark.parametrize('backend', ['DXGI', 'WIN32'])
def test_local_configuration_round_trips_without_box_credentials(tmp_path, backend):
    settings = Settings()
    settings.capture.source = 'desktop'
    settings.capture.local_backend = backend
    settings.capture.local_target = {'kind': 'window', 'title': 'test'}
    settings.capture.width = 0
    settings.capture.fourcc = 'P010'
    settings.kmbox.protocol = 'sendinput'
    settings.kmbox.dry_run = False
    settings.kmbox.target = {'kind': 'window', 'title': 'test'}
    path = tmp_path / 'local.json'
    settings.save(path)
    loaded = Settings.load(path)
    assert loaded == settings
    loaded.capture.dxgi_follow_refresh = False
    loaded.capture.local_fps = 540
    loaded.save(path)
    assert Settings.load(path) == loaded
    for fps in (0, 1001, 60.5):
        loaded.capture.local_fps = fps
        with pytest.raises(ValueError):
            loaded.validate()
    loaded.capture.local_fps = 60
    loaded.kmbox.target = {}
    loaded.validate()


def test_dxgi_and_sendinput_need_no_manual_target_or_frame_rate():
    settings = Settings()
    settings.capture.source = 'desktop'
    settings.capture.local_backend = 'DXGI'
    settings.kmbox.protocol = 'sendinput'
    settings.kmbox.dry_run = False
    settings.validate()
    assert settings.capture.dxgi_monitor == ''
    assert settings.capture.dxgi_follow_refresh
    assert settings.capture.local_target == {}
    assert settings.kmbox.target == {}


def test_legacy_default_still_uses_capture_card_and_kmbox():
    settings = Settings().validate()
    assert settings.capture.source == 'card'
    assert settings.kmbox.protocol == 'kmbox_net'


def test_win32_primary_monitor_and_optional_sendinput_window_round_trip(tmp_path):
    settings = Settings()
    settings.capture.source = 'desktop'
    settings.capture.local_backend = 'WIN32'
    settings.kmbox.protocol = 'sendinput'
    settings.kmbox.dry_run = False
    settings.validate()
    settings.kmbox.target_enabled = True
    with pytest.raises(ValueError):
        settings.validate()
    settings.kmbox.target = {'kind': 'window', 'title': 'test'}
    path = tmp_path / 'advanced.json'
    settings.save(path)
    assert Settings.load(path) == settings
    settings.kmbox.target_enabled = False
    settings.kmbox.target = {}
    settings.validate()

from types import SimpleNamespace

import cv2
import pytest

from dualpc.settings import CaptureSettings
from dualpc.video_devices import select_video


def test_capture_name_match_selects_gc573_instead_of_camera_zero():
    devices = [SimpleNamespace(index=0, backend=cv2.CAP_DSHOW, name="Integrated Camera"),
               SimpleNamespace(index=2, backend=cv2.CAP_DSHOW, name="AVerMedia HD Capture GC573 1")]
    assert select_video(CaptureSettings(), lambda backend: devices) == (2, cv2.CAP_DSHOW, "AVerMedia HD Capture GC573 1")


def test_missing_capture_never_falls_back_to_webcam():
    with pytest.raises(RuntimeError, match="matched 0"):
        select_video(CaptureSettings(), lambda backend: [SimpleNamespace(index=0, backend=cv2.CAP_DSHOW, name="Integrated Camera")])


def test_ambiguous_names_require_exact_selection():
    devices = [SimpleNamespace(index=i, backend=cv2.CAP_DSHOW, name=f"GC573 {i}") for i in range(2)]
    with pytest.raises(RuntimeError, match="matched 2"):
        select_video(CaptureSettings(), lambda backend: devices)
    assert select_video(CaptureSettings(device_name="GC573 1"), lambda backend: devices)[0] == 1


def test_stable_device_path_survives_index_changes_and_duplicate_names():
    devices = [SimpleNamespace(index=i, backend=cv2.CAP_DSHOW, name="USB Capture", path=f"card-{i}") for i in range(2)]
    settings = CaptureSettings(device_index=0, device_name="USB Capture", device_path="card-1")
    assert select_video(settings, lambda backend: devices)[0] == 1
    with pytest.raises(RuntimeError, match="unavailable"):
        select_video(settings, lambda backend: devices[:1])

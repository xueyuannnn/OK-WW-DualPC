import cv2


def enumerate_video(backend="DSHOW"):
    from cv2_enumerate_cameras import enumerate_cameras
    api = cv2.CAP_DSHOW if backend in {"DSHOW", "FFMPEG"} else cv2.CAP_MSMF
    return enumerate_cameras(api)


def select_video(settings, enumerator=enumerate_video):
    api = cv2.CAP_DSHOW if settings.backend in {"DSHOW", "FFMPEG"} else cv2.CAP_MSMF
    if not settings.device_name and not settings.device_path:
        return settings.device_index, api, f"#{settings.device_index}"
    devices = enumerator(settings.backend)
    if settings.device_path:
        matches = [d for d in devices if getattr(d, "path", "").casefold() == settings.device_path.casefold()]
        if len(matches) != 1:
            raise RuntimeError("The selected capture device is unavailable; reconnect it or select another device in 双机设置")
        selected = matches[0]
        return selected.index, selected.backend, selected.name
    name = settings.device_name.casefold()
    matches = [d for d in devices if d.name.casefold() == name]
    if not matches:
        matches = [d for d in devices if name in d.name.casefold()]
    if len(matches) != 1:
        raise RuntimeError(f"Video device name {settings.device_name!r} matched {len(matches)} devices; available: {[d.name for d in devices]}")
    selected = matches[0]
    return selected.index, selected.backend, selected.name

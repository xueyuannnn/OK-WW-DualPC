"""Read DirectShow media types without starting a capture graph.

Windows ABI references:
https://learn.microsoft.com/windows/win32/api/strmif/nf-strmif-iamstreamconfig-getstreamcaps
https://learn.microsoft.com/windows/win32/api/strmif/nf-strmif-iamvideocontrol-getframeratelist
All COM pointers and media-type allocations belong to this short-lived probe.
"""
import ctypes as ct
from ctypes import wintypes as wt
import struct
import uuid


class GUID(ct.Structure):
    _fields_ = [("bytes", ct.c_ubyte * 16)]

    def __init__(self, value):
        super().__init__()
        self.bytes[:] = uuid.UUID(value).bytes_le

    def __str__(self):
        return str(uuid.UUID(bytes_le=bytes(self.bytes)))


class MediaType(ct.Structure):
    _fields_ = [("major", GUID), ("subtype", GUID), ("fixed", wt.BOOL),
                ("temporal", wt.BOOL), ("sample_size", wt.ULONG), ("format_type", GUID),
                ("unknown", ct.c_void_p), ("format_size", wt.ULONG), ("format", ct.c_void_p)]


SYSTEM_ENUM = "62be5d10-60eb-11d0-bd3b-00a0c911ce86"
DEVICE_ENUM = "29840822-5b84-11d0-bd3b-00a0c911ce86"
VIDEO_INPUT = "860bb310-5d01-11d0-bd3b-00a0c911ce86"
BASE_FILTER = "56a86895-0ad4-11ce-b03a-0020af0ba770"
STREAM_CONFIG = "c6e13340-30ac-11d0-a18c-00a0c9118956"
VIDEO_CONTROL = "6a2e0670-28e4-11d0-a18c-00a0c9118956"
PROPERTY_BAG = "55272a00-42cb-11ce-8135-00aa004bb851"
VIDEO_INFO = "05589f80-c356-11ce-bf01-00aa0055595a"
VIDEO_INFO2 = "f72a76a0-eb0a-11d0-ace4-0000c0cc16ba"


def _call(pointer, slot, *types):
    address = ct.cast(pointer, ct.POINTER(ct.POINTER(ct.c_void_p))).contents[slot]
    return ct.WINFUNCTYPE(ct.c_long, ct.c_void_p, *types)(address)


def _check(result):
    if result < 0:
        raise OSError(f"DirectShow 返回错误 0x{result & 0xffffffff:08X}")


def _release(pointer):
    if pointer:
        _call(pointer, 2)(pointer)


def _query(pointer, iid):
    result = ct.c_void_p()
    _check(_call(pointer, 0, ct.POINTER(GUID), ct.POINTER(ct.c_void_p))(
        pointer, ct.byref(GUID(iid)), ct.byref(result)))
    return result


def _next(enumerator):
    result, fetched = ct.c_void_p(), wt.ULONG()
    _check(_call(enumerator, 3, wt.ULONG, ct.POINTER(ct.c_void_p), ct.POINTER(wt.ULONG))(
        enumerator, 1, ct.byref(result), ct.byref(fetched)))
    return result if fetched.value else None


def _properties(moniker):
    from comtypes.automation import VARIANT
    bag = ct.c_void_p()
    _check(_call(moniker, 9, ct.c_void_p, ct.c_void_p, ct.POINTER(GUID), ct.POINTER(ct.c_void_p))(
        moniker, None, None, ct.byref(GUID(PROPERTY_BAG)), ct.byref(bag)))
    values = {}
    try:
        for key in ("FriendlyName", "DevicePath"):
            value = VARIANT()
            result = _call(bag, 3, ct.c_wchar_p, ct.POINTER(VARIANT), ct.c_void_p)(
                bag, key, ct.byref(value), None)
            if result >= 0:
                values[key] = value.value or ""
    finally:
        _release(bag)
    return values


def _parse_media(media):
    """Use AM_MEDIA_TYPE, not the deprecated VIDEO_STREAM_CONFIG_CAPS fields."""
    kind = str(media.format_type)
    offset = 48 if kind == VIDEO_INFO else 72 if kind == VIDEO_INFO2 else None
    if offset is None or not media.format or media.format_size < offset + 40:
        return None
    data = ct.string_at(media.format, media.format_size)
    width, height = struct.unpack_from("<ii", data, offset + 4)
    interval = struct.unpack_from("<q", data, 40)[0]
    if width <= 0 or not height or interval <= 0:
        return None
    compression = data[offset + 16:offset + 20]
    if all(32 <= byte < 127 for byte in compression):
        fourcc = compression.decode("ascii")
    else:
        # Uncompressed DIB media types use BI_RGB=0 instead of a FourCC.
        bits = struct.unpack_from("<H", data, offset + 14)[0]
        fourcc = {24: "RGB3", 32: "RGB4"}.get(bits, "")
    if not fourcc:
        return None
    return {"width": width, "height": abs(height), "fourcc": fourcc,
            "fps": _fps(interval)}


def _fps(interval):
    value = 10_000_000 / interval
    # A 100-ns integer interval can represent 144 Hz as 144.0009 Hz.
    return round(value) if abs(value - round(value)) < value * 0.0001 else round(value, 3)


def _pin_modes(pin, video_control, ole):
    config = _query(pin, STREAM_CONFIG)
    try:
        count, size = ct.c_int(), ct.c_int()
        _check(_call(config, 5, ct.POINTER(ct.c_int), ct.POINTER(ct.c_int))(
            config, ct.byref(count), ct.byref(size)))
        if not 0 < size.value <= 65536 or not 0 <= count.value <= 4096:
            return []
        buffer = ct.create_string_buffer(size.value)
        result = []
        for index in range(count.value):
            media = ct.POINTER(MediaType)()
            try:
                _check(_call(config, 6, ct.c_int, ct.POINTER(ct.POINTER(MediaType)), ct.c_void_p)(
                    config, index, ct.byref(media), buffer))
                if not media:
                    continue
                mode = _parse_media(media.contents)
                if mode is None:
                    continue
                result.append(mode)
                if video_control:
                    rates, length = ct.POINTER(ct.c_longlong)(), wt.LONG()
                    try:
                        status = _call(video_control, 8, ct.c_void_p, wt.LONG, wt.SIZE,
                                       ct.POINTER(wt.LONG), ct.POINTER(ct.POINTER(ct.c_longlong)))(
                            video_control, pin, index, wt.SIZE(mode["width"], mode["height"]),
                            ct.byref(length), ct.byref(rates))
                        if status >= 0 and rates and 0 < length.value <= 4096:
                            result.extend(dict(mode, fps=_fps(rates[i]))
                                          for i in range(length.value) if rates[i] > 0)
                    finally:
                        if rates:
                            ole.CoTaskMemFree(rates)
            finally:
                if media:
                    _release(media.contents.unknown)
                    ole.CoTaskMemFree(media.contents.format)
                    ole.CoTaskMemFree(media)
        return result
    finally:
        _release(config)


def _filter_modes(moniker, ole):
    device, pins, video_control = ct.c_void_p(), ct.c_void_p(), None
    try:
        _check(_call(moniker, 8, ct.c_void_p, ct.c_void_p, ct.POINTER(GUID), ct.POINTER(ct.c_void_p))(
            moniker, None, None, ct.byref(GUID(BASE_FILTER)), ct.byref(device)))
        _check(_call(device, 10, ct.POINTER(ct.c_void_p))(device, ct.byref(pins)))
        try:
            video_control = _query(device, VIDEO_CONTROL)
        except OSError:
            pass
        result = []
        while pin := _next(pins):
            try:
                direction = ct.c_int()
                _check(_call(pin, 9, ct.POINTER(ct.c_int))(pin, ct.byref(direction)))
                if direction.value == 1:
                    try:
                        result.extend(_pin_modes(pin, video_control, ole))
                    except OSError:
                        continue  # Some preview pins do not expose IAMStreamConfig.
            finally:
                _release(pin)
        return sorted({(m["width"], m["height"], m["fps"], m["fourcc"]): m for m in result}.values(),
                      key=lambda m: (m["width"] * m["height"], m["fps"], m["fourcc"]))
    finally:
        _release(video_control)
        _release(pins)
        _release(device)


def enumerate_modes(name, path=""):
    """Return only modes reported by the selected device. Never start video/input."""
    ole = ct.WinDLL("ole32")
    ole.CoInitializeEx.argtypes = [ct.c_void_p, wt.DWORD]
    ole.CoCreateInstance.argtypes = [ct.POINTER(GUID), ct.c_void_p, wt.DWORD,
                                    ct.POINTER(GUID), ct.POINTER(ct.c_void_p)]
    ole.CoTaskMemFree.argtypes = [ct.c_void_p]
    _check(ole.CoInitializeEx(None, 2))
    system, devices = ct.c_void_p(), ct.c_void_p()
    try:
        _check(ole.CoCreateInstance(ct.byref(GUID(SYSTEM_ENUM)), None, 1,
                                    ct.byref(GUID(DEVICE_ENUM)), ct.byref(system)))
        _check(_call(system, 3, ct.POINTER(GUID), ct.POINTER(ct.c_void_p), wt.DWORD)(
            system, ct.byref(GUID(VIDEO_INPUT)), ct.byref(devices), 0))
        if not devices:
            raise RuntimeError("未发现视频设备")
        while moniker := _next(devices):
            try:
                properties = _properties(moniker)
                matches = (properties.get("DevicePath", "").casefold() == path.casefold()
                           if path else properties.get("FriendlyName") == name)
                if matches:
                    return _filter_modes(moniker, ole)
            finally:
                _release(moniker)
        raise RuntimeError("设备已拔出或驱动未提供模式信息，请刷新设备")
    finally:
        _release(devices)
        _release(system)
        ole.CoUninitialize()

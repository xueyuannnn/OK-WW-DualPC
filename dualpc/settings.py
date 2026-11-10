from __future__ import annotations

import json
import math
from dataclasses import asdict, dataclass, field
from pathlib import Path


@dataclass
class CaptureSettings:
    device_index: int = 0
    backend: str = "DSHOW"
    width: int = 1920
    height: int = 1080
    fps: float = 60
    fourcc: str = ""
    crop: list[int] | None = None
    output_width: int = 1920
    output_height: int = 1080
    stale_after: float = 0.15
    device_name: str = "GC573"
    device_path: str = ""
    ffmpeg_path: str = "ffmpeg"


@dataclass
class BoxSettings:
    protocol: str = "kmbox_net"
    host: str = ""
    port: int = 0
    uuid: str = ""
    timeout: float = 0.3
    dry_run: bool = True
    desktop_width: int = 1920
    desktop_height: int = 1080
    counts_per_pixel_x: float = 1.0
    counts_per_pixel_y: float = 1.0
    home_counts: int = 32767
    home_wait: float = 0.05
    move_chunk: int = 512
    move_interval: float = 0.002


@dataclass
class Settings:
    capture: CaptureSettings = field(default_factory=CaptureSettings)
    kmbox: BoxSettings = field(default_factory=BoxSettings)
    game_language: str = "zh_CN"

    def validate(self):
        c, b = self.capture, self.kmbox
        if c.backend not in {"DSHOW", "MSMF", "FFMPEG"}:
            raise ValueError("capture.backend must be DSHOW, MSMF or FFMPEG")
        if c.backend == "FFMPEG" and not c.device_name:
            raise ValueError("FFMPEG DirectShow capture requires the exact video device name")
        if c.device_index < 0 or min(c.width, c.height, c.output_width, c.output_height, c.fps) <= 0:
            raise ValueError("Invalid capture index, resolution or frame rate")
        if not math.isfinite(c.fps):
            raise ValueError("Capture frame rate must be finite")
        if abs(c.output_width / c.output_height - 16 / 9) > 0.01 or c.output_width < 1280 or c.output_height < 720:
            raise ValueError("OK-WW requires a 16:9 inference image of at least 1280x720")
        if not math.isfinite(c.stale_after) or c.stale_after <= 0:
            raise ValueError("stale_after must be positive")
        if c.fourcc and len(c.fourcc) != 4:
            raise ValueError("fourcc must contain four characters or be empty")
        if c.crop is not None and (len(c.crop) != 4 or min(c.crop[:2]) < 0 or min(c.crop[2:]) <= 0):
            raise ValueError("crop must be [x, y, width, height] in source pixels")
        if min(b.desktop_width, b.desktop_height) <= 0 or not 1 <= b.move_chunk <= 32767 or not 1 <= b.home_counts <= 32767:
            raise ValueError("Invalid desktop size or movement limits")
        if any(not math.isfinite(v) or v <= 0 for v in (b.timeout, b.counts_per_pixel_x, b.counts_per_pixel_y)):
            raise ValueError("Timeout and cursor scales must be positive and finite")
        if not math.isfinite(b.move_interval) or not 0 <= b.move_interval <= 0.1:
            raise ValueError("move_interval must be between 0 and 0.1 seconds")
        if not math.isfinite(b.home_wait) or not 0 <= b.home_wait <= 1:
            raise ValueError("home_wait must be between 0 and 1 second")
        if b.protocol != "kmbox_net":
            raise ValueError("Unknown input protocol; select KMBox Net")
        if not b.dry_run and b.protocol == "kmbox_net":
            import ipaddress
            ipaddress.IPv4Address(b.host)
            if not 1 <= b.port <= 65535 or len(b.uuid) != 8:
                raise ValueError("Fill in the port and 8-digit hexadecimal UUID shown on KMBox Net")
            int(b.uuid, 16)
        if self.game_language not in {"zh_CN", "zh_TW", "en_US"}:
            raise ValueError("Unsupported game language")
        return self

    @classmethod
    def load(cls, path: str | Path):
        data = json.loads(Path(path).read_text(encoding="utf-8-sig"))
        unknown = set(data) - {"capture", "kmbox", "game_language"}
        if unknown:
            raise ValueError(f"Unknown configuration fields: {sorted(unknown)}")
        box = dict(data.get("kmbox", {}))
        # Reuse retained KMBox network settings from a retired serial profile.
        if box.get("protocol") == "makcu_serial":
            box["protocol"] = "kmbox_net"
        for key in ("hid_vid", "hid_pid", "hid_path", "serial_port", "baudrate"):
            box.pop(key, None)
        return cls(CaptureSettings(**data.get("capture", {})), BoxSettings(**box),
                   data.get("game_language", "zh_CN")).validate()

    def save(self, path: str | Path):
        self.validate()
        path = Path(path)
        temporary = path.with_suffix(".tmp")
        temporary.write_text(json.dumps(asdict(self), indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        temporary.replace(path)

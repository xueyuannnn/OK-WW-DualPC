"""JSON probes run in isolated processes so driver delays never block the UI."""
import json
import sys


def discover_devices(backend):
    from .video_devices import enumerate_video
    result = {"video": [], "serial": [], "warnings": []}
    try:
        result["video"] = [{"index": d.index, "name": d.name, "path": d.path or ""}
                           for d in enumerate_video(backend)]
    except Exception as error:
        result["warnings"].append(f"视频设备：{error}")
    try:
        from serial.tools.list_ports import comports
        result["serial"] = [{"port": p.device, "description": p.description} for p in comports()]
    except Exception as error:
        result["warnings"].append(f"串口设备：{error}")
    return result


def main():
    try:
        if sys.argv[1] == "devices":
            result = discover_devices(sys.argv[2])
        elif sys.argv[1] == "makcu":
            from .makcu import probe_serial
            result = probe_serial(sys.argv[2])
        else:
            from .native_modes import enumerate_modes
            device = json.loads(sys.argv[2])
            result = {"modes": enumerate_modes(device["name"], device.get("path", ""))}
        print(json.dumps(result, ensure_ascii=True))
    except Exception as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=True))
        raise SystemExit(1)


if __name__ == "__main__":
    main()

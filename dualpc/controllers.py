"""Factories for the two supported dual-PC input transports."""
from .kmbox import KMBoxNet

SUPPORTED = {
    "kmbox_net": "KMBox Net · UDP",
    "makcu_serial": "MAKCU V4 · Serial",
}


def create_controller(settings):
    if settings.protocol == "kmbox_net":
        return KMBoxNet(settings)
    if settings.protocol == "makcu_serial":
        from .makcu import MakcuSerial
        return MakcuSerial(settings)
    raise ValueError(f"Unsupported controller protocol: {settings.protocol}; select MAKCU or KMBox Net")

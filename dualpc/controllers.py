"""Factory for the supported dual-PC input transport."""
from .kmbox import KMBoxNet

SUPPORTED = {
    "kmbox_net": "KMBox Net · UDP",
}


def create_controller(settings):
    if settings.protocol == "kmbox_net":
        return KMBoxNet(settings)
    raise ValueError(f"Unsupported controller protocol: {settings.protocol}; select KMBox Net")

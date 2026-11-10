"""An application-specific process lock; does not stop unrelated OK-WW instances."""
import msvcrt
from pathlib import Path


class SessionLock:
    def __init__(self, root):
        self.path = Path(root) / ".dualpc-session.lock"
        self.handle = None

    def __enter__(self):
        self.handle = self.path.open("a+b")
        if self.path.stat().st_size == 0:
            self.handle.write(b"0")
            self.handle.flush()
        self.handle.seek(0)
        try:
            msvcrt.locking(self.handle.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            self.handle.close()
            self.handle = None
            raise RuntimeError("This dual-PC application is already running") from None
        return self

    def __exit__(self, *args):
        if self.handle is not None:
            self.handle.seek(0)
            msvcrt.locking(self.handle.fileno(), msvcrt.LK_UNLCK, 1)
            self.handle.close()
            self.handle = None

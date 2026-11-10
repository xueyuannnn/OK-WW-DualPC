import pytest

from dualpc.session import SessionLock


def test_second_instance_is_rejected_and_lock_is_reusable(tmp_path):
    with SessionLock(tmp_path):
        with pytest.raises(RuntimeError, match="already running"):
            with SessionLock(tmp_path):
                pass
    with SessionLock(tmp_path):
        pass

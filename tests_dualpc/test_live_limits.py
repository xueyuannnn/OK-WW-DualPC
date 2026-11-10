from unittest.mock import Mock

import pytest

from tools_dualpc.validate_task_live import (EchoLimitReached, install_echo_limit,
                                           EnhancementStageLimitReached, install_one_stage_limit)


def test_enhance_limit_waits_for_discard_to_finish_and_exits_before_next_echo():
    task = type("EnhanceEchoTask", (), {})()
    phases = []
    task.trash_and_esc = lambda: phases.extend(("counter", "discard", "escape"))
    task.lock_and_esc = Mock()
    task.find_echo_enhance = lambda: phases.append("bag_visible") or True
    report = {}
    install_echo_limit(task, 1, report)
    with pytest.raises(EchoLimitReached):
        task.trash_and_esc()
        phases.append("next_echo")
    assert phases == ["counter", "discard", "escape", "bag_visible"]
    assert report == {"completed_echoes": 1, "echo_limit_reached": True}


def test_failed_or_incomplete_operation_is_not_counted():
    task = type("EnhanceEchoTask", (), {})()
    task.trash_and_esc = Mock(side_effect=RuntimeError("discard failed"))
    task.lock_and_esc = Mock()
    task.find_echo_enhance = Mock(return_value=False)
    report = {}
    install_echo_limit(task, 1, report)
    with pytest.raises(RuntimeError, match="discard failed"):
        task.trash_and_esc()
    assert report == {"completed_echoes": 0}
    with pytest.raises(RuntimeError, match="未确认返回背包"):
        task.lock_and_esc()
    assert report == {"completed_echoes": 0}


def test_reconstruction_limit_counts_only_success_after_bag_return():
    task = type("ChangeEchoTask", (), {})()
    task.info_incr = Mock()
    task.find_echo_enhance = Mock(return_value=True)
    report = {}
    install_echo_limit(task, 1, report)
    task.info_incr("失败声骸数量")
    assert report == {"completed_echoes": 0}
    task.find_echo_enhance.assert_not_called()
    with pytest.raises(EchoLimitReached):
        task.info_incr("成功声骸数量")
    assert report["completed_echoes"] == 1
    task.info_incr.__wrapped__.assert_called_with("成功声骸数量")


def test_first_stage_limit_keeps_selected_echo_and_stops_before_discard_or_next_stage():
    from types import SimpleNamespace
    task = type("EnhanceEchoTask", (), {})()
    task.is_0_level = Mock(return_value=True)
    task.check_echo_stats = Mock(return_value=False)
    task.trash_and_esc = Mock()
    report = {}
    install_one_stage_limit(task, report)
    assert task.select_0_level_echo is task.is_0_level
    with pytest.raises(EnhancementStageLimitReached):
        if not task.check_echo_stats([SimpleNamespace(name="生命")], [SimpleNamespace(name="450")]):
            task.trash_and_esc()
    task.trash_and_esc.assert_not_called()
    assert report["enhancement_stage_limit_reached"] is True
    assert report["enhancement_stage"] == {"properties": ["生命"], "values": ["450"], "decision": False}

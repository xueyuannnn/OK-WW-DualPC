import xml.etree.ElementTree as ET

from dualpc.schedule import install_scheduler_hook


def test_schedule_xml_starts_dualpc_without_creating_system_tasks(monkeypatch):
    from ok.util.windows_schedule import TriggerType, WindowsScheduleManager
    manager = object.__new__(WindowsScheduleManager)
    monkeypatch.setattr(manager, "_resolve_current_user_id", lambda: "TEST_USER")
    install_scheduler_hook()
    wrapped = WindowsScheduleManager._generate_task_xml
    install_scheduler_hook()
    assert WindowsScheduleManager._generate_task_xml is wrapped
    xml = manager._generate_task_xml("unit-test", 1, TriggerType.DAILY, task_identifier="src.task.DailyTask2.DailyTask2")
    root = ET.fromstring(xml)
    arguments = root.find("{*}Actions/{*}Exec/{*}Arguments").text
    assert arguments == "main_dualpc.py -t src.task.DailyTask2.DailyTask2 -e"

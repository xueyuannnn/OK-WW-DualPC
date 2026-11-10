"""Keep the upstream task scheduler pointed at the dual-PC entrypoint."""


def install_scheduler_hook():
    from ok.util.windows_schedule import WindowsScheduleManager
    original = WindowsScheduleManager._generate_task_xml
    if getattr(original, "_dualpc_entrypoint", False):
        return

    def generate(self, *args, **kwargs):
        xml = original(self, *args, **kwargs)
        expected = "<Arguments>main.py "
        if xml.count(expected) != 1:
            raise RuntimeError("Framework scheduler entrypoint format changed; review the dual-PC scheduling adapter")
        return xml.replace(expected, "<Arguments>main_dualpc.py ", 1)

    generate._dualpc_entrypoint = True
    WindowsScheduleManager._generate_task_xml = generate

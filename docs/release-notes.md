# 0.2.0-beta.8 · Rust 内核

业务内核改为 Rust，沿用原有任务流程、连招时序和恢复逻辑，界面与操作方式保持不变。

- Rust 负责内置战斗、任务调度、识别策略、采集恢复和输入控制。
- C++ 保留 Qt 界面及 Windows、OpenCV、OpenVINO 的 SDK 适配。
- Python 工坊和通用脚本继续兼容，原有配置可直接沿用。

已通过离线回归测试。真实设备和游戏内表现仍需实测。

下载 `OK-WW-DualPC-full.zip`，完整解压后运行 `OKWW-DualPC-Native-Dev.exe`，无需另装 Rust 或 Python。更新前备份旧目录中的 `native-full-data`，再复制到新版目录。

源码：`OK-WW-DualPC-source.zip`；校验：`SHA256SUMS.txt`。

反馈：[lidada0202@gmail.com](mailto:lidada0202@gmail.com)

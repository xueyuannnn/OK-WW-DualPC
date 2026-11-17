# 0.2.0-beta.10

整理 Rust 工作区和构建目录，界面、任务流程及操作方式保持不变。

- 业务模块按采集、输入、任务、队伍和工坊等职责分组。
- Qt 界面、平台适配及行为对照测试独立整理。
- 保留 Python 工坊接口，更新源码构建说明。

已通过离线回归测试。真实设备和游戏内表现仍需实测。

下载 `OK-WW-DualPC-full.zip`，完整解压后运行 `OKWW-DualPC-Native-Dev.exe`，无需另装 Rust 或 Python。更新前备份旧目录中的 `native-full-data`，再复制到新版目录。

源码：`OK-WW-DualPC-source.zip`；校验：`SHA256SUMS.txt`。

反馈：[lidada0202@gmail.com](mailto:lidada0202@gmail.com)

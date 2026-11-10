# 完整包

完整包包含原生主程序、Qt / OpenCV / OpenVINO 动态库、识别资源、许可证和独立 Python 工坊运行环境。解压整个目录后运行 `OKWW-DualPC-Native-Dev.exe`。

界面、采集、设备输入、内置角色与任务在 C++ 中执行。只有工坊相关代码使用 Python；运行环境放在 `workshop-engine/`，不要求预装系统 Python。

配置写入程序旁的 `native-full-data/`。普通包首次运行使用默认配置与演练模式，不附带账号、设备凭据或导入队伍。

完整包内的 `source/` 是对应源码，构建说明位于 `source/docs/development.md`。`PACKAGE.json` 记录文件大小与 SHA-256，可用于检查解压后的文件。运行时产生的配置不属于发布文件清单。

设备连接、任务与工坊使用方法见 [项目首页](../README.md)。

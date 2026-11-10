# 原生主程序

C++20 / Qt 6 主程序负责界面、采集、设备输入、内置角色和任务执行。角色工坊和通用 Python 脚本通过独立兼容进程接入。

| 目录 | 内容 |
| --- | --- |
| `src/` | Qt 界面、DirectShow / Media Foundation 采集、盒子协议 |
| `src/runtime/` | 任务执行、角色连招、识别、调度、通知和工坊进程管理 |
| `bridge/` | Python 工坊与通用任务加载、脚本包管理和原生输入桥接 |
| `resources/` | 任务与角色元数据、图标、头像和资源清单 |
| `tests/` | 原生模块、任务、界面和工坊桥接测试 |
| `third_party/` | Clipper 及其许可证 |
| `licenses/` | Qt 许可文本 |
| `tools/` | 构建和离线检查工具 |

默认构建目标为 `OKWWNativeDev`，输出 `OKWW-DualPC-Native-Dev.exe`。旧兼容宿主目标 `OKWWDualPC` 保留在 CMake 中，标准构建关闭它。独立 Python 工坊不需要启用旧宿主目标。

构建方式见 [开发说明](../docs/development.md)，使用方式见 [项目首页](../README.md)。

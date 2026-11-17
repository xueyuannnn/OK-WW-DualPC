# 原生主程序

Rust 内核负责采集控制、设备输入、内置角色和任务执行，C++20 / Qt 6 保留界面与原生 SDK 适配。角色工坊和通用 Python 脚本通过独立兼容进程接入。

| 目录 | 内容 |
| --- | --- |
| `rust/` | 执行内核、角色策略、任务流程、图像规则和协议 |
| `src/` | Qt 界面与 Windows、采集和输入 SDK 适配 |
| `src/runtime/` | 运行时接口、SDK 对象与 Python 工坊桥接 |
| `bridge/` | Python 工坊与通用任务加载、脚本包管理和原生输入桥接 |
| `resources/` | 任务与角色元数据、图标、头像和资源清单 |
| `tests/` | 原生模块、任务、界面和工坊桥接测试 |
| `third_party/` | Clipper 及其许可证 |
| `licenses/` | Qt、Rust 标准库许可和第三方声明 |
| `tools/` | 构建和离线检查工具 |

默认构建目标为 `OKWWNativeDev`，输出 `OKWW-DualPC-Native-Dev.exe`。旧兼容宿主目标 `OKWWDualPC` 保留在 CMake 中，标准构建关闭它。独立 Python 工坊不需要启用旧宿主目标。

构建方式见 [开发说明](../docs/development.md)，使用方式见 [项目首页](../README.md)。

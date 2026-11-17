# Windows 宿主

此目录提供 Rust 内核的桌面宿主：Qt 界面、Windows 设备接口和图像 SDK 绑定。业务代码从根目录的 Cargo 工作区构建，入口为 [`okww-core`](../crates/okww-core/)。

| 目录 | 内容 |
| --- | --- |
| `ui/` | Qt 页面、对话框、控件和窗口入口 |
| `platform/` | Windows 采集、键鼠、桌面和更新接口 |
| `host/runtime/` | Rust 调用边界、SDK 对象与工坊进程宿主 |
| `include/` | 宿主公共头文件和 ABI 类型 |
| `resources/` | 任务与角色元数据、图标、头像和资源清单 |
| `tests/` | 宿主、界面和跨语言接口测试 |
| `third_party/` | Clipper 及其许可证 |
| `licenses/` | Qt、Rust 标准库许可和第三方声明 |
| `tools/` | 构建和离线检查工具 |

默认构建目标为 `OKWWNativeDev`，输出 `OKWW-DualPC-Native-Dev.exe`。此文件名为更新器兼容入口，普通包和个人包使用同一名称；是否带个人数据由配置目录决定。旧兼容宿主目标 `OKWWDualPC` 默认关闭。

宿主不另行实现角色连招和任务决策。新增业务规则应写入 Rust 模块；此处只处理对象生命周期、界面更新和平台调用。Python 工坊入口位于 [`workshop/bridge`](../workshop/bridge/)，冻结的行为对照代码位于 [`tests/parity`](../tests/parity/)。

构建方式见 [开发说明](../docs/development.md)，使用方式见 [项目首页](../README.md)。

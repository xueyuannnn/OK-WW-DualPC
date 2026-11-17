# okww-core

OK-WW-DualPC 的 Rust 业务内核。普通 Rust 模块实现规则和状态机，`services` 负责调用宿主接口、串联执行步骤；桌面宿主通过 C ABI 接入。

| 模块 | 内容 |
| --- | --- |
| `engine`、`execution`、`lifecycle` | 命令路由、任务状态、取消和设备恢复 |
| `rotation`、`combat`、`session` | 角色连招、队伍切换与战斗状态 |
| `taskflow`、`farming`、`garden` | 任务决策、副本领奖和周常策略 |
| `capture_math`、`input`、`protocol` | 采集控制、键鼠状态与设备协议 |
| `services` | 按业务分组的宿主调用与执行流程 |
| `ffi`、`abi/` | 跨语言接口和操作编号定义 |

`services` 分为 `application`、`capture`、`input`、`runtime`、`tasks`、`teams`、`vision`、`workshop`、`notifications` 和 `updates`。界面控件及 Windows / OpenCV / OpenVINO 对象由宿主持有。

ABI 调用为同步调用。宿主异常在 C++ 边界捕获，Rust 决定恢复或结束流程；取消、按键释放和暂停仍由统一的生命周期控制。ABI 操作编号用于兼容宿主，不应因文件整理而重排。

在仓库根目录执行：

```sh
cargo fmt --all --check
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo test --workspace --locked
```

桌面宿主与行为对照测试见[开发说明](../../docs/development.md)。

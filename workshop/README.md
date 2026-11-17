# 工坊执行环境

`bridge/` 提供角色队伍包、通用 Python 脚本、脚本录制数据和代码库管理的进程入口。桌面宿主管理进程生命周期，Python 引擎执行作者代码，通过桥接接口取得画面与发送输入。

- `workshop_worker.py`：角色工坊执行入口。
- `script_worker.py`：通用脚本执行入口。
- `workshop_manager.py`、`script_manager.py`：代码库与脚本包管理。
- `native_io.py`、`workshop_protocol.py`：共享画面与宿主通信。

根目录的 `src/`、`dualpc/` 与 `vendor/ok-script/` 保留既有工坊的导入路径。不要为整理目录而改写用户队伍包的 Python 导入；新桥接功能放在本目录，通过协议暴露。

完整包内的位置为 `workshop-engine/workshop/bridge/`。源码运行使用仓库 `.venv`，发布包使用便携 Python；个人队伍和运行配置不属于本目录。

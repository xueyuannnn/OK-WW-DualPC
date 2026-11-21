# 源码构建

## 环境

- Windows 10／11 x64，Visual Studio 2022 的 C++ 桌面开发组件与 Windows SDK。
- Rust 1.100.0，使用 `x86_64-pc-windows-msvc` 工具链，版本固定在 `rust-toolchain.toml`。
- Qt 6.8 或更新版本的 MSVC 2022 x64 SDK，包含 Qt Test。
- OpenCV 4.12 x64 SDK。
- OpenVINO Runtime 开发文件。现有完整包使用 2026.4.1。
- Python 3.12，用于工坊引擎和 Python 测试。原生内置任务不依赖 Python 开发头文件。

仓库直接包含 `vendor/ok-script` 框架源码和识别资源，无需初始化子模块。构建产物、SDK、个人配置和导入队伍不在源码中。

## Python 工坊环境

在仓库根目录创建虚拟环境，再安装固定框架快照和依赖：

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dualpc.txt
```

开发时工坊使用 `.venv/Scripts/python.exe`。完整包使用 `workshop-engine/runtime/python.exe`。使用项目本地解释器运行 Python 命令。

## 原生构建

仓库根目录的 `Cargo.toml` 是业务内核的构建入口。`crates/okww-core/src/services/` 按任务、队伍、采集、输入、识别、工坊和更新等职责分组。Qt 页面在 `native/ui/`，平台接口在 `native/platform/`，对象宿主在 `native/host/`。新增规则写在 Rust 中，再由宿主提供所需的 SDK 操作。

界面使用 Qt Widgets，业务执行位于 `crates/okww-core/`。任务调度、角色连招、识别策略、采集与输入控制、设备恢复、配置、定时任务、工坊通信、通知和更新流程由 Rust 执行。Python 工坊、通用脚本及两项活动流程保留独立执行引擎。

C++ 层保留 Qt 界面、Windows／COM、OpenCV 和 OpenVINO 对象及 SDK 调用。Rust 通过同步 C ABI 借用这些对象，不持有 Qt 指针；原生异常由宿主捕获并保存，返回 Rust 后决定恢复或收尾，再在原生调用边界抛出。图像像素运算使用有长度与步长校验的缓冲区接口。整数时间戳、帧序号和系统句柄不通过浮点参数传递。

角色代码页沿用原有文件选择器，可查看 C++ 适配层、Rust 连招和公共战斗规则。工坊的 Python 编辑、导入导出和按队伍切换方式保持一致。普通源码不包含本地私有队伍；本地队伍单独链接执行库，不进入普通包。

源码构建前通过 [rustup](https://rust-lang.org/tools/install/) 安装固定工具链：

```powershell
rustup toolchain install 1.100.0 --profile minimal --component rustfmt,clippy
cargo test --workspace --locked
```

CMake 会编译并链接 Rust 静态库，普通用户运行完整包无需安装 Rust。标准 Cargo 路径由 CMake 自动查找，也可通过 `-DOKWW_CARGO=<cargo.exe>` 指定；本机独立工具链可以放在项目的 `.rust-tools/` 中，该目录不进入源码包。

`QtRoot` 指向含 `bin/` 和 `lib/cmake/Qt6/` 的 Qt SDK；`OpenCvRoot` 指向含 `OpenCVConfig.cmake` 的目录；`OpenVinoRoot` 指向含 `cmake/OpenVINOConfig.cmake` 的目录。

```powershell
.\Build-Native.ps1 -QtRoot C:\Qt\6.8.3\msvc2022_64 `
    -OpenCvRoot C:\SDK\opencv\build `
    -OpenVinoRoot C:\SDK\openvino -Test
```

也可设置 `OKWW_QT_ROOT`、`OKWW_OPENCV_ROOT` 和 `OKWW_OPENVINO_ROOT`。OpenCV 默认查找 `native/deps/opencv/build`，OpenVINO 默认查找本地虚拟环境中的 `openvino` 包。`native/tools/Fetch-NativeVision.ps1` 可下载并校验 OpenCV SDK。

默认启用原生业务运行时，关闭旧兼容宿主和本地队伍 DLL。输出位于 `build-native/Release/`。`-Test` 会构建测试并运行 CTest；仅编译应用时可省略。

开发运行时，DLL 搜索路径还需要 Qt `bin`、OpenCV `x64/vc16/bin` 和 OpenVINO `libs`，或对应 SDK 的运行库目录：

```powershell
$env:PATH = 'C:\Qt\6.8.3\msvc2022_64\bin;C:\SDK\opencv\build\x64\vc16\bin;C:\SDK\openvino\libs;' + $env:PATH
.\build-native\Release\OKWW-DualPC-Native-Dev.exe --root . --data .\native-full-data
```

## 测试

CTest 覆盖设备协议、运行时、任务流程、通知、工坊和 Qt 界面。部分历史队伍测试仅在本地队伍模块存在并显式启用时构建。没有工坊解释器时，工坊运行测试会跳过；测试输出中的跳过不能视为已验证。

```powershell
.\.venv\Scripts\python.exe -m pytest native/tests/test_native_io.py -q
.\.venv\Scripts\python.exe tools_release/prepare_source.py --check
```

应用的 `--smoke <报告路径>` 使用离线画面检查界面。请同时指定临时 `--data`，避免改动日常配置。离线测试不连接采集卡、不发送真实盒子输入，不验证实际游戏胜率。

`workshop_tests` 可通过 `OKWW_WORKSHOP_TEST_ROOT` 指向解压后的普通完整包，使用包内便携 Python 验证桥接握手、任务类型、当前角色单次收尾、暂停释放和同帧复制。未设置时使用当前源码目录。测试始终在临时配置中运行，并使用模拟输入；设置 `OKWW_UI_ARTIFACTS` 可保存软件内部耗时报告。

## 发布文件

`native/RELEASE_VERSION` 是完整版本号（例如 `0.2.0-beta.10`）。发布新版本前修改此文件并重新配置、编译，Git 标签使用相同版本加 `v` 前缀。界面、更新比较和完整包清单均读取此版本；预览版按语义版本顺序比较，不按发布日期排序。

```powershell
.\.venv\Scripts\python.exe tools_release/prepare_source.py --output release-review
```

工具按 Git 可见文件生成独立源码目录、源码 ZIP 和文件校验清单，并检查本地配置、密钥特征和绝对用户路径。源码 ZIP 不能直接双击运行。

已有完整运行目录时，可用 `tools_release/prepare_binary.py --runtime <完整包目录> --native-build build-native/Release --review release-review` 整理普通完整包。此工具复用已校验的可再分发运行库，采用重新编译的原生程序和当前公开源码，排除配置、队伍和构建残留；不会从系统下载或构建 Python 运行环境。首次制作完整运行目录时，需按第三方组件的许可部署动态库和 Python 依赖。

工具只生成本地文件，不创建提交、标签、远程仓库或 GitHub Release。


## 活动与 OCR

拉海洛方块和第二索拉活动保留上游 Python 流程，通过 `ScriptRuntime` 运行。它们的 OCR 请求使用宿主刚发布的同一帧，由原生 PP-OCRv6 small 识别；活动进程不另加载 Python OCR 模型。拖动、键盘和文本输入统一通过当前 KMBox / SendInput 通道，取消时由宿主终止子进程并释放按键。通用脚本和工坊的 Python 依赖版本保持独立。

`assets/ocr/ppocrv6-small/manifest.json` 记录模型、字典、来源和 SHA-256。运行时先校验三者，再加载识别模型；不混用 v5 字典。选取的上游更新见 `UPSTREAM-UPDATES.json`，原始基线仍由 `UPSTREAM.lock.json` 记录。

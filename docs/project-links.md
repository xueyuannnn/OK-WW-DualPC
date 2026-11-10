# 项目链接与上游动态

核查日期：**2026-11-10（北京时间）**。以下为当次公开访问结果；网站状态可能随时间和网络环境变化。

## 项目与发行入口

| 项目 | 当前入口 | 核查结果 |
| --- | --- | --- |
| OK-WW | [ok-oldking/ok-wuthering-waves](https://github.com/ok-oldking/ok-wuthering-waves) | 仓库可访问，默认分支 `master`，未发现地址迁移 |
| OK-WW Enhanced | [zzc-tongji/ok-ww-enhanced](https://github.com/zzc-tongji/ok-ww-enhanced) | 仓库可访问，默认分支 `main`；[版本发布](https://github.com/zzc-tongji/ok-ww-enhanced/releases)仍在此仓库 |
| OK-WW-E 更新仓库 | [zzc-tongji/ok-ww-e](https://github.com/zzc-tongji/ok-ww-e) | 仓库可访问，默认分支 `main`，增强版的更新配置仍引用此地址；其 `releases/latest` 当前返回 404 |
| ok-script | [ok-oldking/ok-script](https://github.com/ok-oldking/ok-script) | 仓库可访问，默认分支 `master`，未发现地址迁移 |

`ok-ww-enhanced` 与 `ok-ww-e` 同时存在。增强版源码和发布页使用前者，更新配置引用后者，两个入口用途不同。

本次查到的增强版 Latest 发行是 [v3.7.19-rc.2](https://github.com/zzc-tongji/ok-ww-enhanced/releases/tag/v3.7.19-rc.2)，发布时间为 2026-11-05。其发布记录包含界面框架迁移、移除角色代码入口和多账号配置页等变化。本项目独立保留 Python 角色工坊、通用脚本与多账号日常；增强版软件发版与工坊索引更新分别检查。

增强版的 [v3.7.13-rc.0](https://github.com/zzc-tongji/ok-ww-enhanced/releases/tag/v3.7.13-rc.0) 已包含锁瞑支持。本项目选择接入提交 `726b10e65cbeb1303214774664d0277a71b7e18a` 的五个缺失角色及所需接口、素材，接入范围记录在 [character-updates.json](../native/resources/character-updates.json)。

仓库地址核查不会更换本项目的整体源代码基线。原始基线仍以 [UPSTREAM.lock.json](../UPSTREAM.lock.json) 为准，单独接入的角色及来源见[来源与组件说明](../THIRD-PARTY-DUALPC.md)。

## 角色工坊

| 入口 | 核查结果 | 本项目使用方式 |
| --- | --- | --- |
| 原仓库 `ok-oldking/ok-ww-char-code` | GitHub 公开接口返回 404，未确认新的原作者仓库地址 | 保留来源记录，不作为默认下载源 |
| 旧站 `cfokwwchar.ok-script.com` | 本次 HTTPS 握手失败 | 不作为默认入口 |
| 旧站 `okwwcharcode.ok-script.com` | 本次 HTTPS 证书域名不匹配 | 不作为默认入口 |
| [yamadafz/ok-ww-char-code](https://github.com/yamadafz/ok-ww-char-code) | 社区抢救镜像可访问，仓库说明明确其为原工坊快照 | 当前默认工坊来源 |
| [镜像全量索引](https://raw.githubusercontent.com/yamadafz/ok-ww-char-code/main/teams.json) | 索引可读取，包含 50 种配队、72 份已索引方案 | 周期同步和手动同步使用此索引 |

本次镜像主分支仍为 `0c05a0edbfbda0d0ca1483569b07c4d411167477`，与已核对的快照一致，未发现新的镜像方案。镜像可访问表示现有内容可以下载，不代表原工坊已恢复持续更新。自动同步周期和离线导入见[工坊说明](workshop.md)。

## 设备与框架参考

| 参考资料 | 当前地址 | 核查结果 |
| --- | --- | --- |
| KMBox Net 协议示例 | [厂家公开 C++ 示例](https://github.com/kvmaibox/kmboxnet/blob/main/c%2B%2B_demo/NetConfig/kmboxNet.cpp) | 可访问，文件地址有效 |
| MAKCU 协议 | [API](https://makcu.com/en/api/) · [开发者资料](https://makcu.com/en/developers/) | 均可访问；API 页标示 KM Host Protocol v3.9 与 V4.108 兼容信息 |
| 双机架构参考 | [DualPC-TensorRT-Aim-KMBOX](https://github.com/ai123lj/DualPC-TensorRT-Aim-KMBOX) | 仓库可访问，地址未变 |
| GC573 驱动 | [AVerMedia 官方下载页](https://www.avermedia.com/support/download?model_number=GC573) | 可访问，型号参数保留 |
| Media Foundation 异步采集 | [Microsoft OnReadSample 文档](https://learn.microsoft.com/en-us/windows/win32/api/mfreadwrite/nf-mfreadwrite-imfsourcereadercallback-onreadsample) | 文档可访问 |

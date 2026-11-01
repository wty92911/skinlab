# SkinLab

Windows x64 的本地外观切换实验项目。当前版本 0.3.2：自动显示对局英雄，皮肤下拉选择即生效，Insert 显示／隐藏窗口；启动时请求管理员权限。没有对局时提示打开游戏，关闭窗口时请求恢复原皮并停止会话。

普通皮肤使用共用更新流程。基础换肤已完成实际对局验证，用户也抽样验证了多个英雄；特殊模型／多形态皮肤仍需适配，全部选项可选不代表全部特殊行为已实现。项目没有免封保证。

## 在 Windows 构建和启动

需要 Python 3.10+（含 Tkinter）及 x64 MinGW-w64 GCC（C++17）。Python 运行部分只有标准库依赖；不需要 REA、原始换肤软件或 DLL 样本。

```powershell
git clone https://github.com/wty92911/skinlab.git
cd skinlab
python -m venv .venv
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-session.ps1 -Compiler "C:/你的工具链/bin/g++.exe"
.\skinlab.cmd
```

编译器已在 PATH 时可省略 `-Compiler`。构建脚本会核对 x64 目标，产物写入 `out/session/`，当前产物索引为 `out/current.json`。已载入旧 DLL 时可使用 `-Bundle session-dev1` 构建到新目录。

也可以双击 `SkinLab.pyw` 或运行 `python -m skinlab`。`skinlab-native.cmd` 是兼容启动别名。Windows UAC 提示仍需用户确认。

当前适配的国服 EXE 为 `16.21.832.5350`，SHA256：

```text
8dd49307106f45f65288425ce5291b9de4d0ad50e721d9255c0f63b888baf0ee
```

当前安装路径为 `C:/WeGameApps/英雄联盟/Game/League of Legends.exe`。更换安装位置时，需要同步修改 `skinlab/native_controller.py` 中的 `GAME` 和 `native/loader/observe_host.cpp` 中的路径检查，再重新构建。可配置路径列入后续 TODO。

## 目录数据

`skinlab/data/catalog.json` 随源码和版本一起发布，当前包含 173 个英雄、2138 个基础皮肤款式。它只保存名称、编号等信息，不包含模型、纹理或皮肤资源。运行时直接读取本地文件，不联网检查更新，也不使用旧的 `local-data` 目录缓存。

目录的作用是把“显示名称”映射为引擎使用的“皮肤编号”；编号并不连续，不能直接用下拉列表序号替代。目录更新和游戏二进制适配是两件事。游戏升级但没有新皮肤时可能只需更新二进制适配；新增皮肤时再更新目录并发新版。

维护者在准备发版时运行：

```powershell
python scripts/update_catalog.py
```

脚本从腾讯英雄联盟官网的公开数据获取目录，排除独立炫彩条目，写回仓库内的 JSON；构建时同步生成原生编号表。数据来源见 JSON 的 `source` 字段。客户端只通过本机 Live Client API 辅助识别当前英雄。

## 代码结构与检查

- `skinlab/`：精简界面、当前英雄识别、目录与会话控制。
- `native/src/`：皮肤状态事务、DX11 帧桥接、运行时特征扫描。
- `native/loader/`：Windows 会话加载及跨进程命令协议。
- `native/lab/observe_fixture.cpp`：自建 WARP 测试目标。
- `scripts/`：构建、维护目录和已有的离线流程检查。
- `docs/ADAPTATION.md`：游戏更新后如何适配；`docs/TODO.md`：待办与当前范围。

```powershell
python -m unittest discover -s tests
python scripts/verify_compact.py
```

第二条需要先构建，创建本项目自己的隐藏 WARP 进程，检查识别、选择、恢复与关闭流程；不连接游戏。没有持续维护测试台账的要求，按实际变更运行现有相关检查即可。原生构建和真实游戏验证在 Windows 上进行，其他终端可以编辑代码、维护目录和文档。

DLL 保存在项目产物目录，不写入 LoL／WeGame 文件夹。停止后模块和帧桥接保留至对局进程退出；当前不实现运行中热卸载。同一对局主动停止后暂不支持重新接入。

原始软件、第三方 DLL 样本、分析工具、历史研究和个人日志不进入仓库；旧研究仅在本机 `.archive/` 中保留。当前仓库未指定开源许可证。

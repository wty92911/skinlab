# SkinLab

Windows x64 的本地外观切换实验项目。当前版本 0.4.0：Rust + Tauri 桌面应用，自动识别对局英雄，点击皮肤卡片即生效，Insert 显示／隐藏窗口；启动时请求管理员权限。没有对局时提示打开游戏，关闭窗口时恢复原皮并停止会话。

普通皮肤使用共用更新流程。基础换肤已完成实际对局验证，用户也抽样验证了多个英雄；终极／多形态皮肤在界面上标注 `!`，特殊形态仍需适配，可选不代表特殊行为已实现。修改游戏内存违反游戏用户协议，项目没有免封保证。

## 结构

```
crates/skinlab-core     目录、已适配构建、Live Client 解析、host 协议、错误分类（纯逻辑，无平台依赖）
crates/skinlab-session  游戏发现、构建校验、host 进程驱动、自动接入状态机；含 WARP fixture 与内存 mock 后端
app/                    Tauri 2 外壳（Rust）与界面（app/ui，原生 HTML/CSS/JS，无构建步骤）
xtask/                  维护任务：原生构建、目录更新、构建标识、fixture 端到端检查
native/                 C++ host、会话 DLL、DX11 帧桥接、运行时特征扫描、WARP 测试窗口
data/catalog.json       随版本发布的皮肤目录（名称 ↔ 编号）
data/builds.json        已完成适配验证的游戏构建（版本 + SHA256）
```

运行时链路：`SkinLab.exe` → `skinlab-observe-host.exe --session`（stdin/stdout JSON 行协议）→ 游戏内 `skinlab-observe.dll`（共享内存命令，DX11 帧线程执行）。

## 在 Windows 构建

需要 Rust stable（MSVC 工具链）、x64 MinGW-w64 g++（编译 `native/`）和 Node.js（只用于 `npx @tauri-apps/cli` 打包安装程序）。

```powershell
git clone https://github.com/wty92911/skinlab.git
cd skinlab
cargo xtask native            # 产物写入 out/session/，索引 out/current.json
cargo xtask verify            # host 自检 + 控制器驱动自建 WARP 窗口，不连接游戏
cargo run -p skinlab          # 开发运行（需管理员终端，或接受 UAC）
cd app; npx @tauri-apps/cli@2 build   # NSIS 安装包：target/release/bundle/nsis/
```

`cargo xtask native --compiler <g++ 路径>` 可指定编译器；已载入旧 DLL 时用 `--bundle session-dev1` 构建到新目录。游戏安装位置不再需要配置：SkinLab 从运行中的进程读取路径，以 `data/builds.json` 中的 SHA256 确认版本。

`cargo run -p skinlab -- --fixture` 用真实原生链路驱动自建 WARP 窗口；`-- --demo`（非 Windows 默认）使用内存后端，便于在任何系统上调整界面。只改样式时也可以直接用静态服务器打开 `app/ui/`，页面会自动载入 `dev-mock.js`（`?phase=ready|no_game|waiting_hero|unsupported|blocked`）。

## 检查

```bash
cargo fmt --all --check
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace
```

CI（`.github/workflows/ci.yml`）在 Linux 上跑以上检查，在 Windows 上构建原生产物、运行 `cargo xtask verify` 并打包安装程序。

## 目录与适配

目录的作用是把显示名称映射为引擎使用的非连续皮肤编号；它随版本发布，运行时不联网更新。维护者发版前运行 `cargo xtask update-catalog`，从腾讯英雄联盟官网公开数据刷新并同步生成 `native/loader/skin_catalog_generated.h`。

界面中的头像与原画从官网 CDN 按需加载，可在设置中关闭；关闭后显示文字占位。

目录更新和游戏二进制适配是两件事。游戏升级后按 `docs/ADAPTATION.md` 适配，确认后用 `cargo xtask build-id <EXE>` 生成条目写入 `data/builds.json`。未登记的构建会显示“游戏版本尚未适配”，不会加载 DLL。

## 其他

- 日志：`%LOCALAPPDATA%\io.github.wty92911.skinlab\logs\`（按天滚动保留 7 天；`sessions/` 保留最近 30 次会话的 JSONL）。设置中可直接打开。
- DLL 保存在安装目录，不写入 LoL／WeGame 文件夹。停止后模块和帧桥接保留至对局进程退出；不实现运行中热卸载。同一对局主动停止后不支持重新接入。
- 原始软件、第三方 DLL 样本、分析工具、历史研究和个人日志不进入仓库。当前仓库未指定开源许可证。

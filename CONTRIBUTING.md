# 参与贡献

感谢关注 SkinLab。提交 issue 或 PR 前请先阅读 [README](README.md) 中的声明：这是非商业的学习研究项目，贡献内容同样以 [PolyForm Noncommercial 1.0.0](LICENSE) 发布。

## 不接受的贡献

- 规避或对抗反作弊的内容：隐藏模块、伪装签名、绕过或关闭保护、降低被检测概率等。
- 影响其他玩家或对局公平性的功能（只允许本机外观）。
- 原始第三方软件、DLL 样本、游戏文件、美术资源、账号信息、个人日志与截图。
- 任何商业化相关内容（付费功能、广告、推广链接）。

## 开发环境

| 用途 | 需要 |
| --- | --- |
| 界面与控制器（任意系统） | Rust stable |
| 原生组件 | x64 MinGW-w64 g++（Windows 或交叉编译，如 `brew install mingw-w64`） |
| 安装包 | Windows + Node.js（`npx @tauri-apps/cli@2 build`） |

```bash
cargo run -p skinlab                       # 非 Windows 默认演示模式
SKINLAB_DEMO_HERO=Lux cargo run -p skinlab # 指定演示英雄
python3 -m http.server 5178                # 在仓库根目录运行，然后打开 /app/ui/ 只调样式
```

浏览器调样式时可用参数：`?hero=Lux`、`?phase=ready|no_game|waiting_hero|unsupported|blocked`、`?notice`（显示首次使用说明）、`?shot`（隐藏演示切换器）。

Windows 上的完整流程：

```powershell
cargo xtask native
cargo xtask verify
cargo run -p skinlab -- --fixture   # 真实原生链路 + 自建 WARP 窗口
cargo run -p skinlab                # 连接游戏（请在训练模式中测试）
```

## 提交前检查

```bash
cargo fmt --all
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace
```

改动原生代码时还需在 Windows 上运行 `cargo xtask native` 与 `cargo xtask verify`。CI 会在 Linux 和 Windows 上自动执行同样的检查。

## 代码约定

- 新文件开头加 `SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0`。
- 状态只在 `skinlab-session` 的控制器线程中变化；界面只渲染 `snapshot` 事件。
- host 的英文诊断只在 `crates/skinlab-core/src/error.rs` 中分类与翻译。
- 普通皮肤共用一条更新流程，不按英雄复制实现。
- 提交信息用英文祈使句概括改动（如 `Fix restore after target change`），正文说明原因。

## 适配新游戏版本

按 [`docs/ADAPTATION.md`](docs/ADAPTATION.md) 操作。PR 需说明：游戏版本与 SHA256（`cargo xtask build-id` 输出）、哪些规则有变化、训练模式中验证了哪些英雄与操作。未经实际验证不要向 `data/builds.json` 添加条目。

## 发版（维护者）

```bash
cargo xtask release 0.5.0 --push
```

推送 tag 后 CI 自动构建、验证并发布 GitHub Release。

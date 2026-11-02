# 开发约定

- 先读 README、`docs/ADAPTATION.md` 和 `docs/TODO.md`，以当前代码为准；不要恢复历史研究项目、Python 控制器或旧的多按钮界面。
- 产品交互保持“自动识别当前英雄、点击皮肤即应用、Insert 收起”。目录静态随版本发布，不在启动时联网更新；原画按需从官网 CDN 加载且可关闭。
- 状态只在 `skinlab-session` 的控制器线程中变化；界面只渲染 `snapshot` 事件并转发点击，不在前端重复状态机逻辑。
- host 返回的英文诊断只在 `skinlab-core/src/error.rs` 中分类和翻译，不在其他地方匹配错误字符串。
- 普通皮肤共用一条引擎更新流程，避免按英雄复制实现。特殊形态单独研究，不以放开目录选项代替适配。
- 原生构建和端到端检查在 Windows x64 上进行：`cargo xtask native`、`cargo xtask verify`。Rust 部分在任意系统上可 `cargo test`；非 Windows 默认使用 demo 后端。
- 改动后运行 `cargo fmt`、`cargo clippy --workspace --all-targets -- -D warnings`、`cargo test --workspace`；不为简单改动扩充测试矩阵，不新增测试台账、验收日志文档或历史更新流水。
- 游戏测试与用户安排训练模式后进行。不操作用户正在进行的匹配／排位，不擅自关闭游戏或改变系统保护配置。
- 版本更新不能只改 `data/builds.json` 的哈希；按适配说明确认关键特征、字段、编码、调用约定和帧线程。区分 RVA、对象成员偏移和机器指令解析位置。
- 协议或 ABI 改变时同步重建 host 与 DLL，并同步 `skinlab-core/src/protocol.rs`。不要覆盖仍被游戏占用的产物，使用新的 `session-*` 目录。
- 不实现反作弊规避：不隐藏模块、不伪装签名、不绕过或关闭保护。当前不做运行中热卸载，不以删除 DLL 或隐藏界面声称免封。
- 不提交 `out/`、`target/`、`evidence/`、`.tools/`、`.archive/`、`local-data/`、原始软件、账号信息、截图或运行日志。
- 发版用 `cargo xtask release <版本>`，由 tag 触发 CI 发布；不要手动上传安装包，也不要改动已发布 tag。适配新游戏版本或更新目录后应发新版本。
- 文档保持当前状态与短 TODO；修完问题后更新相关条目即可。未明确授权时不要替项目添加开源许可证。

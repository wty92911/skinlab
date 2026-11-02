## 改动

<!-- 做了什么、为什么 -->

## 检查

- [ ] `cargo fmt --all`、`cargo clippy --workspace --all-targets -- -D warnings`、`cargo test --workspace`
- [ ] 改动原生代码时已在 Windows 上运行 `cargo xtask native` 与 `cargo xtask verify`
- [ ] 适配新游戏版本时已附 `cargo xtask build-id` 输出，并说明训练模式中的验证情况
- [ ] 不包含反作弊规避、第三方二进制、游戏资源或个人信息

提交即表示同意以 PolyForm Noncommercial 1.0.0 发布本次贡献。

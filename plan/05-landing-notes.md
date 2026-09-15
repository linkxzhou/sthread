# plan/05 落地记录

分支：`impl-plan-05-docs`

## 已做

1. **重写 [`README.md`](../README.md)**：环境要求、真实 `make` 目标、核心概念、架构图、API 参考、旧名→新名、性能公式、已知限制、风格与许可证说明。
2. **重写 [`AGENTS.md`](../AGENTS.md)**：反映 01～04 完成后的仓库状态与九条约定。
3. **关键类三件套注释**（用途 / 线程模型 / 所有权）：`StThreadSchedule`、`StEventSchedule`、`StThread`、`StThreadItem`、`StEventItem`、`StConnection`、`StClientConnection`、`StConnectionManager`、`StSysSchedule`、`StServer`、`Singleton`、`UtilPtrPool`、`StHeapTimer`、`StIOState`（epoll/kqueue）、`StBuffer`/`StBufferPool`、`StClosure`。
4. **来源说明**：`app/st_memcacheclient/README.md`、`app/st_wrk/THIRD_PARTY.md`、`stlib/tests/ucontext/README.sthread.md`；`COPYRIGHT` / `thirdparty/readme.md` 沿用 01。
5. **D1 措辞**：README/AGENTS 统一为 LLVM 基线 + `m_x_`；plan 硬约束行同步。
6. **D6**：不代选本仓库 `LICENSE`；README 写明待维护者决定。

## 未做 / 诚实缺口

| 项 | 说明 |
| --- | --- |
| README 示例在 arm64 上端到端跑通 | 受 ucontext stub 限制；文档已标 known-failure |
| 万级协程 / wrk QPS 实测数字 | 无 Linux 实测，文档用公式 +「未完成」 |
| 根 `LICENSE` | 维护者决策（D6） |
| 英文版文档 | 未做（保持中文） |

## 验收对照（摘要）

- [x] README 不再以失效的 `IMtActionServer` / `mt_set_timeout` / `make event` 为主路径
- [x] 构建说明与 `make lib|apps|tests` 一致
- [x] 特性编号与 `kqueue` 拼写
- [x] AGENTS 覆盖核心约定
- [x] 关键类注释
- [x] COPYRIGHT 存在；LICENSE 明确「未发布」
- [x] 注释-only 改动后 `make lib` 仍通过

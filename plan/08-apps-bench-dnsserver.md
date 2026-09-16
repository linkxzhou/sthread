# 08 · apps 优化：HTTP/DNS 压测闭环 + st_dnsserver + 结构整理

> **状态：📝 计划中（未实现）** · 2026-09-16 起草。
>
> **决策：D1–D6 已接受**（2026-09-16）：D1=b 内嵌后抽、D2=b 空 ANSWER、D3=c 冻结 baseline、D4=a 短连接为主、D5=a 5353、D6=b wrk 小改 SUMMARY/JSON。
>
> 本文档**只描述计划，不包含任何代码变更**。与 [`06`](06-stlib-refactor-cleanup.md) / [`07`](07-src-refactor-optimize.md) 衔接：06/07 已把 `stlib`/`src` 打磨到可独立使用；本篇聚焦 **`app/` 示例与压测闭环**，用可复现的数字回答「整体性能如何」，并补齐 DNS 服务端样例。
>
> 硬约束沿用总索引：C++98、零第三方运行时依赖（`st_wrk` 已 vendored 的 `http_parser` 保持现状）、makefile 构建、行为兼容、注释中文。

---

## 1. 目标与非目标

### 1.1 目标

1. **HTTP 压测闭环**：`st_httpserver` 常驻可跑；用本仓库 `st_wrk` 压测并**自动产出性能报告**（可提交的 markdown + 原始摘要），作为 libmthread 端到端吞吐/延迟基线。
2. **DNS 压测闭环**：新增 `app/st_dnsserver`（基于 `StServer` + UDP）；改造/扩展 `st_dns` 指向本地权威服务，输出查询 QPS / 延迟报告（不再依赖公网合成域名超时）。
3. **apps 结构与代码质量**：统一入口风格、makefile、`make apps` 装配、README；清掉明显历史债（`mt_*` 别名混用、不可达逻辑、二进制/`.dSYM` 污染工作树）；抽**可复用压测脚本**，避免人手抄命令。

### 1.2 非目标（明确不做）

| # | 不做 | 理由 |
| --- | --- | --- |
| N1 | 不改 `src/`/`stlib/` 调度模型、栈布局、枚举值 | 属 06/07 范围；本篇只消费框架 |
| N2 | 不做完整 HTTP/1.1 或 DNS 权威实现（RFC 全覆盖） | 样例够用即可；HTTP 仍短连接固定响应；DNS 仅 TYPE_A + 固定区 |
| N3 | 不引入 gtest / wrk 上游重写 / 新第三方 | 继续用现有 `st_wrk` + shell/python 报告 |
| N4 | 不发明根 `LICENSE` | 仍由维护者决定 |
| N5 | 不承诺 Linux CI 数字与本机 macOS 一致 | 报告标注平台；Linux 数字作为后续加分项 |
| N6 | 不把 `app/st_c.*` 物理搬迁到 `src/` | 07 已降级反向依赖；搬迁另立项 |

---

## 2. 现状盘点（写计划时实测口径）

### 2.1 目录与职责

| 路径 | 现状 | 问题 |
| --- | --- | --- |
| `app/st_httpserver/` | 最小 `StServer` HTTP 样例；`:8765`；`Connection: close` | 无一键压测脚本；无固化报告；短连接限制 QPS 上限 |
| `app/st_wrk/` | 自研类 wrk（多进程 + `http_parser`）；已能量出 req/s | 报告只打 stdout；`-c`/`-n` 语义易混淆（connections ≥ numbers）；工作树常留 `wrk` 二进制 / `.dSYM` |
| `app/st_dns/` | UDP 客户端查公网/指定 DNS；150 协程并发 | 默认 `www.2000+.com` 合成域名 → 超时；`Frame::Loop(true)` 不退出；无本地服务端 → **无法稳定测 QPS** |
| `app/st_memcacheclient/` | 客户端样例 | 本篇仅回归编译，不扩功能 |
| `app/st_c.*` `st_sys.*` `st_action.*` `st_frame.h` | **库代码**物理在 app/ | 本篇不搬迁；注意示例勿加深反向依赖 |
| 根 `make apps` | 编 dns / memcache / wrk / httpserver | 需挂上 `st_dnsserver` + 可选 `bench` 目标 |

### 2.2 已有基线数字（readme，macOS arm64 历史冒烟）

| 场景 | 结果 |
| --- | --- |
| `st_wrk -n 3 -c 3 -d 2s` → httpserver | ~832 req/s（连接数/请求数极小，**不能**当性能结论） |
| `st_dns` 150 协程 | 调度切换 OK；公网合成域名超时属预期 |

→ 本篇要用**更大、固定参数**的压测矩阵，把数字写进报告并进仓库（或 `reports/` + `.gitignore` 大文件策略，见 D3）。

### 2.3 与 06/07 的联动

- 07 已修 server `CallBack` `FreePtr`、栈泄漏、`WaitFdReady`、fd 容量 D4 → 压测时内存应更稳；报告中可对比 RSS（可选）。
- keepalive **真复用仍未做**（07-D1）→ HTTP 默认继续短连接；若做 keepalive 压测矩阵，须在报告中标注「非池复用」。

---

## 3. 功能设计

### 3.1 HTTP：`st_httpserver` × `st_wrk` → 性能报告

**工作流（一键）**：

```
make lib && make -C app/st_httpserver && make -C app/st_wrk
scripts/bench_http.sh   # 或 make bench-http
  ├─ 后台起 ./app/st_httpserver/main $PORT
  ├─ 健康检查 curl
  ├─ 跑一组 wrk 矩阵（见下）
  ├─ 解析 stdout → 写入 reports/http-YYYYMMDD-HHMM.md
  └─ 停掉 server（trap）
```

**建议默认矩阵**（可用环境变量覆盖）：

| 配置名 | `-n` (进程/worker) | `-c` (连接) | `-d` | 目的 |
| --- | --- | --- | --- | --- |
| smoke | 2 | 10 | 3s | 脚本冒烟 |
| medium | 4 | 50 | 10s | 日常基线 |
| heavy | 8 | 200 | 30s | 压力上限探底 |

每条配置记录：`req/s`、完成请求数、字节、Latency 分位（若 wrk 开 `--latency`）、错误计数、墙钟、CPU/平台、`git rev-parse --short HEAD`。

**报告模板要点**（markdown）：

1. 环境：OS / arch / commit / 编译选项（`TRACE`/`DEBUG`）
2. Server：短连接、固定 body、端口
3. 矩阵表格 + 原始 wrk 输出附录
4. 解读：对比 smoke→heavy 是否近似线性；错误是否为 0
5. 已知偏差：短连接、单 OS 线程事件循环、无 HTTP keepalive 复用

**可选增强（P1，不挡主路径）**：

- `st_httpserver` 增加 `-k` / env 切换 `Connection: keep-alive` + `eTCP_KEEPLIVE_CONN`（仍受 07-D1 限制，报告必须写明）
- `st_wrk` 修正 help 文案：`connections` 与 `numbers` 关系；增加 `--json` 一行摘要便于脚本解析

### 3.2 DNS：新增 `st_dnsserver` + `st_dns` 性能

**`app/st_dnsserver/`（新建）**

- 基于 `StServer<DnsConn, eUDP_CONN>`（或等价 UDP listen + 协程路径，与现有 `UdpSrvConn` 单测对齐）。
- 能力范围（最小权威）：
  - 解析标准 DNS query（HEADER + QNAME + QTYPE=A / QCLASS=IN）
  - 对配置区应答：例如 `*.bench.sthread.local` / `www.N.bench.local` → 固定 `A`（如 `10.0.0.(N%256)`）或统一 `127.0.0.1`
  - 错误：格式坏包 → 丢弃或 FORMERR；非 A → 空应答 / NOTIMP（二选一，拍板 D2）
- 运行：`./main [bind_ip] [port]` 默认 `0.0.0.0:5353`（避免与系统 53 抢权限；可用 53 若 root）
- README：如何与 `st_dns` 联调

**`app/st_dns/` 改造**

| 项 | 改法 |
| --- | --- |
| 默认上游 | 支持 `-s 127.0.0.1 -p 5353`；默认仍可指向 `8.8.8.8` 但 README 标明压测用本地 |
| 压测模式 | `-n 1000 -c 100`：起 N 个协程循环查 `www.{i}.bench.local`，汇总成功数 / 失败数 / 耗时 / QPS 后**主动退出**（解决 `Loop(true)` 挂死） |
| 报告 | `scripts/bench_dns.sh` 起 dnsserver + 跑 client → `reports/dns-*.md` |
| 命名卫生 | 新代码用 `st_*`；保留 `mt_init_frame` 宏兼容，但 main 改为 `st_init_frame` |

**协议复用**：优先把编解码从 `st_dns/dns.cpp` 抽到双方可 include 的薄头（如 `app/st_dns/dns_proto.h` 或 `app/dns_proto/`），避免 server/client 两份 QNAME 编解码；若抽公共头成本高，允许第一期 server 内复制最小编解码，第二期再抽（决策 D1）。

### 3.3 整体结构 / 代码优化（apps）

按优先级：

**P0（与压测闭环绑在一起做）**

1. 根 `makefile`：`apps` 增加 `st_dnsserver`；新增 `bench-http` / `bench-dns` / `bench` phony。
2. `scripts/bench_http.sh`、`scripts/bench_dns.sh`（POSIX shell 或 python3，C++98 约束不作用于脚本）。
3. `reports/.gitkeep` + README：说明如何生成；默认 **提交最新一份基线报告**，或只提交模板 + CI artifact（见 D3）。
4. `.gitignore`：确保 `app/*/{main,wrk}`、`*.dSYM`、`*.o` 不被误加（复核 R9）。

**P1（结构卫生）**

5. `st_httpserver`：可选 keepalive 开关；请求路径日志级别收敛；端口/`SO_REUSEADDR` 已有则保持。
6. `st_dns`：CLI 参数、退出路径、去掉无用 `std::stringstream` 热点路径（可用栈上 `snprintf`）。
7. `st_wrk`：help/参数校验文案；可选 JSON 摘要行；`THIRD_PARTY.md` 保持。
8. 统一各 app README 结构：编译 / 运行 / 压测 / 限制。

**P2（加分，可另提交）**

9. `st_memcacheclient` 只做编译回归 + README 指向压测非目标。
10. 共享 `app/common/` 仅当出现真实重复（DNS proto / bench 计时）；避免过度框架化。

---

## 4. 分阶段执行

### Phase 0 · 基线锚定（无功能代码）

1. `make lib && make apps` 全绿。
2. 手工跑一轮现有 HTTP：`httpserver` + `wrk -n 4 -c 50 -d 5s`，把 stdout 贴进 plan §9 作「改造前」对照。
3. 记录 `st_dns` 默认行为（超时）作为反例，证明需要 `st_dnsserver`。
4. 确认 `.gitignore` 对 app 产物覆盖。

**出口**：基线数字与命令写入 §9；三件套仍绿。

### Phase 1 · HTTP 压测闭环

1. 实现 `scripts/bench_http.sh` + 报告模板。
2. 根 makefile 挂 `bench-http`。
3. （可选 P1）`st_wrk --json` 或稳定可 grep 的 `SUMMARY` 行。
4. 更新 `app/st_httpserver/README.md`、根 readme「验证状态」表。

**出口**：`make bench-http` 在干净树可复现；生成 `reports/http-*.md`；错误计数为 0（允许 heavy 下定义「可接受错误率」若有，须写进报告）。

### Phase 2 · `st_dnsserver` + DNS 压测闭环

1. 新建 `app/st_dnsserver/`（main + makefile + README）。
2. 改造 `st_dns` CLI / 压测退出。
3. `scripts/bench_dns.sh` + `make bench-dns`。
4. `make apps` 纳入 dnsserver。

**出口**：本地 DNS 往返成功；报告含 QPS；不再依赖公网合成域名。

### Phase 3 · apps 结构与代码优化

1. P0/P1 清单收尾（gitignore、README 统一、dns 命名卫生、httpserver 可选 keepalive）。
2. 协议抽取（若 D1 选「二期再抽」则本 Phase 做抽取）。
3. `AGENTS.md` / 根 readme 同步 apps 列表与 bench 入口。

**出口**：`make apps`、`make bench`（http+dns）全绿；文档与真实命令一致。

### Phase 4 · 固化基线与收尾

1. 选定一组「官方基线」参数写入 `reports/baseline-http.md` / `reports/baseline-dns.md`（或单文件分节）。
2. plan §9 回填偏差；`plan/README.md` 将 08 标为完成。
3. （可选）Linux 同参数补跑一节附录。

**出口**：文档闭环；推送策略按用户要求。

---

## 5. 决策点（实现前需拍板）

| # | 问题 | 选项 | 建议 |
| --- | --- | --- | --- |
| D1 | DNS 编解码是否先抽公共头 | a) Phase2 即抽 `dns_proto`；b) server 先内嵌拷贝，Phase3 再抽 | **b**：先打通闭环，降低第一刀风险 |
| D2 | 非 A 查询行为 | a) NOTIMP；b) 空 ANSWER+NOERROR；c) 丢弃 | **b**：客户端当前只查 A，空应答最好测 |
| D3 | 报告是否进 git | a) 每次基线 md 提交；b) 只提交模板，数字本地/CI；c) 提交一份冻结 baseline | **c**：一份冻结 baseline + 脚本随时重生 |
| D4 | HTTP 是否做 keepalive 矩阵 | a) 仅短连接；b) 短连接 + keepalive 对照（标注 D1 限制） | **a** 为主路径；**b** 作 Phase3 可选 |
| D5 | DNS 监听端口默认 | a) 5353；b) 5300；c) 53 | **a** 5353，免 root |
| D6 | `st_wrk` 是否大改 | a) 仅脚本包装；b) 加 JSON/SUMMARY + 文案修复 | **b** 小改，利于稳定解析 |

---

## 6. 风险

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R1 | 短连接压测打不满 CPU，数字偏低被误读 | 中 | 报告标明模型；可选 keepalive 对照 |
| R2 | `st_wrk` 多进程 + 本机单核调度噪声 | 中 | 固定矩阵；多次取中位数；写明进程模型 |
| R3 | UDP DNS 丢包导致 QPS 抖动 | 中 | loopback；统计超时率；重试策略明确 |
| R4 | 脚本杀进程误杀同名 `main` | 低 | 用 pid 文件 / 绝对路径 / `lsof -i` |
| R5 | 抽 dns_proto 时 endian/打包差异 | 中 | 单测：query 字节对照 + 一轮真实联调 |
| R6 | 报告提交导致 diff 噪音 | 低 | D3 选冻结 baseline |
| R7 | Linux 未跑 | 中 | Phase4 附录；不挡 macOS 完成 |

---

## 7. 验收清单

### A. HTTP

- [ ] `make bench-http` 可复现
- [ ] 报告含 req/s、错误、commit、平台
- [ ] curl 冒烟仍过

### B. DNS

- [ ] `st_dnsserver` 监听并可被 `st_dns` 查到 A 记录
- [ ] `make bench-dns` 产出 QPS 报告；进程正常退出
- [ ] 默认压测不依赖公网

### C. 工程

- [ ] `make apps` 含 dnsserver
- [ ] 无新增第三方；`otool -L` 样例仍只链系统库 + libmthread
- [ ] README / AGENTS 命令与真实一致
- [ ] 工作树无强制提交的 app 二进制

---

## 8. 建议目录落点（实现时）

```
app/st_httpserver/     # 已有，小改 + README
app/st_wrk/            # 小改 SUMMARY/JSON（D6）
app/st_dns/            # CLI + 压测退出
app/st_dnsserver/      # 新建
scripts/bench_http.sh
scripts/bench_dns.sh
reports/
  README.md
  baseline-http.md     # D3-c
  baseline-dns.md
plan/08-apps-bench-dnsserver.md  # 本文
```

---

## 9. 落地记录（实现时填写）

> 每完成一个 Phase，在此追加：日期、提交号、三件套/`make apps`/`make bench-*` 结果、报告路径、偏差。

（待填写）

---

## 10. 与用户需求的对照

| 用户要求 | 本计划落点 |
| --- | --- |
| 1. httpserver 用 st_wrk 压测并输出性能报告 | §3.1 + Phase 1 |
| 2. 增加 st_dnsserver，用 st_dns 测性能 | §3.2 + Phase 2 |
| 3. 整体优化代码/功能/结构 | §3.3 + Phase 3 |
| 先写详细 plan 到 plan/ | 本文；`plan/README.md` 索引更新 |

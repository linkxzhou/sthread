# st_wrk

自研类 wrk HTTP 压测客户端（多进程 + vendored `http_parser`）。协议实现见 [`THIRD_PARTY.md`](THIRD_PARTY.md)。

## 编译

```bash
make lib
make -C app/st_wrk
```

## 运行

```bash
./wrk -n 2 -c 10 -d 3s --latency --json http://127.0.0.1:8765/
./wrk -h
```

| 参数 | 含义 |
| --- | --- |
| `-n` / `--numbers` | **worker 进程数**（fork） |
| `-c` / `--connections` | 总连接数，必须 **≥ numbers**；每进程 `connections/numbers` 路 |
| `-d` / `--duration` | 时长标签（SI 时间）；当前每 worker 发完一批即退出 |
| `--json` | 额外打印一行 `JSON {…}` |
| `--latency` | 延迟分位 |

结束时**总是**打印可 grep 的：

```
SUMMARY complete=… requests=… req_per_s=… bytes=… errors=… runtime_us=…
```

## 压测闭环

与 `st_httpserver` 联调：仓库根 `make bench-http`。短连接为主路径（plan/08 D4=a）。

## 限制

- 不是 upstream wrk；不实现 HTTP keepalive 连接池
- `-c` 必须 ≥ `-n`，否则参数校验失败

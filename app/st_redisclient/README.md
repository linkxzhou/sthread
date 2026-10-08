# st_redisclient

样例里的 RESP 客户端。不链接 hiredis，协议不进 `libmthread`。传输是 `StExecClientConnection` 加 `st_send` / `st_recv`，和 `st_memcacheclient` 同一类。

```bash
make lib && make -C app/st_redisclient
./app/st_redisclient -h 127.0.0.1 -p 6379 ping
./app/st_redisclient set k v
./app/st_redisclient get k
./app/st_redisclient incr n
./app/st_redisclient -c 4 -n 20 ping
```

本地冒烟（不进 CI，也不在 CI 里安装 redis）：`make smoke-redis`。有 `redis-server` 就用它，否则用 `python3 scripts/redis_stub.py`（只实现 PING / SET / GET / INCR）。

本机已经有 redis 时可以手工压一下，不进 `make bench`：

```bash
./app/st_redisclient -c 50 -n 5000 -q ping
```

## 行为

命令只有 `ping`、`set`、`get`、`incr`。`-c 1 -n 1` 且没有 `-q` 时，先把回复打到 stdout（批量字符串打正文，整数打十进制，空批量打 `(nil)`），再打：

```
SUMMARY ok=.. fail=.. qps=.. elapsed_ms=.. p50_ms=.. p99_ms=..
```

`+` / `:` / `$` / 空批量计 ok。`-` 是 Redis 的错误回复，计 fail。传输失败也计 fail。分位是 `int` 数组加 `qsort`。

退出码：0 全部成功；1 有失败；2 参数或未知命令。

## 限制

- RESP 子集，不是 RESP3。数组解码能接住 `*1` 这种回复，元素个数上限 32，嵌套深度 4。
- 每个请求一条短连接。同一协程里复用连接不做：`FreePtr` 仍然关掉 fd。
- 只接受 IPv4 字面量。
- 单 OS 线程。`-c` 很大时栈大约 266KB × 协程数，还要 `ulimit -n`。

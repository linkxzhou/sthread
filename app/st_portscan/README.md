# st_portscan

几十到几千个协程同时 `st_connect`，把结果分成 open / refused / timeout / other。

```bash
make lib && make -C app/st_portscan
./app/st_portscan -c 32 -t 300 -p 7707,9 127.0.0.1
```

```
OPEN 7707
SUMMARY open=1 refused=1 timeout=0 other=0 elapsed_ms=..
```

退出码：0 扫完（open 可以为 0）；2 参数错误。host 只接受 IPv4 字面量。

`-c` 是同时在飞的协程数，不是「每个端口一个协程」。默认 `-c 64`，端口默认 `80,443`（不把 `1-1024` 当默认值）。本地冒烟用 `-c 32`，不进 CI。

## 分类

`StClientConnection::Create` 对超时和其他失败都返回 `-2`。扫描器读的是 **保存过的 errno**：

| errno | 计数 |
| --- | --- |
| 成功（返回值 `>=0`） | open，立刻 `FreePtr` |
| `ETIME` | timeout |
| `ECONNREFUSED` | refused |
| 其他（`ENETUNREACH`、`EHOSTUNREACH`、`ECONNRESET`） | other |

loopback 上没人听是 `ECONNREFUSED`，不是超时。打到 `192.0.2.1`（TEST-NET-1）经常是 `ENETUNREACH`，计进 other。对一个丢掉 SYN 的地址加 `-t 200`，应看到 timeout。这条不进 CI。

## 限制

- 单协程栈大约 266240 字节。`-c 2000` 光栈就要大约 500MB，还要 `ulimit -n` 大于 `-c`。不要在冒烟里用这个数。
- 单 OS 线程，1:N。
- 不在失败路径上再 `connect` 一次。

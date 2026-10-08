# st_echo

最短的 TCP 一行回显。readme 的入口示例，也是比 HTTP 更短的吞吐基线：没有解析，只有 `tcp_sendrecv`。

```bash
make lib && make -C app/st_echo
./app/st_echo/st_echoserver                 # 0.0.0.0:7707
./app/st_echo/st_echoclient 127.0.0.1 7707
./app/st_echo/st_echoclient -c 8 -n 40 -s ping 127.0.0.1 7707
```

本地冒烟（不进 CI）：`make smoke-echo`，脚本 `scripts/smoke_echo.sh`，端口默认 `17707`。

## 行为

服务端是 `StServer<EchoConn, eTCP_KEEPLIVE_CONN>`。`eTCP_KEEPLIVE_CONN` 只为了 `CallBack` 的 `while (Keeplive())` 能把拆开的一行收齐。**不是**连接池：对端关掉之后 `FreePtr` 仍然 `HashRemove` 并关闭 fd。

客户端每个请求一次 `tcp_sendrecv(..., keeplive=false)`。见到 `\n` 的回调返回下标 + 1。接收超时是 `app/st_c.h` 里的 **-3**（`errno=ETIME`），不要和 `st_recv` 的 `-1` 混用。

```
SUMMARY ok=.. fail=.. qps=.. elapsed_ms=..
```

退出码：0 全部成功；1 有失败或仍有 pending；2 参数错误。`-c 1 -n 1` 时先把回显打到 stdout，再打 SUMMARY。

## 限制

- 一次只保证一行。`DoInput` 返回 `ST_CONN_RESET_RECVBUF` 会把 `\n` 后面的粘包清掉。
- 同一连接上第二次发送前要 `SetHaveSendLen(0)`，样例已经在 `DoProcess` 里做了。
- 没有完整行时 `DoOutput` 把长度设为 0。`st_send` 在 `nbyte==0` 时直接返回 0。
- 地址只接受 IPv4 字面量（`inet_pton`）。`getaddrinfo` 不在 hook 表里，会堵住 OS 线程。
- 单 OS 线程。`-c` 很大时栈大约 266KB × 协程数，还要 `ulimit -n`。

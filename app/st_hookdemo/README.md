# st_hookdemo

业务代码按阻塞 POSIX 来写，放进协程之后，一次慢 `read` 不会堵住其他协程。

允许出现在 `blocking_client.c` 里的调用：`socket`、`connect`、`setsockopt`、`write`、`read`、`close`。地址用 `inet_pton`。超时用 `setsockopt(SO_RCVTIMEO / SO_SNDTIMEO)`。不要 `sleep`、`getaddrinfo`、`poll`、`select`、`readv`。

源码文本里没有 `st_` / `St`。makefile 用 `-include st_posix_alias.h` 把这些 POSIX 名字换成 `sys_*`。对照二进制 `blocking_client` 不加 `-include`，也不链 `libmthread`。

不在 `libmthread.so` 里定义全局 `read`，也不使用 `LD_PRELOAD`。那样会劫持进程里所有读，包括 hook 自己的 `dlsym` 回退。

```bash
make lib && make -C app/st_hookdemo
./app/st_echo/st_echoserver 127.0.0.1 7707
./app/st_hookdemo/blocking_client 127.0.0.1 7707 hook
./app/st_hookdemo/st_hookdemo -c 4 -n 4 127.0.0.1 7707
```

```
SUMMARY ok=.. fail=.. elapsed_ms=..
```

退出码：0 全部成功；1 有失败；2 参数错误或 host 不是 IPv4 字面量。

本地冒烟（不进 CI）：`make smoke-hook`。

## 限制

- `sys_new_fd` 的默认读写超时是 512ms。业务文件必须自己 `setsockopt`，样例已经做了。
- `sys_socket` 把内核 fd 设成非阻塞，但不再打上 `ST_FD_FLG_UNBLOCK`。用户自己再 `fcntl(O_NONBLOCK)` 时，hook 让开，`read` 立刻 `EAGAIN`。
- `sys_accept` 不改。服务端样例走 `st_accept`。
- 单 OS 线程。

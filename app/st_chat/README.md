# st_chat

一行消息广播到同一房间里的其他连接。叫醒用的是 `st_notify` / `st_wait`（`src/st_sys.h`），不占 fd，不建 pipe，也不在别人的 socket 上调用 `st_send`。

```bash
make lib && make -C app/st_chat
./app/st_chat/st_chatserver                 # 0.0.0.0:7700
./app/st_chat/st_chatclient -n ada 127.0.0.1 7700 hello
```

本地冒烟（不进 CI）：`make smoke-chat`，端口 `17700`。B 要能一直读到截止时间，不能在 `* joined bob` 那一行就退出，否则会错过 `ada: hello`。

## 行为

不用 `StServer::Loop`。`CallBack` 只会 `RecvData`，空闲连接收不到别人的消息。监听只用 `CreateSocket` + `Listen`，然后自己的循环 `st_accept`。每个连接有自己的 `StEventItem`（登记时不挂兴趣，避免没人认领时 daemon 空转），协程记下 `StThread *`，别人用 `st_notify` 叫醒。

协议是一行一个 `\n`，单行不超过 512 字节。第一条是 nick，服务端回 `* joined <nick>`，并向其他人发 `* <nick> joined`。之后每行变成 `<nick>: <text>` 发给其他人，不回显给发送者。`quit` 或对端关闭时发 `* <nick> left`。

广播只做两件事，并且在 Yield 之前做完：把一行拷进对方邮箱，然后 `st_notify`。对方若正停在 `st_wait`，会带着 `ST_WAIT_NOTIFY` 醒来；若正停在自己的 `st_send` 里，粘滞位让它下一次 `st_wait` 立刻返回。读侧先排空邮箱，再 `st_wait`。`st_wait` 返回 `ST_WAIT_FD` 之后用一次非阻塞 `recv`，不再套 `st_recv`。

邮箱满（8 行）时丢掉这一行，给发送者自己的 socket 回 `* dropped`。满员（64）时直接关掉新 fd，不建协程。

客户端把读到的每一行打到 stdout。默认 nick 是 pid，超时 3000 ms。退出码：0 至少读到一行；1 超时或连接失败；2 参数错误。

## 限制

- 单房间，最多 64 人。不做多频道，也不读 stdin（fd 0 不在 hook 表里，`read(0)` 会堵住整个 OS 线程）。
- 空闲超时只是回到循环，不断开。
- 只接受 IPv4 字面量。
- 单 OS 线程。

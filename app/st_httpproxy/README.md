# st_httpproxy

一个进程里前面是 `StServer`，后面用 `st_http_exchange` 把请求转到 IPv4 后端。连接槽位、上游超时、探活都在这个样例里。

```bash
make lib && make -C app/st_httpproxy
./app/st_httpproxy -l 0.0.0.0:18080 -b 127.0.0.1:8765 -b 127.0.0.1:8766
curl -x http://127.0.0.1:18080/ http://127.0.0.1:8765/
```

本地冒烟（不进 CI）：`make smoke-proxy`。脚本起两个 `st_httpserver`，经反代拿到 `hello from sthread`，停掉一个仍是 200，两个都停则是 502。

编译依赖已经在仓库里的 `app/st_httpclient/http_client.cc` 和 `app/st_wrk/http_parser.c`。这两个文件不编进 `libmthread`，`http_parser.c` 不改。

## 行为

监听是 `StServer<ProxyConn, eTCP_CONN>`。一请求一连接，转发完 `FreePtr` 关掉 fd。**不是**库里的连接池：每个后端最多 `-s` 个 `StHttpConn` 槽位（默认 2，最大 8）。槽位满了就 `st_sleep(1)` 轮询，直到 `-t` 耗尽再回 502。

探活是另一个协程，用自己的短连接 `GET /`（`keepalive=0`），不占业务槽位。`-H 0` 表示不探活，后端一直当作活着。交换失败会把该后端标成不活，等下一次探活再拉起来。

`st_httpserver` 回 `Connection: close`，所以槽位上的 fd 每次交换后仍会被关掉。槽位限制的是同时在飞的上游个数。

## 限制

- 请求头必须落在 `ST_RECV_BUFFSIZE`（8192）里。头不完整，或 `Content-Length` 的正文没有在第一次 `RecvData` 里到齐，回 400。冒烟用 GET。
- 头超过缓冲时不能从 `DoInput` 返回 `-1`：`CallBack` 在 `RecvData` 失败后不会调用 `DoOutput`。样例改成置标志并返回正长度，再由 `DoProcess` 写 400。
- 上游正文大于 `ST_SEND_BUFFSIZE` 减去约 128 字节的响应头余量时，丢掉正文回 502。8192 这个常量不动。
- `CONNECT` 和 `https://` 绝对 URL 回 400。`http://` 绝对 URL 只转发 path。
- 只接受 IPv4 字面量。后端最多 8 个。
- 槽位不是 `StConnectionManager` 的真复用，`FreePtr` 仍然关闭 fd。

# st_httpclient

同步写法的 HTTP/1.1 命令行样例。链接 `libmthread.a`，用 `StExecClientConnection` + `st_send` / `st_recv` 收发，响应交给 vendored `http_parser`（Content-Length、chunked、读到 EOF 都认）。

不是通用 HTTP 客户端：没有 HTTPS/TLS、HTTP/2、重定向、cookie。`https://` 直接退出码 2。

## 编译

```bash
make -C app/st_httpclient
# 或
make apps
```

## 运行

```bash
./app/st_httpclient/st_httpclient http://127.0.0.1:8765/
./app/st_httpclient/st_httpclient -q -c 10 -n 100 http://127.0.0.1:8765/
./app/st_httpclient/st_httpclient -X POST -d 'a=1' http://127.0.0.1:8765/
```

先在另一个终端起 `./app/st_httpserver/main 8765`。

| 选项 | 含义 |
| --- | --- |
| `-X GET\|POST` | 方法。带 `-d` / `-D` 且没写 `-X` 时默认 POST，否则 GET |
| `-d DATA` / `-D FILE` | 请求体。二者不能同时用 |
| `-H 'K: V'` | 额外头，可重复，同名时覆盖默认头 |
| `-c` | 并发协程，默认 1 |
| `-n` | 总请求数，默认等于 `-c` |
| `-t` | 单请求超时毫秒，默认 3000。连接用 `SetTimeout`，读写用剩余时间 |
| `-k` | 同一协程里复用 TCP 连接（应用层 keep-alive）。服务端回 `Connection: close` 就重连。不走库的 keepalive 池 |
| `-o FILE` | 把最后一次响应体写入文件 |
| `-q` | 不打印响应体 |
| `-v` | 请求和响应头打到 stderr |
| `--json` | 用一行 JSON 代替 `SUMMARY` 文本 |

`c=1` 且 `n=1` 且没有 `-q` 时，先把响应体打到 stdout，然后总是一行：

```
SUMMARY ok=.. fail=.. status_2xx=.. status_other=.. bytes=.. qps=.. elapsed_ms=.. p50_ms=.. p99_ms=..
```

`ok` 是 2xx 的个数。非 2xx 计入 `fail` 和 `status_other`。连接或解析失败只计入 `fail`。

## 退出码

| 码 | 含义 |
| --- | --- |
| 0 | 全部是 2xx |
| 1 | 有失败、非 2xx，或结束时 fd 明显泄漏 |
| 2 | 参数或 URL 不合法，包括 `https` |
| 3 | DNS 失败（IPv4 字面量不走 DNS） |

DNS 在进协程之前用 `inet_pton` / `getaddrinfo(AF_INET)` 做一次。本期不解析 IPv6。

## 冒烟

```bash
make smoke-httpclient
```

脚本会拉起 `app/st_httpserver`，再跑 GET、并发、POST、拒绝连接和 `https`。

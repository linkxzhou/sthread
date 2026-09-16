# st_httpserver

基于 `StServer` 的最小 HTTP/1.1 样例：每接受一个连接起一个协程，读完请求头后回一段固定正文。

## 编译

```bash
# 仓库根目录
make lib
make -C app/st_httpserver
# 或
make apps
```

## 运行

```bash
./main           # 默认监听 8765
./main 8080      # 自定义端口
```

## 验证

```bash
curl -v http://127.0.0.1:8765/
# 期望：HTTP/1.1 200，正文 hello from sthread

# 与 app/st_wrk 联调（另开终端先起本服务）
../st_wrk/wrk -n 2 -c 10 -d 3s --latency --json http://127.0.0.1:8765/
```

一键压测（仓库根目录，脚本用 PID 起停本服务）：

```bash
make bench-http                          # 默认 BENCH_PROFILE=smoke
BENCH_PROFILE=medium make bench-http
```

报告写入 `reports/http-*.md`；冻结基线见 [`reports/baseline-http.md`](../../reports/baseline-http.md)。

## 说明

- 短连接（`eTCP_CONN`），响应带 `Connection: close`（plan/08 D4=a，无 keepalive 矩阵）
- 请求解析只认 `\r\n\r\n` 结束的 header，不做完整 HTTP 语义解析
- 无第三方依赖；不使用 `st_wrk` 里的 `http_parser`

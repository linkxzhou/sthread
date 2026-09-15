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
../st_wrk/wrk -n 1000 -c 50 -d 5s http://127.0.0.1:8765/
```

## 说明

- 短连接（`eTCP_CONN`），响应带 `Connection: close`
- 请求解析只认 `\r\n\r\n` 结束的 header，不做完整 HTTP 语义解析
- 无第三方依赖；不使用 `st_wrk` 里的 `http_parser`

# Vendored：http_parser

`http_parser.c` / `http_parser.h` 来自 [nodejs/http-parser](https://github.com/nodejs/http-parser)（MIT）。

- 不要局部改协议实现；需升级时整文件替换并核对 `COPYRIGHT`
- 本目录其它文件（`wrk.cpp`、`utils.h` 等）是 sthread 示例，不是 upstream

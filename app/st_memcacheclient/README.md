# st_memcacheclient

memcache 协议客户端示例。

- `memcache.cpp` / `memcache.h`：协议解析
- `mt_array.*`：示例内部数组工具（保留 `mt_` 前缀，属 app 私有）
- `memcache.pcap`：抓包样本，用于对照协议解析；**不是**可执行产物，请保留
- 依赖本机 memcached，以及可用的协程切换（非 arm64 stub）才能端到端跑通

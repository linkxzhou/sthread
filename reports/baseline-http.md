# HTTP baseline (frozen)

> Frozen from a real `BENCH_PROFILE=smoke make bench-http` on the agent VM.
> **Do not edit numbers by hand.** Re-run `make bench-http` to produce a timestamped `http-*.md` (gitignored).

## Environment

| 项 | 值 |
| --- | --- |
| Date | 2026-09-16 |
| OS | Linux 6.12.94+ (`cursor`) |
| Arch | x86_64 (nproc=4) |
| Compiler | g++ 13.3.0 (`Ubuntu 13.3.0-6ubuntu2~24.04.1`) |
| Commit (binaries / run) | `2ed314b` |
| Build | `TRACE=0 DEBUG=1` (`make bench-http` forces TRACE=0) |
| Server | `app/st_httpserver/main` `:8765`, `Connection: close`, short-conn |
| Client | `app/st_wrk/wrk` (`-n` = worker **processes**, `-c` = total connections) |

`ldd` of `st_httpserver` / `st_wrk`: system libs only (`libstdc++`, `libgcc_s`, `libc`, `libm`). No third-party runtime.

## Smoke matrix (official freeze)

`st_wrk -n 2 -c 10 -d 3s --latency --json http://127.0.0.1:8765/`

```
SUMMARY complete=10 requests=10 req_per_s=2590.67 bytes=1500 errors=0 runtime_us=3860
JSON {"complete":10,"requests":10,"req_per_s":2590.67,"bytes":1500,"errors":0,"runtime_us":3860}
```

- 10/10 complete, **errors=0**
- Wall time ~3.86 ms (not 3s): `-d` is a **duration label**; each worker issues one batch then exits
- Latency p50 ~514 µs, p99 ~0.96 ms (from the same run)
- Source report: `reports/http-20260916-0416.md` (local, gitignored)

curl health-check against the same server: 5 sequential `GET /` all returned `hello from sthread`.

## How to read this

- Main path is **HTTP short connections** (plan/08 D4=a). Not comparable to wrk-upstream keepalive / pipelining.
- Single OS-thread event loop; `st_wrk` forks worker processes.
- req/s is `complete / runtime_s` for that one batch, not a sustained 3s load.
- Server still logs `del event failed` on close (epoll MOD of a closed fd); it did not fail requests in this run.

## Regenerating

```bash
TRACE=0 BENCH_PROFILE=smoke make bench-http
# optional: BENCH_PROFILE=medium|heavy|all
```

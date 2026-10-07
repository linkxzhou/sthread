# HTTP concurrency curve (frozen)

> Frozen from a real `make bench-curve` run. **Do not edit numbers by hand.** Re-run `make bench-curve` for a timestamped `reports/curve-*` (gitignored).

## Environment

| Item | Value |
| --- | --- |
| Date (UTC) | 2026-10-07 |
| OS | Linux 6.12.94+ (x86_64) |
| CPU | Intel(R) Xeon(R) Processor (nproc=4) |
| Memory | 16398384 kB |
| nofile | 524288 |
| Compiler | g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 (`x86_64-linux-gnu`) |
| Commit | `e62c158` |
| Build | make apps TRACE=0 (DEBUG=1 ASAN=0 TCMALLOC=0 PROFILER=0) |
| TRACE | 0 |
| HTTP server | `app/st_httpserver/main` |
| HTTP client | `app/st_httpclient/st_httpclient` |
| URL | http://127.0.0.1:8765/ |
| Repeats | 3 |
| Requests | n = max(50000, concurrency * 30) |
| Client timeout | 15000 ms per request; wall timeout 120 s |

`ldd` http server: `linux-vdso.so.1 libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6 /lib64/ld-linux-x86-64.so.2`

`ldd` http client: `linux-vdso.so.1 libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6 /lib64/ld-linux-x86-64.so.2`

## Command

Each repeat starts a fresh `st_httpserver` (stderr discarded), then:

```bash
app/st_httpclient/st_httpclient -q -c <conc> -n <n> -t 15000 http://127.0.0.1:8765/
```

`st_wrk` is not the load generator: `-d` is a duration label and each worker exits after one batch, so it does not hold a sustained rate.

## HTTP results

QPS, p50, and p99 are the median of the repeats. Errors are the sum of `fail` across repeats. A point is **reliable** only when every repeat exits 0, the server stays up, and `fail=0`. Unreliable points are omitted from the charts.

![QPS vs concurrency](../docs/perf/http-qps-vs-concurrency.svg)

![latency vs concurrency](../docs/perf/http-latency-vs-concurrency.svg)

| conc | n | repeats | QPS median | QPS min | QPS max | p50 ms | p99 ms | elapsed ms | errors | reliable |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 50000 | 3 | 21710.81 | 21673.17 | 22026.43 | 0 | 1 | 2303 | 0 | yes |
| 10 | 50000 | 3 | 38461.54 | 35714.29 | 38461.54 | 0 | 1 | 1300 | 0 | yes |
| 50 | 50000 | 3 | 38051.75 | 37965.07 | 38343.56 | 1 | 2 | 1314 | 0 | yes |
| 100 | 50000 | 3 | 37037.04 | 35236.08 | 38343.56 | 3 | 4 | 1350 | 0 | yes |
| 200 | 50000 | 3 | 36576.44 | 36390.10 | 36764.71 | 5 | 6 | 1367 | 0 | yes |
| 500 | 50000 | 3 | 36549.71 | 22925.26 | 36791.76 | 13 | 14 | 1368 | 0 | yes |
| 1000 | 50000 | 3 | 35868.01 | 35486.16 | 36205.65 | 25 | 30 | 1394 | 0 | yes |

Shared-VM spread (still reliable, chart uses the median): c=500 median 36549.71 min 22925.26 max 36791.76.

### Every repeat

| proto | conc | repeat | n | ok | fail | qps | elapsed_ms | p50_ms | p99_ms | exit | server_alive | pending |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| http | 1 | 1 | 50000 | 50000 | 0 | 21673.17 | 2307 | 0 | 1 | 0 | 1 |  |
| http | 1 | 2 | 50000 | 50000 | 0 | 21710.81 | 2303 | 0 | 1 | 0 | 1 |  |
| http | 1 | 3 | 50000 | 50000 | 0 | 22026.43 | 2270 | 0 | 1 | 0 | 1 |  |
| http | 10 | 1 | 50000 | 50000 | 0 | 35714.29 | 1400 | 0 | 1 | 0 | 1 |  |
| http | 10 | 2 | 50000 | 50000 | 0 | 38461.54 | 1300 | 0 | 1 | 0 | 1 |  |
| http | 10 | 3 | 50000 | 50000 | 0 | 38461.54 | 1300 | 0 | 1 | 0 | 1 |  |
| http | 50 | 1 | 50000 | 50000 | 0 | 37965.07 | 1317 | 1 | 2 | 0 | 1 |  |
| http | 50 | 2 | 50000 | 50000 | 0 | 38051.75 | 1314 | 1 | 2 | 0 | 1 |  |
| http | 50 | 3 | 50000 | 50000 | 0 | 38343.56 | 1304 | 1 | 2 | 0 | 1 |  |
| http | 100 | 1 | 50000 | 50000 | 0 | 38343.56 | 1304 | 3 | 3 | 0 | 1 |  |
| http | 100 | 2 | 50000 | 50000 | 0 | 37037.04 | 1350 | 3 | 4 | 0 | 1 |  |
| http | 100 | 3 | 50000 | 50000 | 0 | 35236.08 | 1419 | 3 | 4 | 0 | 1 |  |
| http | 200 | 1 | 50000 | 50000 | 0 | 36576.44 | 1367 | 5 | 8 | 0 | 1 |  |
| http | 200 | 2 | 50000 | 50000 | 0 | 36390.10 | 1374 | 5 | 6 | 0 | 1 |  |
| http | 200 | 3 | 50000 | 50000 | 0 | 36764.71 | 1360 | 5 | 6 | 0 | 1 |  |
| http | 500 | 1 | 50000 | 50000 | 0 | 36791.76 | 1359 | 13 | 14 | 0 | 1 |  |
| http | 500 | 2 | 50000 | 50000 | 0 | 36549.71 | 1368 | 13 | 15 | 0 | 1 |  |
| http | 500 | 3 | 50000 | 50000 | 0 | 22925.26 | 2181 | 13 | 14 | 0 | 1 |  |
| http | 1000 | 1 | 50000 | 50000 | 0 | 35868.01 | 1394 | 23 | 30 | 0 | 1 |  |
| http | 1000 | 2 | 50000 | 50000 | 0 | 36205.65 | 1381 | 25 | 30 | 0 | 1 |  |
| http | 1000 | 3 | 50000 | 50000 | 0 | 35486.16 | 1409 | 26 | 29 | 0 | 1 |  |
| dns | 1 | 1 | 1 | 1 | 0 | 1000.00 | 1 |  |  | 0 | 1 | 0 |
| dns | 1 | 2 | 1 | 1 | 0 | 1000.00 | 1 |  |  | 0 | 1 | 0 |
| dns | 1 | 3 | 1 | 1 | 0 | 1000.00 | 1 |  |  | 0 | 1 | 0 |
| dns | 10 | 1 | 10 | 10 | 0 | 909.09 | 11 |  |  | 0 | 1 | 0 |
| dns | 10 | 2 | 10 | 10 | 0 | 1000.00 | 10 |  |  | 0 | 1 | 0 |
| dns | 10 | 3 | 10 | 10 | 0 | 1000.00 | 10 |  |  | 0 | 1 | 0 |
| dns | 50 | 1 | 50 | 50 | 0 | 5000.00 | 10 |  |  | 0 | 1 | 0 |
| dns | 50 | 2 | 50 | 50 | 0 | 5000.00 | 10 |  |  | 0 | 1 | 0 |
| dns | 50 | 3 | 50 | 50 | 0 | 5000.00 | 10 |  |  | 0 | 1 | 0 |
| dns | 100 | 1 | 100 | 100 | 0 | 9090.91 | 11 |  |  | 0 | 1 | 0 |
| dns | 100 | 2 | 100 | 100 | 0 | 10000.00 | 10 |  |  | 0 | 1 | 0 |
| dns | 100 | 3 | 100 | 100 | 0 | 9090.91 | 11 |  |  | 0 | 1 | 0 |
| dns | 200 | 1 | 200 | 200 | 0 | 16666.67 | 12 |  |  | 0 | 1 | 0 |
| dns | 200 | 2 | 200 | 200 | 0 | 16666.67 | 12 |  |  | 0 | 1 | 0 |
| dns | 200 | 3 | 200 | 200 | 0 | 18181.82 | 11 |  |  | 0 | 1 | 0 |
| dns | 500 | 1 | 500 | 500 | 0 | 35714.29 | 14 |  |  | 0 | 1 | 0 |
| dns | 500 | 2 | 500 | 500 | 0 | 35714.29 | 14 |  |  | 0 | 1 | 0 |
| dns | 500 | 3 | 500 | 500 | 0 | 35714.29 | 14 |  |  | 0 | 1 | 0 |
| dns | 1000 | 1 | 1000 | 1000 | 0 | 58823.53 | 17 |  |  | 0 | 1 | 0 |
| dns | 1000 | 2 | 1000 | 1000 | 0 | 62500.00 | 16 |  |  | 0 | 1 | 0 |
| dns | 1000 | 3 | 1000 | 1000 | 0 | 62500.00 | 16 |  |  | 0 | 1 | 0 |

## DNS

Not plotted. `st_dns` still needs `-n == -c` (one query per coroutine). Each point is a single burst, and `elapsed_ms` is a 1 ms clock, so the QPS figure is not a sustained rate. A check with `-n > -c` fails the extra queries (same-process sequential UDP); this probe does not do that.

| conc | n | repeats | QPS median | elapsed ms median | errors | pending max | reliable |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 1 | 3 | 1000.00 | 1 | 0 | 0 | yes |
| 10 | 10 | 3 | 1000.00 | 10 | 0 | 0 | yes |
| 50 | 50 | 3 | 5000.00 | 10 | 0 | 0 | yes |
| 100 | 100 | 3 | 9090.91 | 11 | 0 | 0 | yes |
| 200 | 200 | 3 | 16666.67 | 12 | 0 | 0 | yes |
| 500 | 500 | 3 | 35714.29 | 14 | 0 | 0 | yes |
| 1000 | 1000 | 3 | 62500.00 | 16 | 0 | 0 | yes |

## Caveats

- Loopback on a shared VM. Numbers are not comparable across machines or to a quiet bare-metal host.
- One OS thread runs the server event loop. The client is a second process, also one event-loop thread.
- HTTP connections are short. `st_httpserver` always sends `Connection: close`. The sweep does not pass `-k`. Against this server, `-k` reconnects; it is not pool reuse (keepalive pool reuse is still not implemented).
- The server process is restarted before every HTTP repeat. Finished coroutines are not reclaimed (`StThread` pool TODO), so a long-lived server accumulates stacks and later points would measure that leak.
- Latency comes from `st_httpclient`'s millisecond clock (`p50_ms` / `p99_ms`). Sub-millisecond requests show up as 0.
- Server stderr is discarded (`/dev/null`). Close still logs `del event failed` inside the process; those lines are not kept, and they are not request failures.
- `listen` backlog is 128. A point with `fail>0` or a dead server stops the HTTP sweep at that concurrency.
- DNS rows above are bursts of one query per coroutine, not the curve.

## Regenerating

```bash
make bench-curve
# smaller smoke of the script only:
# BENCH_CURVE_CONCS='1 10' BENCH_CURVE_REPEATS=1 BENCH_CURVE_MIN_N=200 make bench-curve
```

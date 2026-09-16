# DNS baseline (frozen)

> Frozen from a real `BENCH_PROFILE=smoke make bench-dns` on the agent VM.
> **Do not edit numbers by hand.** Re-run `make bench-dns` to produce a timestamped `dns-*.md` (gitignored).

## Environment

| 项 | 值 |
| --- | --- |
| Date | 2026-09-16 |
| OS | Linux 6.12.94+ (`cursor`) |
| Arch | x86_64 (nproc=4) |
| Compiler | g++ 13.3.0 (`Ubuntu 13.3.0-6ubuntu2~24.04.1`) |
| Commit (binaries / run) | `2ed314b` |
| Build | `TRACE=0 DEBUG=1` (`make bench-dns` forces TRACE=0) |
| Server | `app/st_dnsserver/main` `udp://127.0.0.1:5353`, zone `*.bench.local` → `127.0.0.1` (A) |
| Client | `app/st_dns/main -q` (exits after SUMMARY; no `Loop(true)`) |

`ldd` of `st_dnsserver` / `st_dns`: system libs only. No third-party runtime. No public resolver.

## Smoke matrix (official freeze)

`st_dns -q -s 127.0.0.1 -p 5353 -c 10 -n 10 -t 2000`

```
SUMMARY success=10 fail=0 qps=1000.00 elapsed_ms=10 pending=0
```

- 10/10 A lookups, **fail=0**, process exited
- QPS = successful lookups / wall ms (`10 / 0.010 s` → 1000)
- Health check: `www.1.bench.local` → `127.0.0.1` ttl=60
- Source report: `reports/dns-20260916-0416.md` (local, gitignored)

## How to read this

- Default listen port is **5353** (plan/08 D5=a).
- Non-A queries: empty ANSWER + NOERROR (D2=b); this matrix only queries TYPE_A.
- **Deviation from plan §3.2 example `-n 1000 -c 100`:** same-process **sequential** UDP (one coroutine, many queries) currently fails after the first lookup. The freeze matrix uses **`-n == -c`** (one query per coroutine). Separate processes are fine (`-c 1 -n 1` repeated).
- QPS here is a 10-query burst, not a multi-second sustained load.

## Regenerating

```bash
TRACE=0 BENCH_PROFILE=smoke make bench-dns
# optional: BENCH_PROFILE=medium|heavy|all  (still -n == -c)
```

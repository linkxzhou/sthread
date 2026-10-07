#!/bin/sh
# Concurrency curve: restart st_httpserver per run, drive it with st_httpclient.
# st_wrk -d is only a duration label (one batch then exit), so it is not used.
# DNS (-n == -c, one query per coroutine) is recorded but not treated as sustained.
#
# Output (gitignored): reports/curve-YYYYMMDD-HHMM.csv (+ .md and .svg via plot_curve.py)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

PORT=${BENCH_CURVE_PORT:-8765}
HOST=${BENCH_CURVE_HOST:-127.0.0.1}
DNS_PORT=${BENCH_CURVE_DNS_PORT:-5353}
DNS_HOST=${BENCH_CURVE_DNS_HOST:-127.0.0.1}
CONCS=${BENCH_CURVE_CONCS:-"1 10 50 100 200 500 1000"}
REPEATS=${BENCH_CURVE_REPEATS:-3}
MIN_N=${BENCH_CURVE_MIN_N:-50000}
PER_CORO=${BENCH_CURVE_PER_CORO:-30}
CLIENT_TIMEOUT_MS=${BENCH_CURVE_CLIENT_TIMEOUT_MS:-15000}
WALL_TIMEOUT_S=${BENCH_CURVE_WALL_TIMEOUT_S:-120}
DO_DNS=${BENCH_CURVE_DNS:-1}

HTTP_BIN=${HTTP_BIN:-$ROOT/app/st_httpserver/main}
CLIENT=${CLIENT:-$ROOT/app/st_httpclient/st_httpclient}
DNS_SRV=${DNS_SRV:-$ROOT/app/st_dnsserver/main}
DNS_CLI=${DNS_CLI:-$ROOT/app/st_dns/main}
REPORT_DIR=${REPORT_DIR:-$ROOT/reports}
URL="http://${HOST}:${PORT}/"

if [ ! -x "$HTTP_BIN" ] || [ ! -x "$CLIENT" ]; then
  echo "missing httpserver or st_httpclient (run: make apps)" >&2
  exit 1
fi
if [ "$DO_DNS" != "0" ]; then
  if [ ! -x "$DNS_SRV" ] || [ ! -x "$DNS_CLI" ]; then
    echo "missing dnsserver or st_dns (run: make apps)" >&2
    exit 1
  fi
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required to plot the curve" >&2
  exit 1
fi

mkdir -p "$REPORT_DIR"
STAMP=$(date +%Y%m%d-%H%M)
CSV="$REPORT_DIR/curve-${STAMP}.csv"
MD="$REPORT_DIR/curve-${STAMP}.md"
QPS_SVG="$REPORT_DIR/curve-${STAMP}-qps.svg"
LAT_SVG="$REPORT_DIR/curve-${STAMP}-latency.svg"

COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
OS=$(uname -s)
KERNEL=$(uname -r)
ARCH=$(uname -m)
NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo unknown)
CPU=$(lscpu 2>/dev/null | awk -F: '/Model name/ {gsub(/^[ \t]+/, "", $2); print $2; exit}')
if [ -z "${CPU:-}" ]; then
  CPU=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo unknown)
fi
MEM=$(awk '/MemTotal/ {print $2 " kB"}' /proc/meminfo 2>/dev/null || echo unknown)
CC_BIN=${CXX:-${CC:-g++}}
COMPILER=$("$CC_BIN" --version 2>/dev/null | head -n 1 || echo unknown)
DUMPMACHINE=$("$CC_BIN" -dumpmachine 2>/dev/null || echo unknown)
NOFILE=$(ulimit -n 2>/dev/null || echo unknown)
DATE_UTC=$(date -u +%Y-%m-%d)
ldd_names() {
  ldd "$1" 2>/dev/null | awk '{print $1}' | tr '\n' ' ' | sed 's/ *$//'
}
relpath() {
  case "$1" in
    "$ROOT"/*) printf '%s\n' "${1#"$ROOT"/}" ;;
    *) printf '%s\n' "$1" ;;
  esac
}
LDD_HTTP=$(ldd_names "$HTTP_BIN" || echo unknown)
LDD_CLIENT=$(ldd_names "$CLIENT" || echo unknown)

SERVER_PID=""
cleanup() {
  if [ -n "$SERVER_PID" ]; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
  fi
}
trap cleanup EXIT INT TERM HUP

if command -v lsof >/dev/null 2>&1; then
  busy=$(lsof -nP -iTCP:"$PORT" -sTCP:LISTEN 2>/dev/null | awk 'NR>1 {print $1"/"$2; exit}') || true
  if [ -n "${busy:-}" ]; then
    echo "HTTP port $PORT already in use by $busy" >&2
    echo "free it, or: BENCH_CURVE_PORT=<free> make bench-curve" >&2
    exit 1
  fi
fi

meta() {
  # One CSV comment line. Newlines in the value become spaces.
  printf '# %s=%s\n' "$1" "$(printf '%s' "$2" | tr '\n' ' ')" >>"$CSV"
}

: >"$CSV"
meta date "$DATE_UTC"
meta commit "$COMMIT"
meta os "$OS"
meta kernel "$KERNEL"
meta arch "$ARCH"
meta nproc "$NPROC"
meta cpu "$CPU"
meta mem_total "$MEM"
meta compiler "$COMPILER"
meta dumpmachine "$DUMPMACHINE"
meta nofile "$NOFILE"
meta build "make apps TRACE=0 (DEBUG=${DEBUG:-1} ASAN=${ASAN:-0} TCMALLOC=${TCMALLOC:-0} PROFILER=${PROFILER:-0})"
meta trace "${TRACE:-unset}"
meta http_bin "$(relpath "$HTTP_BIN")"
meta http_client "$(relpath "$CLIENT")"
meta http_url "$URL"
meta dns_bin "$(relpath "$DNS_SRV")"
meta dns_client "$(relpath "$DNS_CLI")"
meta dns_addr "${DNS_HOST}:${DNS_PORT}"
meta min_n "$MIN_N"
meta per_coro "$PER_CORO"
meta repeats "$REPEATS"
meta client_timeout_ms "$CLIENT_TIMEOUT_MS"
meta wall_timeout_s "$WALL_TIMEOUT_S"
meta dns_enabled "$DO_DNS"
meta ldd_http "$LDD_HTTP"
meta ldd_client "$LDD_CLIENT"
printf '%s\n' "proto,conc,repeat,n,ok,fail,qps,elapsed_ms,p50_ms,p99_ms,exit_code,server_alive,pending" >>"$CSV"

requests_for() {
  c=$1
  n=$((c * PER_CORO))
  if [ "$n" -lt "$MIN_N" ]; then
    n=$MIN_N
  fi
  printf '%s\n' "$n"
}

# Parse a SUMMARY line into ok|fail|qps|elapsed|p50|p99|pending
parse_summary() {
  printf '%s\n' "$1" | awk '
    /^SUMMARY / { line = $0 }
    END {
      if (line == "") {
        print "||||||"
        exit
      }
      n = split(line, f, " ")
      for (i = 1; i <= n; i++) {
        eq = index(f[i], "=")
        if (eq > 0) {
          k = substr(f[i], 1, eq - 1)
          val[k] = substr(f[i], eq + 1)
        }
      }
      ok = val["ok"]
      if (ok == "") ok = val["success"]
      printf "%s|%s|%s|%s|%s|%s|%s\n", ok, val["fail"], val["qps"], val["elapsed_ms"], val["p50_ms"], val["p99_ms"], val["pending"]
    }
  '
}

run_cmd() {
  if command -v timeout >/dev/null 2>&1; then
    timeout "$WALL_TIMEOUT_S" "$@"
  else
    "$@"
  fi
}

wait_http() {
  i=0
  while [ "$i" -lt 20 ]; do
    if "$CLIENT" -q -t 2000 "$URL" >/dev/null 2>&1; then
      return 0
    fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
      return 1
    fi
    i=$((i + 1))
    sleep 1
  done
  return 1
}

start_http() {
  cleanup
  "$HTTP_BIN" "$PORT" >/dev/null 2>&1 &
  SERVER_PID=$!
  if wait_http; then
    return 0
  fi
  cleanup
  echo "httpserver health-check failed; retry with log" >&2
  "$HTTP_BIN" "$PORT" >"$REPORT_DIR/curve-${STAMP}-httpserver.log" 2>&1 &
  SERVER_PID=$!
  if wait_http; then
    return 0
  fi
  echo "st_httpserver did not become ready on $URL" >&2
  cat "$REPORT_DIR/curve-${STAMP}-httpserver.log" >&2 || true
  cleanup
  return 1
}

record_row() {
  proto=$1
  c=$2
  rep=$3
  n=$4
  rc=$5
  alive=$6
  blob=$7
  parsed=$(parse_summary "$blob") || true
  ok=""
  fail=""
  qps=""
  elapsed=""
  p50=""
  p99=""
  pending=""
  IFS='|' read ok fail qps elapsed p50 p99 pending <<EOF || true
$parsed
EOF
  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "$proto" "$c" "$rep" "$n" "$ok" "$fail" "$qps" "$elapsed" "$p50" "$p99" \
    "$rc" "$alive" "$pending" >>"$CSV"
  echo "  $proto c=$c rep=$rep n=$n exit=$rc alive=$alive SUMMARY ok=${ok:-?} fail=${fail:-?} qps=${qps:-?} elapsed_ms=${elapsed:-?} p50=${p50:-} p99=${p99:-} pending=${pending:-}"
}

run_http_point() {
  c=$1
  rep=$2
  n=$3
  if ! start_http; then
    record_row http "$c" "$rep" "$n" 1 0 ""
    return 1
  fi
  set +e
  out=$(run_cmd "$CLIENT" -q -c "$c" -n "$n" -t "$CLIENT_TIMEOUT_MS" "$URL" 2>&1)
  rc=$?
  set -e
  alive=0
  if kill -0 "$SERVER_PID" 2>/dev/null; then
    alive=1
  fi
  record_row http "$c" "$rep" "$n" "$rc" "$alive" "$out"
  cleanup
  if [ "$rc" -ne 0 ] || [ "$alive" -ne 1 ]; then
    return 1
  fi
  return 0
}

echo "curve CSV $CSV"
echo "HTTP concs: $CONCS  repeats=$REPEATS  min_n=$MIN_N  per_coro=$PER_CORO"
echo "server restarted every run (coroutine stacks are not reclaimed)"

stop=0
for c in $CONCS; do
  case "$c" in
    ''|*[!0-9]*)
      echo "bad concurrency: $c" >&2
      exit 1
      ;;
  esac
  if [ "$stop" -eq 1 ]; then
    echo "skip http c=$c after an unreliable point"
    break
  fi
  n=$(requests_for "$c")
  bad=0
  rep=1
  while [ "$rep" -le "$REPEATS" ]; do
    echo "=== http c=$c n=$n repeat=$rep/$REPEATS ==="
    if ! run_http_point "$c" "$rep" "$n"; then
      bad=1
    fi
    rep=$((rep + 1))
  done
  if [ "$bad" -eq 1 ]; then
    echo "http c=$c had a failed repeat; not sweeping higher concurrency" >&2
    stop=1
  fi
done

if [ "$DO_DNS" != "0" ]; then
  if command -v lsof >/dev/null 2>&1; then
    holders=$(lsof -nP -iUDP:"$DNS_PORT" 2>/dev/null | awk 'NR>1 {print $1}' | sort -u | tr '\n' ' ') || true
    if [ -n "${holders:-}" ] && [ "$DNS_PORT" = "5353" ] && [ -z "${BENCH_CURVE_DNS_PORT:-}" ]; then
      echo "note: UDP 5353 held by [$holders]; using 15353" >&2
      DNS_PORT=15353
    fi
  fi
  cleanup
  "$DNS_SRV" "$DNS_HOST" "$DNS_PORT" >/dev/null 2>&1 &
  SERVER_PID=$!
  sleep 1
  dns_up=0
  if kill -0 "$SERVER_PID" 2>/dev/null; then
    if "$DNS_CLI" -q -s "$DNS_HOST" -p "$DNS_PORT" -c 1 -n 1 -t 2000 www.1.bench.local >/dev/null 2>&1; then
      dns_up=1
    fi
  fi
  if [ "$dns_up" -ne 1 ]; then
    echo "st_dnsserver health-check failed; DNS curve rows omitted" >&2
    cleanup
  else
    echo "DNS burst probe (n == c, not sustained) udp://${DNS_HOST}:${DNS_PORT}"
    for c in $CONCS; do
      rep=1
      while [ "$rep" -le "$REPEATS" ]; do
        echo "=== dns c=$c n=$c repeat=$rep/$REPEATS ==="
        alive=0
        if kill -0 "$SERVER_PID" 2>/dev/null; then
          alive=1
        fi
        set +e
        out=$(run_cmd "$DNS_CLI" -q -s "$DNS_HOST" -p "$DNS_PORT" -c "$c" -n "$c" -t 3000 2>&1)
        rc=$?
        set -e
        if kill -0 "$SERVER_PID" 2>/dev/null; then
          alive=1
        else
          alive=0
        fi
        record_row dns "$c" "$rep" "$c" "$rc" "$alive" "$out"
        if [ "$alive" -ne 1 ]; then
          echo "dnsserver died; stopping DNS probe" >&2
          break 2
        fi
        rep=$((rep + 1))
      done
    done
    cleanup
  fi
fi

PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/plot_curve.py" "$CSV" \
  --qps "$QPS_SVG" --latency "$LAT_SVG" --md "$MD" \
  --embed-qps "$(basename "$QPS_SVG")" \
  --embed-latency "$(basename "$LAT_SVG")"

echo "wrote $CSV"
echo "wrote $MD"
echo "wrote $QPS_SVG"
echo "wrote $LAT_SVG"
echo "CURVE_CSV=$CSV"

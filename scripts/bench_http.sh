#!/bin/sh
# HTTP bench: start st_httpserver, health-check, run st_wrk matrix, write reports/http-*.md
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

PORT=${BENCH_HTTP_PORT:-8765}
HOST=${BENCH_HTTP_HOST:-127.0.0.1}
PROFILE=${BENCH_PROFILE:-smoke}
HTTP_BIN=${HTTP_BIN:-$ROOT/app/st_httpserver/main}
WRK_BIN=${WRK_BIN:-$ROOT/app/st_wrk/wrk}
REPORT_DIR=${REPORT_DIR:-$ROOT/reports}
URL="http://${HOST}:${PORT}/"

if [ ! -x "$HTTP_BIN" ]; then
  echo "missing $HTTP_BIN (run: make apps)" >&2
  exit 1
fi
if [ ! -x "$WRK_BIN" ]; then
  echo "missing $WRK_BIN (run: make apps)" >&2
  exit 1
fi
if ! command -v curl >/dev/null 2>&1; then
  echo "curl is required for HTTP health-check" >&2
  exit 1
fi

mkdir -p "$REPORT_DIR"
STAMP=$(date +%Y%m%d-%H%M)
REPORT="$REPORT_DIR/http-${STAMP}.md"
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
OS=$(uname -s)
ARCH=$(uname -m)
NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo unknown)

SERVER_PID=""
cleanup() {
  if [ -n "$SERVER_PID" ]; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
  fi
}
trap cleanup EXIT INT TERM HUP

"$HTTP_BIN" "$PORT" >"$REPORT_DIR/httpserver-${STAMP}.log" 2>&1 &
SERVER_PID=$!
sleep 1

i=0
healthy=0
while [ "$i" -lt 20 ]; do
  if curl -sf --max-time 1 "$URL" >/dev/null 2>&1; then
    healthy=1
    break
  fi
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "st_httpserver exited early; log:" >&2
    cat "$REPORT_DIR/httpserver-${STAMP}.log" >&2 || true
    exit 1
  fi
  i=$((i + 1))
  sleep 1
done
if [ "$healthy" -ne 1 ]; then
  echo "health-check failed for $URL" >&2
  exit 1
fi

run_one() {
  name=$1
  numbers=$2
  conns=$3
  dur=$4
  echo "=== $name: wrk -n $numbers -c $conns -d $dur --latency --json $URL ==="
  set +e
  out=$("$WRK_BIN" -n "$numbers" -c "$conns" -d "$dur" --latency --json "$URL" 2>&1)
  rc=$?
  set -e
  printf '%s\n' "$out"
  summary=$(printf '%s\n' "$out" | grep '^SUMMARY ' | tail -n 1 || true)
  json=$(printf '%s\n' "$out" | grep '^JSON ' | tail -n 1 || true)
  echo "$summary"
  printf '\n### %s (`-n %s -c %s -d %s`)\n\n' "$name" "$numbers" "$conns" "$dur" >>"$REPORT"
  echo "exit=$rc" >>"$REPORT"
  if [ -n "$summary" ]; then
    echo "" >>"$REPORT"
    echo '```' >>"$REPORT"
    echo "$summary" >>"$REPORT"
    if [ -n "$json" ]; then
      echo "$json" >>"$REPORT"
    fi
    echo '```' >>"$REPORT"
  fi
  echo "" >>"$REPORT"
  echo "<details><summary>raw wrk stdout</summary>" >>"$REPORT"
  echo "" >>"$REPORT"
  echo '```' >>"$REPORT"
  printf '%s\n' "$out" >>"$REPORT"
  echo '```' >>"$REPORT"
  echo "" >>"$REPORT"
  echo "</details>" >>"$REPORT"
  echo "" >>"$REPORT"
}

{
  echo "# HTTP bench $STAMP"
  echo ""
  echo "- OS: $OS / $ARCH (nproc=$NPROC)"
  echo "- commit: \`$COMMIT\`"
  echo "- server: \`$HTTP_BIN\` short-conn \`Connection: close\` port $PORT"
  echo "- client: \`$WRK_BIN\`"
  echo "- url: $URL"
  echo "- profile: $PROFILE"
  echo "- TRACE/DEBUG: TRACE=${TRACE:-unset} DEBUG=${DEBUG:-unset}"
  echo ""
  echo "## Notes"
  echo ""
  echo "- Main path is **HTTP short connections** (plan/08 D4=a); no keepalive reuse."
  echo "- \`st_wrk -n\` is worker **processes**; \`-c\` is total connections (\`>= -n\`)."
  echo "- \`-d\` is a duration **label**; each worker currently issues one batch then exits."
  echo ""
  echo "## Matrix"
  echo ""
} >"$REPORT"

case "$PROFILE" in
  smoke)
    run_one smoke 2 10 3s
    ;;
  medium)
    run_one medium 4 50 10s
    ;;
  heavy)
    run_one heavy 8 200 30s
    ;;
  all)
    run_one smoke 2 10 3s
    run_one medium 4 50 10s
    run_one heavy 8 200 30s
    ;;
  *)
    echo "unknown BENCH_PROFILE=$PROFILE (use smoke|medium|heavy|all)" >&2
    exit 1
    ;;
esac

{
  echo "## Interpretation"
  echo ""
  echo "- Compare smoke → medium → heavy for roughly linear req/s (short-conn + single OS-thread event loop will cap)."
  echo "- Errors should be 0 on loopback; non-zero needs investigation."
  echo "- This is **not** comparable to wrk-upstream keepalive HTTP/1.1 pipelining."
  echo ""
} >>"$REPORT"

echo "wrote $REPORT"
echo "HTTP_REPORT=$REPORT"

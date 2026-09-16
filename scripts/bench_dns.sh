#!/bin/sh
# DNS bench: start st_dnsserver, health-check with st_dns, write reports/dns-*.md
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

PORT=${BENCH_DNS_PORT:-5353}
HOST=${BENCH_DNS_HOST:-127.0.0.1}
PROFILE=${BENCH_PROFILE:-smoke}
DNS_SRV=${DNS_SRV:-$ROOT/app/st_dnsserver/main}
DNS_CLI=${DNS_CLI:-$ROOT/app/st_dns/main}
REPORT_DIR=${REPORT_DIR:-$ROOT/reports}

if [ ! -x "$DNS_SRV" ]; then
  echo "missing $DNS_SRV (run: make apps)" >&2
  exit 1
fi
if [ ! -x "$DNS_CLI" ]; then
  echo "missing $DNS_CLI (run: make apps)" >&2
  exit 1
fi

mkdir -p "$REPORT_DIR"
STAMP=$(date +%Y%m%d-%H%M)
REPORT="$REPORT_DIR/dns-${STAMP}.md"
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

"$DNS_SRV" "$HOST" "$PORT" >"$REPORT_DIR/dnsserver-${STAMP}.log" 2>&1 &
SERVER_PID=$!
sleep 1

i=0
healthy=0
while [ "$i" -lt 20 ]; do
  if "$DNS_CLI" -q -s "$HOST" -p "$PORT" -c 1 -n 1 -t 2000 www.1.bench.local \
      >/tmp/st-dns-health.out 2>/tmp/st-dns-health.err; then
    if grep -q '^SUMMARY success=1 ' /tmp/st-dns-health.out; then
      healthy=1
      break
    fi
  fi
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "st_dnsserver exited early; log:" >&2
    cat "$REPORT_DIR/dnsserver-${STAMP}.log" >&2 || true
    exit 1
  fi
  i=$((i + 1))
  sleep 1
done
if [ "$healthy" -ne 1 ]; then
  echo "DNS health-check failed for $HOST:$PORT" >&2
  cat /tmp/st-dns-health.out >&2 || true
  cat /tmp/st-dns-health.err >&2 || true
  cat "$REPORT_DIR/dnsserver-${STAMP}.log" >&2 || true
  exit 1
fi

run_one() {
  name=$1
  coros=$2
  queries=$3
  timeout_ms=$4
  echo "=== $name: st_dns -s $HOST -p $PORT -c $coros -n $queries -t $timeout_ms ==="
  set +e
  out=$("$DNS_CLI" -q -s "$HOST" -p "$PORT" -c "$coros" -n "$queries" -t "$timeout_ms" 2>&1)
  rc=$?
  set -e
  printf '%s\n' "$out"
  summary=$(printf '%s\n' "$out" | grep '^SUMMARY ' | tail -n 1 || true)
  printf '\n### %s (`-c %s -n %s -t %s`)\n\n' "$name" "$coros" "$queries" "$timeout_ms" >>"$REPORT"
  echo "exit=$rc" >>"$REPORT"
  echo "" >>"$REPORT"
  echo '```' >>"$REPORT"
  printf '%s\n' "$out" >>"$REPORT"
  echo '```' >>"$REPORT"
  echo "" >>"$REPORT"
}

{
  echo "# DNS bench $STAMP"
  echo ""
  echo "- OS: $OS / $ARCH (nproc=$NPROC)"
  echo "- commit: \`$COMMIT\`"
  echo "- server: \`$DNS_SRV\` udp://$HOST:$PORT zone \`*.bench.local\` → 127.0.0.1"
  echo "- client: \`$DNS_CLI\`"
  echo "- profile: $PROFILE"
  echo "- TRACE/DEBUG: TRACE=${TRACE:-unset} DEBUG=${DEBUG:-unset}"
  echo ""
  echo "## Notes"
  echo ""
  echo "- Default listen port is **5353** (plan/08 D5=a); no public resolver involved."
  echo "- Non-A queries get empty ANSWER + NOERROR; this matrix only queries TYPE_A."
  echo "- UDP loopback may still drop under heavy \`-c\`; fail count is in SUMMARY."
  echo "- Same-process **sequential** UDP (one coro, many queries) currently fails after the first lookup; this matrix uses \`-n == -c\` (one query per coroutine)."
  echo ""
  echo "## Matrix"
  echo ""
} >"$REPORT"

case "$PROFILE" in
  smoke)
    run_one smoke 10 10 2000
    ;;
  medium)
    run_one medium 50 50 3000
    ;;
  heavy)
    run_one heavy 100 100 5000
    ;;
  all)
    run_one smoke 10 10 2000
    run_one medium 50 50 3000
    run_one heavy 100 100 5000
    ;;
  *)
    echo "unknown BENCH_PROFILE=$PROFILE (use smoke|medium|heavy|all)" >&2
    exit 1
    ;;
esac

{
  echo "## Interpretation"
  echo ""
  echo "- QPS is successful A lookups / wall time on this host."
  echo "- \`fail=0\` is the expected loopback result for smoke/medium (one query per coroutine)."
  echo ""
} >>"$REPORT"

echo "wrote $REPORT"
echo "DNS_REPORT=$REPORT"

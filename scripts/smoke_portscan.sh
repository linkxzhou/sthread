#!/bin/sh
# 本地冒烟：echo 端口记 open，没人听的端口记 refused。不进 CI。
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

PORT=${ECHO_PORT:-17707}
HOST=${ECHO_HOST:-127.0.0.1}
CLOSED=${PORTSCAN_CLOSED:-1}
SERVER=${ECHO_SERVER:-$ROOT/app/st_echo/st_echoserver}
SCAN=${PORTSCAN_BIN:-$ROOT/app/st_portscan/st_portscan}
OUT=/tmp/sthread-portscan.out
LOG=/tmp/sthread-portscan-echo.log
SERVER_PID=""

if [ ! -x "$SERVER" ] || [ ! -x "$SCAN" ]; then
  echo "missing binaries (run: make apps)" >&2
  exit 1
fi

cleanup() {
  if [ -n "$SERVER_PID" ]; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
  fi
}
trap cleanup EXIT INT TERM HUP

"$SERVER" "$HOST" "$PORT" >"$LOG" 2>&1 &
SERVER_PID=$!
i=0
while [ "$i" -lt 20 ]; do
  if grep -q "listening" "$LOG" 2>/dev/null; then
    break
  fi
  i=$((i + 1))
  sleep 0.1
done
grep -q "listening" "$LOG"

"$SCAN" -c 32 -t 300 -p "${PORT},${CLOSED}" "$HOST" >"$OUT"
grep -q "OPEN ${PORT}" "$OUT"
grep -q 'refused=' "$OUT"
grep -q 'timeout=0 ' "$OUT"
awk -v closed="$CLOSED" '
  /SUMMARY/ {
    if ($2 ~ /^open=/ && $3 ~ /^refused=/) {
      split($3, a, "=")
      if (a[2] + 0 < 1) {
        print "FAIL refused < 1" > "/dev/stderr"
        exit 1
      }
    }
  }
' "$OUT"
echo "smoke-portscan ok"

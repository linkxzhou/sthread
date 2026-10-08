#!/bin/sh
# 本地冒烟：纯 libc blocking_client，以及 st_hookdemo 对 echo。不进 CI。
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

PORT=${ECHO_PORT:-17708}
HOST=${ECHO_HOST:-127.0.0.1}
SERVER=${ECHO_SERVER:-$ROOT/app/st_echo/st_echoserver}
BLOCK=${BLOCKING_CLIENT:-$ROOT/app/st_hookdemo/blocking_client}
HOOK=${HOOKDEMO:-$ROOT/app/st_hookdemo/st_hookdemo}
OUT=/tmp/sthread-hook.out
LOG=/tmp/sthread-hook-echo.log
SERVER_PID=""

if [ ! -x "$SERVER" ] || [ ! -x "$BLOCK" ] || [ ! -x "$HOOK" ]; then
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

"$BLOCK" "$HOST" "$PORT" hook >"$OUT"
grep -q '^hook$' "$OUT"
echo "ok blocking_client"

"$HOOK" -c 4 -n 4 -s hook "$HOST" "$PORT" >"$OUT"
grep -q 'ok=4 ' "$OUT"
grep -q 'fail=0 ' "$OUT"
echo "smoke-hook ok"

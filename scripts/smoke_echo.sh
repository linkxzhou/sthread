#!/bin/sh
# 本地冒烟：st_echoserver + st_echoclient。不进 CI。
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

PORT=${ECHO_PORT:-17707}
HOST=${ECHO_HOST:-127.0.0.1}
SERVER=${ECHO_SERVER:-$ROOT/app/st_echo/st_echoserver}
CLIENT=${ECHO_CLIENT:-$ROOT/app/st_echo/st_echoclient}
OUT=/tmp/sthread-echo-case.out
LOG=/tmp/sthread-echo-server.log
fail=0
SERVER_PID=""

if [ ! -x "$SERVER" ] || [ ! -x "$CLIENT" ]; then
  echo "missing echo binaries (run: make apps)" >&2
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

if command -v lsof >/dev/null 2>&1; then
  busy=$(lsof -nP -iTCP:"$PORT" -sTCP:LISTEN 2>/dev/null | awk 'NR>1 {print $1"/"$2; exit}') || true
  if [ -n "${busy:-}" ]; then
    echo "port $PORT already in use by $busy" >&2
    exit 1
  fi
fi

"$SERVER" "$HOST" "$PORT" >"$LOG" 2>&1 &
SERVER_PID=$!

i=0
while [ "$i" -lt 20 ]; do
  if grep -q "listening" "$LOG" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "st_echoserver exited early" >&2
    cat "$LOG" >&2 || true
    exit 1
  fi
  i=$((i + 1))
  sleep 0.1
done
grep -q "listening" "$LOG"

set +e
"$CLIENT" -c 1 -n 1 -s ping "$HOST" "$PORT" >"$OUT" 2>&1
rc=$?
set -e
if [ "$rc" -ne 0 ]; then
  echo "FAIL echo once exit=$rc" >&2
  cat "$OUT" >&2 || true
  fail=1
fi
grep -q '^ping$' "$OUT" || { echo "FAIL echo payload" >&2; cat "$OUT" >&2; fail=1; }
grep -q 'ok=1 ' "$OUT" || { echo "FAIL echo ok=1" >&2; cat "$OUT" >&2; fail=1; }
echo "ok once"

set +e
"$CLIENT" -c 8 -n 40 -s ping "$HOST" "$PORT" >"$OUT" 2>&1
rc=$?
set -e
if [ "$rc" -ne 0 ]; then
  echo "FAIL echo conc exit=$rc" >&2
  cat "$OUT" >&2 || true
  fail=1
fi
grep -q 'ok=40 ' "$OUT" || { echo "FAIL ok=40" >&2; cat "$OUT" >&2; fail=1; }
grep -q 'fail=0 ' "$OUT" || { echo "FAIL fail=0" >&2; cat "$OUT" >&2; fail=1; }
echo "ok conc"

set +e
"$CLIENT" -c 1 -n 1 -t 300 -s ping "$HOST" 1 >"$OUT" 2>&1
rc=$?
set -e
if [ "$rc" -ne 1 ]; then
  echo "FAIL echo refused exit=$rc" >&2
  cat "$OUT" >&2 || true
  fail=1
fi
grep -q 'fail=' "$OUT" || { echo "FAIL refused summary" >&2; cat "$OUT" >&2; fail=1; }
echo "ok refused"

if [ "$fail" -ne 0 ]; then
  exit 1
fi
echo "smoke-echo ok"

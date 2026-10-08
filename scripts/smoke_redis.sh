#!/bin/sh
# 本地冒烟：redis-server 或 python stub。不进 CI。
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

PORT=${REDIS_PORT:-16379}
HOST=${REDIS_HOST:-127.0.0.1}
CLIENT=${REDIS_CLIENT:-$ROOT/app/st_redisclient/st_redisclient}
OUT=/tmp/sthread-redis.out
LOG=/tmp/sthread-redis-server.log
SERVER_PID=""

if [ ! -x "$CLIENT" ]; then
  echo "missing st_redisclient (run: make apps)" >&2
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

if command -v redis-server >/dev/null 2>&1; then
  redis-server --save "" --appendonly no --bind "$HOST" --port "$PORT" \
    --protected-mode no --daemonize no >"$LOG" 2>&1 &
  SERVER_PID=$!
else
  python3 "$ROOT/scripts/redis_stub.py" "$PORT" >"$LOG" 2>&1 &
  SERVER_PID=$!
fi

i=0
while [ "$i" -lt 30 ]; do
  if grep -q "listening\|Ready to accept" "$LOG" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "redis stub exited early" >&2
    cat "$LOG" >&2 || true
    exit 1
  fi
  i=$((i + 1))
  sleep 0.1
done

"$CLIENT" -h "$HOST" -p "$PORT" ping >"$OUT"
grep -q 'ok=1 ' "$OUT"
grep -q 'PONG' "$OUT"
echo "ok ping"

"$CLIENT" -h "$HOST" -p "$PORT" set k v >"$OUT"
grep -q 'ok=1 ' "$OUT"
echo "ok set"

"$CLIENT" -h "$HOST" -p "$PORT" get k >"$OUT"
grep -q '^v$' "$OUT"
grep -q 'ok=1 ' "$OUT"
echo "ok get"

"$CLIENT" -h "$HOST" -p "$PORT" incr n >"$OUT"
grep -q '^1$' "$OUT"
grep -q 'ok=1 ' "$OUT"
echo "ok incr"

"$CLIENT" -h "$HOST" -p "$PORT" -c 4 -n 20 -q ping >"$OUT"
grep -q 'ok=20 ' "$OUT"
grep -q 'fail=0 ' "$OUT"
echo "smoke-redis ok"

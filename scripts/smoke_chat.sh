#!/bin/sh
# 本地冒烟：B 读到 ada: hello。先等 listening 再起 B，避免连上之前就超时。不进 CI。
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

PORT=${CHAT_PORT:-17700}
HOST=${CHAT_HOST:-127.0.0.1}
SERVER=${CHAT_SERVER:-$ROOT/app/st_chat/st_chatserver}
CLIENT=${CHAT_CLIENT:-$ROOT/app/st_chat/st_chatclient}
LOG=/tmp/sthread-chat-server.log
BOUT=/tmp/sthread-chat-b.out
AOUT=/tmp/sthread-chat-a.out
SERVER_PID=""
B_PID=""

if [ ! -x "$SERVER" ] || [ ! -x "$CLIENT" ]; then
  echo "missing chat binaries (run: make apps)" >&2
  exit 1
fi

cleanup() {
  if [ -n "$B_PID" ]; then
    kill "$B_PID" 2>/dev/null || true
    wait "$B_PID" 2>/dev/null || true
    B_PID=""
  fi
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

"$CLIENT" -n bob -t 3000 "$HOST" "$PORT" >"$BOUT" 2>&1 &
B_PID=$!
sleep 0.4
"$CLIENT" -n ada -t 3000 "$HOST" "$PORT" hello >"$AOUT" 2>&1 || true
wait "$B_PID"
brc=$?
B_PID=""
if [ "$brc" -ne 0 ]; then
  echo "FAIL chat B exit=$brc" >&2
  cat "$BOUT" >&2 || true
  exit 1
fi
grep -q "ada: hello" "$BOUT"
echo "smoke-chat ok"

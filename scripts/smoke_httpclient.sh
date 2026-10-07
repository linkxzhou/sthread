#!/bin/sh
# 对 st_httpserver 跑 st_httpclient 冒烟。退出码见 app/st_httpclient/README.md。
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

PORT=${HTTPCLIENT_PORT:-18765}
HOST=${HTTPCLIENT_HOST:-127.0.0.1}
HTTP_BIN=${HTTP_BIN:-$ROOT/app/st_httpserver/main}
CLIENT=${CLIENT:-$ROOT/app/st_httpclient/st_httpclient}
URL="http://${HOST}:${PORT}/"
OUT=/tmp/sthread-httpclient-case.out
ERR=/tmp/sthread-httpclient-case.err
fail=0

if [ ! -x "$HTTP_BIN" ]; then
  echo "missing $HTTP_BIN (run: make apps)" >&2
  exit 1
fi
if [ ! -x "$CLIENT" ]; then
  echo "missing $CLIENT (run: make apps)" >&2
  exit 1
fi

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
    echo "port $PORT already in use by $busy" >&2
    exit 1
  fi
fi

"$HTTP_BIN" "$PORT" >/tmp/sthread-httpserver-smoke.log 2>&1 &
SERVER_PID=$!

i=0
healthy=0
while [ "$i" -lt 20 ]; do
  if "$CLIENT" -q -t 1000 "$URL" >"$OUT" 2>"$ERR"; then
    healthy=1
    break
  fi
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "st_httpserver exited early" >&2
    cat /tmp/sthread-httpserver-smoke.log >&2 || true
    exit 1
  fi
  i=$((i + 1))
  sleep 1
done
if [ "$healthy" -ne 1 ]; then
  echo "server did not become ready" >&2
  cat "$ERR" >&2 || true
  exit 1
fi

run_case() {
  name=$1
  expect=$2
  shift 2
  set +e
  "$@" >"$OUT" 2>"$ERR"
  rc=$?
  set -e
  if [ "$rc" -ne "$expect" ]; then
    echo "FAIL $name exit=$rc want=$expect" >&2
    cat "$OUT" >&2 || true
    cat "$ERR" >&2 || true
    fail=1
    return 1
  fi
  echo "ok $name"
  return 0
}

run_case get 0 "$CLIENT" -q -t 3000 "$URL" || true
grep -q 'ok=1 ' "$OUT" || { echo "FAIL summary ok=1" >&2; cat "$OUT" >&2; fail=1; }
grep -q 'fail=0 ' "$OUT" || { echo "FAIL summary fail=0" >&2; cat "$OUT" >&2; fail=1; }

run_case conc 0 "$CLIENT" -q -c 10 -n 100 -t 3000 "$URL" || true
grep -q 'ok=100 ' "$OUT" || { echo "FAIL ok=100" >&2; cat "$OUT" >&2; fail=1; }
grep -q 'fail=0 ' "$OUT" || { echo "FAIL conc fail" >&2; cat "$OUT" >&2; fail=1; }

run_case post 0 "$CLIENT" -q -X POST -d 'a=1' -t 3000 "$URL" || true
grep -q 'ok=1 ' "$OUT" || { echo "FAIL post" >&2; cat "$OUT" >&2; fail=1; }

run_case refused 1 "$CLIENT" -q -t 500 "http://${HOST}:1/" || true
run_case https 2 "$CLIENT" -q "https://${HOST}/" || true

if command -v curl >/dev/null 2>&1; then
  curl -sf --max-time 2 "$URL" >/tmp/sthread-curl-body || {
    echo "FAIL curl" >&2
    fail=1
  }
  "$CLIENT" -t 3000 "$URL" >/tmp/sthread-client-body
  sed '$d' /tmp/sthread-client-body >/tmp/sthread-client-only
  if ! cmp -s /tmp/sthread-curl-body /tmp/sthread-client-only; then
    echo "FAIL body mismatch vs curl" >&2
    echo "curl: $(cat /tmp/sthread-curl-body)" >&2
    echo "client: $(cat /tmp/sthread-client-only)" >&2
    fail=1
  else
    echo "ok body"
  fi
else
  echo "curl not found, skip body compare" >&2
fi

run_case stress 0 "$CLIENT" -q -c 50 -n 1000 -t 3000 "$URL" || true
grep -q 'ok=1000 ' "$OUT" || { echo "FAIL stress ok" >&2; cat "$OUT" >&2; fail=1; }
grep -q 'fail=0 ' "$OUT" || { echo "FAIL stress fail" >&2; cat "$OUT" >&2; fail=1; }

run_case keepalive 0 "$CLIENT" -q -k -c 1 -n 4 -t 3000 "$URL" || true
grep -q 'ok=4 ' "$OUT" || { echo "FAIL keepalive" >&2; cat "$OUT" >&2; fail=1; }

if [ "$fail" -ne 0 ]; then
  exit 1
fi
echo "smoke_httpclient ok"

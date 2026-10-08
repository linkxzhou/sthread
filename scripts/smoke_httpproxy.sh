#!/bin/sh
# 本地冒烟：两个 httpserver，停一个仍 200，都停则 502。不进 CI。
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

B1=${PROXY_B1:-18771}
B2=${PROXY_B2:-18772}
PROXY=${PROXY_PORT:-18080}
HOST=${PROXY_HOST:-127.0.0.1}
HTTP=${HTTP_BIN:-$ROOT/app/st_httpserver/main}
BIN=${PROXY_BIN:-$ROOT/app/st_httpproxy/st_httpproxy}
P1=""
P2=""
PP=""

if [ ! -x "$HTTP" ] || [ ! -x "$BIN" ]; then
  echo "missing binaries (run: make apps)" >&2
  exit 1
fi
if ! command -v curl >/dev/null 2>&1; then
  echo "curl is required for smoke-proxy" >&2
  exit 1
fi

cleanup() {
  for p in $PP $P1 $P2; do
    if [ -n "$p" ]; then
      kill "$p" 2>/dev/null || true
      wait "$p" 2>/dev/null || true
    fi
  done
}
trap cleanup EXIT INT TERM HUP

"$HTTP" "$B1" >/tmp/sthread-proxy-b1.log 2>&1 &
P1=$!
"$HTTP" "$B2" >/tmp/sthread-proxy-b2.log 2>&1 &
P2=$!
"$BIN" -l "${HOST}:${PROXY}" -b "${HOST}:${B1}" -b "${HOST}:${B2}" -H 400 \
  >/tmp/sthread-proxy.log 2>&1 &
PP=$!

i=0
while [ "$i" -lt 30 ]; do
  if grep -q "listening" /tmp/sthread-proxy.log 2>/dev/null; then
    break
  fi
  i=$((i + 1))
  sleep 0.1
done
grep -q "listening" /tmp/sthread-proxy.log

body=$(curl -sf --max-time 3 -x "http://${HOST}:${PROXY}/" \
  "http://${HOST}:${B1}/")
printf '%s' "$body" | grep -q "hello from sthread"
echo "ok both"

kill "$P1"
wait "$P1" 2>/dev/null || true
P1=""
sleep 1
body=$(curl -sf --max-time 3 -x "http://${HOST}:${PROXY}/" \
  "http://${HOST}:${B2}/")
printf '%s' "$body" | grep -q "hello from sthread"
echo "ok one down"

kill "$P2"
wait "$P2" 2>/dev/null || true
P2=""
sleep 1
code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 3 \
  -x "http://${HOST}:${PROXY}/" "http://${HOST}:${B2}/")
if [ "$code" != "502" ]; then
  echo "FAIL want 502 got $code" >&2
  exit 1
fi
echo "smoke-proxy ok"

#!/usr/bin/env bash
# Push the current (signed) firmware build to a dongle over WiFi.
#
#   tools/ota-push.sh <dongle-ip-or-hostname> [--no-build]
#
# The dongle flashes the image to its inactive OTA slot, verifies the RSA
# signature, reboots into it, and rolls back automatically if the new
# firmware never comes up healthy.
#
# Firmware 0.4.0+ requires a pairing token; this script pairs itself on
# first use (the dongle must be within its pairing window: ~5 min after
# power-on or after a button tap) and caches the token per host under
# ~/.ember-link/.
set -euo pipefail

HOST=${1:?usage: ota-push.sh <dongle-ip-or-hostname> [--no-build]}
FW_DIR="$(cd "$(dirname "$0")/../firmware" && pwd)"
BIN="$FW_DIR/build/ember-link.bin"

TOKEN_DIR="$HOME/.ember-link"
TOKEN_FILE="$TOKEN_DIR/token-$HOST"
mkdir -p "$TOKEN_DIR"
TOKEN=$(cat "$TOKEN_FILE" 2>/dev/null || true)

pair() {
    echo "==> pairing with $HOST"
    local resp
    resp=$(curl -s --max-time 5 -X POST -H 'Content-Type: application/json' \
        -d '{"name":"ota-push ('"$(whoami)"')"}' "http://$HOST/api/pair") || resp=""
    TOKEN=$(sed -n 's/.*"token":"\([^"]*\)".*/\1/p' <<<"$resp")
    if [[ -z "$TOKEN" ]]; then
        echo "pairing refused: ${resp:-no answer}" >&2
        echo "power-cycle the dongle (or tap its button), then retry within 5 minutes" >&2
        exit 1
    fi
    (umask 077 && printf '%s' "$TOKEN" > "$TOKEN_FILE")
}

# Pre-auth firmware ignores the header; 0.4.0+ answers 401 until we pair.
status=$(curl -s -o /dev/null -w '%{http_code}' --max-time 5 \
    -H "Authorization: Bearer ${TOKEN:-none}" "http://$HOST/api/update") || {
    echo "dongle not reachable at $HOST" >&2
    exit 1
}
if [[ "$status" == "401" ]]; then
    pair
fi

if [[ "${2:-}" != "--no-build" ]]; then
    echo "==> building"
    (source "$HOME/esp/esp-idf/export.sh" >/dev/null 2>&1 && cd "$FW_DIR" && idf.py build) \
        | tail -3
fi
[[ -f "$BIN" ]] || { echo "no build at $BIN"; exit 1; }

OLD=$(curl -sf --max-time 5 "http://$HOST/api/health" | sed -n 's/.*"version":"\([^"]*\)".*/\1/p')
echo "==> dongle at $HOST is running ${OLD:-unknown}; pushing $(du -h "$BIN" | cut -f1) image"

curl --fail-with-body -X POST --data-binary "@$BIN" \
    -H "Authorization: Bearer ${TOKEN:-none}" \
    -H "Content-Type: application/octet-stream" "http://$HOST/api/update"
echo

echo "==> rebooting; waiting for the dongle to come back"
for _ in $(seq 1 30); do
    sleep 2
    NEW=$(curl -sf --max-time 3 "http://$HOST/api/health" \
        | sed -n 's/.*"version":"\([^"]*\)".*/\1/p') || true
    if [[ -n "${NEW:-}" ]]; then
        echo "==> dongle is back, running $NEW"
        curl -sf -H "Authorization: Bearer ${TOKEN:-none}" "http://$HOST/api/update"; echo
        exit 0
    fi
done
echo "dongle did not come back within 60 s — check the LED" >&2
exit 1

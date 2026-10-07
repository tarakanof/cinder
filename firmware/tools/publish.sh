#!/bin/bash
set -eu

usage() {
    cat <<'EOF'
Usage: tools/publish.sh [--release] [--replace] [--no-build] [--dir DIR] [build_release.sh options]

Builds a secret-free image (tools/build_release.sh) and uploads cinder.bin and cinder.elf
to Ember: POST /v1/firmware?channel=test|release, then PUT /v1/firmware/<version>/elf.
Reads only EMBER_SERVER_URL and the owner EMBER_TOKEN from $EMBER_ENV_FILE (default
~/.config/ember/producer.env; never exported or printed; the token goes to curl on stdin, not
argv). Fails unless Ember then lists the version with the local SHA-256, the requested channel
(test bytes are promoted with PATCH for --release; release bytes are never demoted to test)
and the ELF.

  --release    channel release (Automatic mode may install it); default test
  --replace    replace a stored version with other bytes (not while it is a target)
  --no-build   upload what DIR already holds (tools/release.sh: the GitHub Release assets)
EOF
}

FW="$(cd "$(dirname "$0")/.." && pwd)"
ENV_FILE="${EMBER_ENV_FILE:-$HOME/.config/ember/producer.env}"
CHANNEL="test"
REPLACE=""
BUILD=1
DIR=build-release
PASS=()
while [ $# -gt 0 ]; do
    case "$1" in
    --release) CHANNEL=release; shift ;;
    --replace) REPLACE="&replace=1"; shift ;;
    --no-build) BUILD=0; shift ;;
    --dir) DIR="$2"; PASS+=(--dir "$2"); shift 2 ;;
    --version | --ota-test) PASS+=("$1" "$2"); shift 2 ;;
    -h | --help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
    esac
done

[ -f "$ENV_FILE" ] || { echo "missing $ENV_FILE" >&2; exit 1; }
ENV_FILE="$(cd "$(dirname "$ENV_FILE")" && pwd)/$(basename "$ENV_FILE")"
read_env() {
    local key="$1" line v
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line#export }"
        case "$line" in
        "$key="*)
            v="${line#*=}"
            v="${v%\"}"; v="${v#\"}"; v="${v%\'}"; v="${v#\'}"
            printf '%s' "$v"
            return 0
            ;;
        esac
    done <"$ENV_FILE"
    return 1
}
TOKEN="$(read_env EMBER_TOKEN || true)"
SERVER="$(read_env EMBER_SERVER_URL || true)"
[ -n "$TOKEN" ] && [ -n "$SERVER" ] || {
    echo "EMBER_TOKEN or EMBER_SERVER_URL missing in $ENV_FILE" >&2
    exit 1
}

if [ "$BUILD" = 1 ]; then
    "$FW/tools/build_release.sh" ${PASS[@]+"${PASS[@]}"}
fi
cd "$FW"
BIN="$DIR/cinder.bin"
ELF="$DIR/cinder.elf"
[ -f "$BIN" ] && [ -f "$ELF" ] || { echo "no $BIN / $ELF" >&2; exit 1; }
if grep -q '^CONFIG_CINDER_DEV_SEED=y' "$DIR/sdkconfig" 2>/dev/null; then
    echo "$DIR is a dev-seed build: refusing to upload" >&2
    exit 1
fi
python3 "$FW/tools/secret_scan.py" --env "$ENV_FILE" "$BIN" "$ELF" || {
    echo "secret scan failed: refusing to upload" >&2
    exit 1
}
VERSION="$(python3 -c 'import sys; print(open(sys.argv[1],"rb").read()[48:80].split(b"\0")[0].decode())' "$BIN")"
SHA="$(shasum -a 256 "$BIN" | cut -d' ' -f1)"

auth() { printf 'header = "Authorization: Bearer %s"\n' "$TOKEN"; }
field() { python3 -c 'import json,sys; v=json.load(sys.stdin).get(sys.argv[1]); print(str(v).lower() if isinstance(v,bool) else v)' "$1"; }

HOST="${SERVER#*://}"
HOST="${HOST%%/*}"
echo "uploading cinder $VERSION ($CHANNEL) to Ember at ${HOST##*@}"
RESP="$(auth | curl -sS --fail-with-body -K - -X POST -H 'Content-Type: application/octet-stream' \
    --data-binary @"$BIN" "$SERVER/v1/firmware?channel=$CHANNEL$REPLACE")" || {
    printf '%s\n' "$RESP" >&2
    exit 1
}
printf '%s\n' "$RESP"
STORED="$(printf '%s' "$RESP" | field channel)"
case "$STORED/$CHANNEL" in
test/release)
    echo "Ember held these bytes on channel test: promoting to release"
    auth | curl -sS --fail-with-body -K - -X PATCH -H 'Content-Type: application/json' \
        --data '{"channel":"release"}' "$SERVER/v1/firmware/$VERSION" >/dev/null
    ;;
release/test)
    echo "Ember already holds these bytes as release: kept as release (publish.sh never demotes)"
    CHANNEL=release
    ;;
esac
echo "uploading cinder.elf for $VERSION"
auth | curl -sS --fail-with-body -K - -X PUT -H 'Content-Type: application/octet-stream' \
    -T "$ELF" "$SERVER/v1/firmware/$VERSION/elf"

LIST="$(auth | curl -sS --fail-with-body -K - "$SERVER/v1/firmware")"
printf '%s' "$LIST" | python3 -c '
import json, sys
version, sha, channel = sys.argv[1:4]
img = next((i for i in json.load(sys.stdin) if i.get("version") == version), None)
problems = []
if img is None:
    problems.append("not listed")
else:
    if img.get("sha256") != sha:
        problems.append("stored sha256 differs from the local image")
    if img.get("channel") != channel:
        problems.append("channel is " + str(img.get("channel")))
    if not img.get("elf"):
        problems.append("no ELF stored")
if problems:
    sys.exit("verify failed for " + version + ": " + "; ".join(problems))
print("verified on Ember: " + version + ", channel " + channel + ", sha256 and ELF match")
' "$VERSION" "$SHA" "$CHANNEL"

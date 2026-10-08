#!/bin/bash
set -eu

usage() {
    cat <<'EOF'
Usage: tools/build_release.sh [--dir DIR] [--version X.Y.Z] [--ota-test crash_boot|no_checkin|no_render|none]

Secret-free build from sdkconfig.defaults only (never sdkconfig.secrets), into its own
directory and sdkconfig (default build-release). Fails when the dev seed is on or the
image holds a value from sdkconfig.secrets or the Ember master token.

  --version X.Y.Z   override PROJECT_VER (OTA test images: the next versions up)
  --ota-test FAULT  CONFIG_CINDER_OTA_TEST image (2 min rollback timer, ota_fault op);
                    FAULT applies while the image is pending verification
EOF
}

FW="$(cd "$(dirname "$0")/.." && pwd)"
DIR=build-release
VERSION=""
TEST=""
while [ $# -gt 0 ]; do
    case "$1" in
    --dir) DIR="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --ota-test) TEST="$2"; shift 2 ;;
    -h | --help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
    esac
done
if [ -n "$VERSION" ] && ! [[ "$VERSION" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    echo "bad --version: $VERSION" >&2
    exit 2
fi
case "$TEST" in "" | crash_boot | no_checkin | no_render | none) ;; *) usage >&2; exit 2 ;; esac

if [ -z "${IDF_PATH:-}" ] || [ -z "${IDF_PYTHON_ENV_PATH:-}" ]; then
    echo "ESP-IDF is not active: run . ~/.espressif/tools/activate_idf_v5.5.5.sh first" >&2
    exit 1
fi
idf() { "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "$@"; }
cd "$FW"
mkdir -p "$DIR"
DEFAULTS="sdkconfig.defaults"
rm -f "$DIR/sdkconfig" "$DIR/sdkconfig.ota_test"
if [ -n "$TEST" ]; then
    FAULT="$TEST"
    [ "$FAULT" = none ] && FAULT=""
    printf 'CONFIG_CINDER_OTA_TEST=y\nCONFIG_CINDER_OTA_TEST_FAULT="%s"\n' "$FAULT" >"$DIR/sdkconfig.ota_test"
    DEFAULTS="$DEFAULTS;$DIR/sdkconfig.ota_test"
fi

idf -B "$DIR" -D SDKCONFIG="$DIR/sdkconfig" -D SDKCONFIG_DEFAULTS="$DEFAULTS" -D CINDER_SEED_SECRETS=0 \
    -D CINDER_VERSION="$VERSION" build >"$DIR/build.log" 2>&1 || {
    tail -40 "$DIR/build.log" >&2
    exit 1
}

if grep -q '^CONFIG_CINDER_DEV_SEED=y' "$DIR/sdkconfig"; then
    echo "$DIR/sdkconfig has CONFIG_CINDER_DEV_SEED=y: not a release build" >&2
    exit 1
fi
python3 "$FW/tools/secret_scan.py" "$DIR/cinder.bin" "$DIR/cinder.elf"
{
    git -C "$FW" rev-parse HEAD
    git -C "$FW" status --porcelain -- . | grep -q . && echo dirty || echo clean
    [ -n "$TEST" ] && echo ota_test || echo release
} >"$DIR/build.commit"

python3 - "$DIR/cinder.bin" <<'EOF'
import hashlib, sys
b = open(sys.argv[1], "rb").read()
s = lambda o: b[o:o + 32].split(b"\0")[0].decode()
print(f"{sys.argv[1]}: cinder {s(48)} build {b[176:180].hex()} IDF {s(144)}, {len(b)} B, sha256 {hashlib.sha256(b).hexdigest()}")
EOF

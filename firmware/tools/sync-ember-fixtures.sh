#!/bin/bash
set -euo pipefail

REPO="tarakanof/ember"
SRC="cmd/ember/testdata/devices"
D="$(cd "$(dirname "$0")" && pwd)"
DEST="$D/../test/host/fixtures/ember"

if [ $# -ne 1 ] || [ -z "$1" ]; then
    echo "usage: $0 <ember tag or commit>" >&2
    exit 2
fi
REF="$1"
command -v gh >/dev/null || { echo "sync-ember-fixtures: gh not found" >&2; exit 1; }

SHA="$(gh api "repos/$REPO/commits/$REF" --jq .sha)"
case "$SHA" in
    [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]*) ;;
    *) echo "sync-ember-fixtures: cannot resolve $REF in $REPO" >&2; exit 1 ;;
esac

TMP="$(mktemp -d "${TMPDIR:-/tmp}/ember_fixtures.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

gh api "repos/$REPO/contents/$SRC?ref=$SHA" --jq '.[] | select(.type == "file") | .name' >"$TMP/names"
N=0
while IFS= read -r name; do
    case "$name" in
        *.json) ;;
        *) continue ;;
    esac
    case "$name" in
        */* | .*) echo "sync-ember-fixtures: odd file name $name" >&2; exit 1 ;;
    esac
    gh api -H "Accept: application/vnd.github.raw" "repos/$REPO/contents/$SRC/$name?ref=$SHA" >"$TMP/$name.part"
    mv "$TMP/$name.part" "$TMP/$name"
    N=$((N + 1))
done <"$TMP/names"
rm -f "$TMP/names"
if [ "$N" -eq 0 ]; then
    echo "sync-ember-fixtures: no fixtures in $REPO/$SRC at $SHA" >&2
    exit 1
fi

printf 'repo: %s\npath: %s\nref: %s\ncommit: %s\n' "$REPO" "$SRC" "$REF" "$SHA" >"$TMP/SOURCE"
mkdir -p "$DEST"
rm -f "$DEST"/*.json "$DEST/SOURCE"
cp "$TMP"/* "$DEST/"
echo "sync-ember-fixtures: $N fixtures from $REPO@$SHA ($REF) into $DEST"

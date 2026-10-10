#!/bin/bash
set -euo pipefail

REPO="tarakanof/ember"
SRC="cmd/ember/testdata/devices"
D="$(cd "$(dirname "$0")" && pwd)"
DEST="$D/../test/host/fixtures/ember"

if [ $# -ne 1 ] || [ -z "$1" ]; then
    echo "usage: $0 <ember v* tag or 7-40 lowercase hex commit SHA>" >&2
    exit 2
fi
REF="$1"
command -v gh >/dev/null || { echo "sync-ember-fixtures: gh not found" >&2; exit 1; }

case "$REF" in
    v[0-9]*)
        if ! printf '%s' "$REF" | grep -Eq '^v[0-9][0-9A-Za-z.+-]*$'; then
            echo "sync-ember-fixtures: bad tag $REF" >&2
            exit 2
        fi
        gh api "repos/$REPO/git/ref/tags/$REF" --jq .ref >/dev/null || {
            echo "sync-ember-fixtures: no tag $REF in $REPO" >&2
            exit 1
        }
        ;;
    *)
        if ! printf '%s' "$REF" | grep -Eq '^[0-9a-f]{7,40}$'; then
            echo "sync-ember-fixtures: $REF is neither a v* tag nor a commit SHA" >&2
            exit 2
        fi
        ;;
esac

SHA="$(gh api "repos/$REPO/commits/$REF" --jq .sha)"
if ! printf '%s' "$SHA" | grep -Eq '^[0-9a-f]{40}$'; then
    echo "sync-ember-fixtures: cannot resolve $REF in $REPO" >&2
    exit 1
fi
case "$REF" in
    v*) ;;
    *)
        case "$SHA" in
            "$REF"*) ;;
            *) echo "sync-ember-fixtures: $REF resolved to $SHA, not a commit SHA" >&2; exit 1 ;;
        esac
        ;;
esac

TMP="$(mktemp -d "${TMPDIR:-/tmp}/ember_fixtures.XXXXXX")"
NEW="$DEST.new.$$"
OLD="$DEST.old.$$"
cleanup() {
    rm -rf "$TMP" "$NEW"
    if [ ! -e "$DEST" ] && [ -e "$OLD" ]; then
        mv "$OLD" "$DEST" || echo "sync-ember-fixtures: previous fixtures kept in $OLD" >&2
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

gh api "repos/$REPO/contents/$SRC?ref=$SHA" --jq '.[] | select(.type == "file") | .name' >"$TMP/names"
N=0
while IFS= read -r name; do
    if ! printf '%s' "$name" | grep -Eq '^[a-z0-9_]+\.json$'; then
        case "$name" in
            *.json) echo "sync-ember-fixtures: odd file name $name" >&2; exit 1 ;;
            *) continue ;;
        esac
    fi
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
rm -rf "$NEW" "$OLD"
mkdir -p "$(dirname "$DEST")" "$NEW"
cp "$TMP"/* "$NEW/"
if [ -e "$DEST" ]; then mv "$DEST" "$OLD"; fi
if ! mv "$NEW" "$DEST"; then
    if [ -e "$OLD" ] && { [ -e "$DEST" ] || ! mv "$OLD" "$DEST"; }; then
        echo "sync-ember-fixtures: cannot move $NEW to $DEST; previous fixtures kept in $OLD" >&2
    else
        echo "sync-ember-fixtures: cannot move $NEW to $DEST" >&2
    fi
    OLD=
    exit 1
fi
rm -rf "$OLD"
echo "sync-ember-fixtures: $N fixtures from $REPO@$SHA ($REF) into $DEST"

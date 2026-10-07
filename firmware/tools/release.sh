#!/bin/bash
set -euo pipefail

if [ $# -ne 1 ] || ! [[ "$1" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    echo "Usage: tools/release.sh X.Y.Z  (PROJECT_VER in firmware/CMakeLists.txt must already be X.Y.Z)" >&2
    echo "Tags and pushes vX.Y.Z, waits for the release workflow, checks that the GitHub Release assets came" >&2
    echo "from that run at this commit, are immutable, match its artifact and SHA256SUMS and carry its build" >&2
    echo "provenance attestation, then uploads those exact bytes to Ember (publish.sh --release --no-build)." >&2
    echo "Never builds locally; a re-run skips what is done." >&2
    exit 2
fi
V="$1"
TAG="v$V"
WORKFLOW=release.yml
REPO=tarakanof/cinder
API="repos/$REPO"
export GH_REPO="$REPO"
BOT="github-actions[bot]"
FW="$(cd "$(dirname "$0")/.." && pwd)"
cd "$FW/.."

command -v gh >/dev/null || { echo "gh (GitHub CLI) not found" >&2; exit 1; }
HOST_RE='^(git@|ssh://git@|https://)github\.com(-personal)?[:/]'
for url in "$(git remote get-url origin)" "$(git remote get-url --push origin)"; do
    [[ "$url" =~ $HOST_RE"$REPO"(\.git)?$ ]] || { echo "git remote origin ($url) is not github.com/$REPO" >&2; exit 1; }
done
[ -z "${EMBER_ENV_FILE:-}" ] || echo "note: EMBER_ENV_FILE is set: publishing to the server in $EMBER_ENV_FILE" >&2
git fetch -q origin main
REFS="$(git ls-remote origin "refs/tags/$TAG" "refs/tags/$TAG^{}")" || { echo "cannot list tags on origin" >&2; exit 1; }
REL_SHA="$(printf '%s\n' "$REFS" | awk -v t="refs/tags/$TAG" '$2 == t "^{}" { p = $1 } $2 == t { l = $1 } END { print (p != "" ? p : l) }')"
if [ -n "$REL_SHA" ]; then
    echo "origin has $TAG at $REL_SHA: resuming"
    git fetch -q origin "refs/tags/$TAG:refs/tags/$TAG" 2>/dev/null || git fetch -q origin "$REL_SHA"
    git cat-file -e "$REL_SHA:.github/workflows/release.yml" 2>/dev/null ||
        { echo "$TAG predates the release workflow: release the next version instead" >&2; exit 1; }
    git merge-base --is-ancestor "$REL_SHA" origin/main || { echo "$TAG ($REL_SHA) is not on origin/main" >&2; exit 1; }
    git show "$REL_SHA:firmware/CMakeLists.txt" | grep -q "set(PROJECT_VER \"$V\")" ||
        { echo "PROJECT_VER at $TAG is not $V" >&2; exit 1; }
else
    if ! git diff --quiet || ! git diff --cached --quiet; then echo "working tree not clean" >&2; exit 1; fi
    [ "$(git rev-parse --abbrev-ref HEAD)" = main ] || { echo "not on main" >&2; exit 1; }
    REL_SHA="$(git rev-parse HEAD)"
    [ "$REL_SHA" = "$(git rev-parse origin/main)" ] || { echo "main is not in sync with origin/main" >&2; exit 1; }
    grep -q "set(PROJECT_VER \"$V\")" firmware/CMakeLists.txt || { echo "PROJECT_VER is not $V" >&2; exit 1; }
    TAG_SHA="$(git rev-parse -q --verify "refs/tags/$TAG^{commit}" || true)"
    [ -z "$TAG_SHA" ] || [ "$TAG_SHA" = "$REL_SHA" ] || { echo "local tag $TAG is on another commit" >&2; exit 1; }
    [ -n "$TAG_SHA" ] || git tag -a "$TAG" -m "cinder $V"
    git push origin "$TAG"
fi

run_field() {
    gh run list --workflow "$WORKFLOW" --branch "$TAG" --event push --limit 20 \
        --json databaseId,headSha,status,conclusion,attempt \
        --jq "[.[] | select(.headSha == \"$REL_SHA\")][0] | .$1 // empty"
}

RUN=""
for _ in $(seq 60); do
    RUN="$(run_field databaseId)"
    [ -n "$RUN" ] && break
    sleep 5
done
[ -n "$RUN" ] || { echo "no $WORKFLOW run for $TAG at $REL_SHA after 5 min" >&2; exit 1; }
if [ "$(run_field status)" = completed ] && [ "$(run_field conclusion)" != success ]; then
    ATTEMPT="$(run_field attempt)"
    echo "release run $RUN ended $(run_field conclusion): re-running its failed jobs"
    gh run rerun "$RUN" --failed
    for _ in $(seq 60); do
        [ "$(run_field attempt)" != "$ATTEMPT" ] && break
        sleep 5
    done
    [ "$(run_field attempt)" != "$ATTEMPT" ] || { echo "re-run of $RUN did not start within 5 min" >&2; exit 1; }
fi
if [ "$(run_field status)" != completed ]; then
    echo "waiting for release run $RUN"
    gh run watch "$RUN" --exit-status --compact --interval 15 >/dev/null || true
fi
[ "$(run_field conclusion)" = success ] || {
    echo "release run $RUN did not succeed: gh run view $RUN --log-failed" >&2
    exit 1
}

DIST="$(mktemp -d "${TMPDIR:-/tmp}/cinder-release.XXXXXX")"
trap 'rm -rf "$DIST"' EXIT
gh api "$API/actions/runs/$RUN" >"$DIST/run.json"
gh api "$API/releases/tags/$TAG" >"$DIST/release.json"
python3 - "$DIST" "$TAG" "$REL_SHA" "$BOT" <<'EOF'
import json, os, sys
d, tag, sha, bot = sys.argv[1:5]
run = json.load(open(os.path.join(d, "run.json")))
rel = json.load(open(os.path.join(d, "release.json")))
problems = []
for key, want in (("path", ".github/workflows/release.yml"), ("event", "push"), ("head_branch", tag),
                  ("head_sha", sha), ("status", "completed"), ("conclusion", "success")):
    if run.get(key) != want:
        problems.append(f"run {key} is {run.get(key)!r}, want {want!r}")
if rel.get("immutable") is not True:
    problems.append("release is not immutable")
if rel.get("tag_name") != tag or rel.get("draft") or rel.get("prerelease"):
    problems.append("release is not the published, non-prerelease " + tag)
if (rel.get("author") or {}).get("login") != bot:
    problems.append("release author is not " + bot)
if not run["created_at"] <= (rel.get("published_at") or "") <= (rel.get("updated_at") or "") <= run["updated_at"]:
    problems.append("release was published or edited outside run " + str(run.get("id")))
assets = rel.get("assets") or []
names = sorted(a.get("name") for a in assets)
if names != ["SHA256SUMS", "cinder.bin", "cinder.elf"]:
    problems.append("release assets are " + ", ".join(map(str, names)) + ", want exactly cinder.bin, cinder.elf, SHA256SUMS")
for a in assets:
    if (a.get("uploader") or {}).get("login") != bot or a.get("state") != "uploaded":
        problems.append(a.get("name", "?") + " was not uploaded by " + bot)
    if not run["created_at"] <= (a.get("created_at") or "") <= (a.get("updated_at") or "") <= run["updated_at"]:
        problems.append(a.get("name", "?") + " was uploaded or changed outside run " + str(run.get("id")))
    if not str(a.get("digest", "")).startswith("sha256:"):
        problems.append(a.get("name", "?") + " has no sha256 digest")
if problems:
    sys.exit("refusing GitHub Release " + tag + ":\n  " + "\n  ".join(problems))
with open(os.path.join(d, "digests"), "w") as f:
    for a in assets:
        f.write(a["digest"][len("sha256:"):] + "  " + a["name"] + "\n")
print("GitHub Release " + tag + ": assets uploaded by run " + str(run["id"]) + " of release.yml at " + sha)
EOF

mkdir "$DIST/release" "$DIST/run"
gh release download "$TAG" -D "$DIST/release" -p cinder.bin -p cinder.elf -p SHA256SUMS
gh run download "$RUN" -n release -D "$DIST/run" || {
    echo "cannot download artifact 'release' of run $RUN (expired after 90 days?): not cross-checked, refusing" >&2
    exit 1
}
for f in cinder.bin cinder.elf SHA256SUMS; do
    cmp -s "$DIST/release/$f" "$DIST/run/$f" || { echo "release asset $f differs from run $RUN's artifact" >&2; exit 1; }
done
python3 - "$DIST/release" "$V" "$DIST/digests" <<'EOF'
import hashlib, os, re, sys
d, version, digests = sys.argv[1:4]
for line in open(digests):
    digest, name = line.split()
    if hashlib.sha256(open(os.path.join(d, name), "rb").read()).hexdigest() != digest:
        sys.exit(name + ": SHA-256 does not match GitHub's asset digest")
rows = [line.split() for line in open(os.path.join(d, "SHA256SUMS")) if line.strip()]
want = {r[1].lstrip("*"): r[0] for r in rows if len(r) == 2 and re.fullmatch("[0-9a-f]{64}", r[0])}
if len(rows) != 2 or sorted(want) != ["cinder.bin", "cinder.elf"]:
    sys.exit("SHA256SUMS must hold exactly two rows: cinder.bin and cinder.elf with SHA-256 digests")
for name, digest in want.items():
    if hashlib.sha256(open(os.path.join(d, name), "rb").read()).hexdigest() != digest:
        sys.exit(name + ": SHA-256 does not match SHA256SUMS")
built = open(os.path.join(d, "cinder.bin"), "rb").read()[48:80].split(b"\0")[0].decode()
if built != version:
    sys.exit("cinder.bin is version " + built + ", want " + version)
print("SHA256SUMS ok and equal to the run artifact: cinder.bin " + want["cinder.bin"])
EOF
for f in cinder.bin cinder.elf SHA256SUMS; do
    gh attestation verify "$DIST/release/$f" --repo "$REPO" \
        --cert-identity "https://github.com/$REPO/.github/workflows/$WORKFLOW@refs/tags/$TAG" \
        --source-ref "refs/tags/$TAG" --source-digest "$REL_SHA" --deny-self-hosted-runners >/dev/null ||
        { echo "$f: no valid build provenance attestation from $WORKFLOW at $TAG ($REL_SHA), refusing" >&2; exit 1; }
done
echo "attestations ok: cinder.bin, cinder.elf and SHA256SUMS were built by $WORKFLOW at $TAG ($REL_SHA)"

"$FW/tools/publish.sh" --release --no-build --dir "$DIST/release"
echo "released $V ($REL_SHA): GitHub Release $TAG, run $RUN's artifact and Ember channel release hold the same bytes"

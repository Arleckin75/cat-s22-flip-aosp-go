#!/usr/bin/env bash
# Maintainer check: prove patches/ reproduces a reference tree exactly.
#
# For each project in patches/SERIES, check out the pinned revision in a
# temporary worktree, `git am` the series, and compare the resulting git tree
# hash with the reference tree's current state (HEAD plus uncommitted edits to
# tracked files). The reference tree is never modified.
#
# Usage: tools/verify_patches.sh <reference-source-root>
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
reference="$(cd "$1" && pwd)"
manifest="$repo/manifest/cat-s22-aosp16.xml"
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
export GIT_COMMITTER_NAME="CAT S22 build" GIT_COMMITTER_EMAIL="builder@localhost"
export GIT_AUTHOR_DATE="2000-01-01T00:00:00Z" GIT_COMMITTER_DATE="2000-01-01T00:00:00Z"
failed=0

while read -r project count; do
    [[ -n "$project" ]] || continue
    pinned=$(python3 - "$manifest" "$project" <<'PY'
import sys, xml.etree.ElementTree as ET
for p in ET.parse(sys.argv[1]).getroot().findall('project'):
    if (p.get('path') or p.get('name')) == sys.argv[2]:
        print(p.get('revision'))
PY
)
    directory="$reference/$project"
    # Expected: tree of the reference working state, built in a private index.
    index="$scratch/index"
    cp "$(git -C "$directory" rev-parse --absolute-git-dir)/index" "$index"
    expected=$(GIT_INDEX_FILE="$index" git -C "$directory" add -u >/dev/null &&
               GIT_INDEX_FILE="$index" git -C "$directory" write-tree)
    rm -f "$index"

    worktree="$scratch/wt"
    git -C "$directory" worktree add --quiet --detach "$worktree" "$pinned"
    if git -C "$worktree" am --quiet --keep-cr "$repo/patches/$project"/*.patch >"$scratch/am.log" 2>&1; then
        actual=$(git -C "$worktree" rev-parse 'HEAD^{tree}')
    else
        actual="am-failed"
        sed 's/^/    /' "$scratch/am.log" | tail -20
        git -C "$worktree" am --abort >/dev/null 2>&1 || true
    fi
    git -C "$directory" worktree remove --force "$worktree"
    git -C "$directory" worktree prune
    if [[ "$actual" == "$expected" ]]; then
        echo "OK       $project ($count)"
    else
        echo "MISMATCH $project ($count): expected $expected, got $actual"
        failed=1
    fi
done < "$repo/patches/SERIES"
exit "$failed"

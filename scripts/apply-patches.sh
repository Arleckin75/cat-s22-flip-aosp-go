#!/usr/bin/env bash
# Replay patches/ onto a freshly synced tree with git am.
#
# Each project must still be at its pinned manifest revision; a project that
# was already patched by this script is skipped. Stops at the first conflict.
set -euo pipefail
source "$(dirname "$0")/env.sh"

export GIT_COMMITTER_NAME="CAT S22 build" GIT_COMMITTER_EMAIL="builder@localhost"
state="$CAT_S22_SOURCE/.cat-s22-patches"
mkdir -p "$state"

while read -r project count; do
    [[ -n "$project" ]] || continue
    stamp="$state/${project//\//_}.done"
    if [[ -f "$stamp" ]]; then
        echo "already applied: $project"
        continue
    fi
    directory="$CAT_S22_SOURCE/$project"
    pinned=$(python3 - "$CAT_S22_REPO/manifest/cat-s22-aosp16.xml" "$project" <<'PY'
import sys, xml.etree.ElementTree as ET
for p in ET.parse(sys.argv[1]).getroot().findall('project'):
    if (p.get('path') or p.get('name')) == sys.argv[2]:
        print(p.get('revision'))
PY
)
    head=$(git -C "$directory" rev-parse HEAD)
    if [[ "$head" != "$pinned" ]]; then
        echo "$project is at $head, expected pinned $pinned; re-sync it first." >&2
        exit 1
    fi
    if [[ -n "$(git -C "$directory" status --porcelain --untracked-files=no)" ]]; then
        echo "$project has local changes; re-sync it first." >&2
        exit 1
    fi
    patches=("$CAT_S22_REPO/patches/$project"/*.patch)
    if [[ ${#patches[@]} -ne $count ]]; then
        echo "SERIES expects $count patches for $project, found ${#patches[@]}." >&2
        exit 1
    fi
    echo "applying $count patch(es): $project"
    git -C "$directory" am --quiet --keep-cr "${patches[@]}"
    git -C "$directory" rev-parse HEAD > "$stamp"
done < "$CAT_S22_REPO/patches/SERIES"
echo "All patches applied."

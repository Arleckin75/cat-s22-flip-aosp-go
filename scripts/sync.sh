#!/usr/bin/env bash
# Check out the exact AOSP 16 + TrebleDroid source tree this port was built and tested on.
set -euo pipefail
source "$(dirname "$0")/env.sh"

manifest_name=cat-s22-aosp16.xml
mkdir -p "$CAT_S22_SOURCE"
cd "$CAT_S22_SOURCE"

repo init --depth=1 -u https://android.googlesource.com/platform/manifest -b android-16.0.0_r2 --git-lfs
# The pinned manifest is complete (it already contains the TrebleDroid
# projects and all exclusions); extra local manifests would conflict with it.
if [[ -d .repo/local_manifests ]] && compgen -G '.repo/local_manifests/*.xml' > /dev/null; then
    echo "Remove .repo/local_manifests first; the pinned manifest replaces it." >&2
    exit 1
fi
cp "$CAT_S22_REPO/manifest/$manifest_name" ".repo/manifests/$manifest_name"
repo init -m "$manifest_name"
repo sync -c --no-clone-bundle --no-tags --fail-fast -j"${CAT_S22_SYNC_JOBS:-4}"
echo "Sources synced at the pinned revisions: $CAT_S22_SOURCE"

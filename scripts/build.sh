#!/usr/bin/env bash
# Build TrebleApp and the system image. Run after sync, apply-patches and
# install-device (or use scripts/run.sh).
set -eo pipefail
source "$(dirname "$0")/env.sh"
export USE_CCACHE="${USE_CCACHE:-1}" CCACHE_COMPRESS=1

cd "$CAT_S22_SOURCE/treble_app"
./build.sh release
test -s TrebleApp.apk

cd "$CAT_S22_SOURCE"
source build/envsetup.sh
lunch "$CAT_S22_LUNCH"
# Host tools for package.py and check-vintf.py are not dependencies of systemimage.
m systemimage lpmake lpdump lpunpack simg2img checkvintf -j"$CAT_S22_JOBS"
test -s "$CAT_S22_PRODUCT_OUT/system.img"
sha256sum "$CAT_S22_PRODUCT_OUT/system.img"

# Shared settings for the build scripts. Source it; override any value by
# exporting it before running a script.

CAT_S22_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CAT_S22_REPO

# Android source tree (about 150 GB after a build, plus ~70 GB for .repo).
export CAT_S22_SOURCE="${CAT_S22_SOURCE:-$HOME/aosp16}"
# Stock firmware work directory (OTA downloads, extracted V29/V30 images).
export CAT_S22_STOCK="${CAT_S22_STOCK:-$HOME/cat-s22-stock}"
# Where the flashable set is written.
export CAT_S22_OUT="${CAT_S22_OUT:-$CAT_S22_SOURCE/out/cat-s22-package}"
# Android SDK used by treble_app's Gradle build (platform 33, build-tools 33).
export ANDROID_HOME="${ANDROID_HOME:-$HOME/android-sdk}"
# Parallel jobs for the Android build.
export CAT_S22_JOBS="${CAT_S22_JOBS:-$(nproc)}"

# Hardware helper sources and IMS APK. That repository has no license file,
# so its contents are fetched at build time instead of being copied here.
export CAT_S22_UPSTREAM_URL="https://github.com/ajimsjames/cat-s22-flip-android15-fixes.git"
export CAT_S22_UPSTREAM_REV="ef2fb733eff486c5825ff4017c2b9194320539f2"
export CAT_S22_UPSTREAM="${CAT_S22_UPSTREAM:-$CAT_S22_STOCK/cat-s22-flip-android15-fixes}"

export CAT_S22_LUNCH="aosp_cat_s22-bp2a-userdebug"
export CAT_S22_PRODUCT_OUT="$CAT_S22_SOURCE/out/target/product/tdgsi_a64_ab"

#!/usr/bin/env bash
# Download the official CAT S22 Flip OTA packages and rebuild stock V30 images.
#
# V29 is a full OTA; V30 is an incremental OTA applied on top of it with
# imgpatchtools. Every source and target hash from the OTA is verified.
# The resulting images are proprietary and are never redistributed by this
# project; each builder produces their own copy.
set -euo pipefail
source "$(dirname "$0")/env.sh"

mkdir -p "$CAT_S22_STOCK"
cd "$CAT_S22_STOCK"

curl --fail --location --retry 3 --continue-at - -o v29.zip \
  https://ota.googlezip.net/packages/ota-api/package/8e554ecf6fdc8c1963d45068e956c7b2a82a6d96.zip
curl --fail --location --retry 3 --continue-at - -o v30.zip \
  https://ota.googlezip.net/packages/ota-api/package/9a3aee53e9065364e1ddd01d373c0ba9229c27ed.zip
printf '%s\n' \
  'c22aff2c972e04c757c934cd6689182b398f91e96da4f3a2ec4ef7b83ff0b18d  v29.zip' \
  '2d40753316e9def49d4aca70ec289480be396a28032703097efdf64e127743fb  v30.zip' | sha256sum -c -

if [[ ! -f v29/images.json ]]; then
    rm -rf v29
    python3 "$CAT_S22_REPO/scripts/stock/extract_full_ota.py" v29.zip v29
fi

if [[ ! -x imgpatchtools/bin/BlockImageUpdate ]]; then
    rm -rf imgpatchtools
    git clone https://github.com/erfanoabdi/imgpatchtools.git imgpatchtools
    git -C imgpatchtools checkout --detach 1071fecb34d2da535211d6c840c138b48ad57402
    # Parallel make fails on this project's dependency ordering.
    make -C imgpatchtools -j1
fi

if [[ ! -f v30/images.json ]]; then
    python3 "$CAT_S22_REPO/scripts/stock/reconstruct_v30.py" "$CAT_S22_STOCK"
fi
echo "Stock V30 images ready: $CAT_S22_STOCK/v30"

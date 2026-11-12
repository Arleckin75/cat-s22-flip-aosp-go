#!/usr/bin/env bash
# Entry point. Runs the given steps in order, or every step when none is given:
#
#   scripts/run.sh                      # stock sync patch device build package vintf
#   scripts/run.sh device build package # rebuild after changing device/cat/s22
#
# Settings come from scripts/env.sh and may be overridden by exporting them.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
source "$here/env.sh"

steps=("$@")
[[ ${#steps[@]} -gt 0 ]] || steps=(stock sync patch device build package vintf)
for step in "${steps[@]}"; do
    echo "=== $step"
    case "$step" in
        stock)   bash "$here/prepare-stock.sh" ;;
        sync)    bash "$here/sync.sh" ;;
        patch)   bash "$here/apply-patches.sh" ;;
        device)  python3 "$here/install-device.py" ;;
        build)   bash "$here/build.sh" ;;
        package) python3 "$here/package.py" ;;
        vintf)   python3 "$here/check-vintf.py" ;;
        *) echo "Unknown step: $step" >&2; exit 2 ;;
    esac
done
echo "=== done: ${steps[*]}"

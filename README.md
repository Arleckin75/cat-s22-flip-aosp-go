# AOSP 16 Go for the CAT S22 Flip

A reproducible build of AOSP 16 (`android-16.0.0_r2`, `BP2A.250605.031.A3`)
with TrebleDroid and the Android Go profile for the CAT S22 Flip. It runs on
the phone's own stock V30 kernel and vendor partition. Only `system` is built
from source.

> **Experimental.** This is a userdebug development build for people who can
> recover a phone over fastboot. Flashing requires an unlocked bootloader and
> erases all data. Read [docs/FLASHING.md](docs/FLASHING.md) before you start.

## What works

The table shows what was checked on a real phone (Android 16 boot, SELinux
enforcing):

| Area | State |
| --- | --- |
| Boot, setup, launcher | works (Android Go launcher) |
| Touchscreen, number keys | works |
| Main screen off/on with the lid | works |
| Outer (cover) screen | works: clock, date, status, missed calls and SMS; wakes on new events; settings page in Settings → Display. Incoming-call screen not tested on a real call yet |
| Small-screen UI | no on-screen navigation bar (hardware keys), AOSP boot animation, single-line lock clock, 4×4 home grid |
| Camera | stock CAT camera app (extracted from your V30): photo, video, front camera work |
| Browser (WebView shell) | works |
| Wi-Fi | connects, internet and DNS work |
| Bluetooth | turns on; pairing not checked |
| IMS service | starts and binds to telephony |
| Calls, SMS, mobile data, VoLTE/VoWiFi | **not tested** (no SIM yet) |
| Keypad backlight | works: on while the lid is open and the screen is on |
| Memory | Android Go profile (see [docs/HARDWARE.md](docs/HARDWARE.md)) |
| Battery life | not measured |
| Google apps | not included; [microG](docs/MICROG.md) can be installed for push notifications |

Details and known issues are in [docs/HARDWARE.md](docs/HARDWARE.md).

## How it is put together

| Part | Source |
| --- | --- |
| Android tree | [manifest/cat-s22-aosp16.xml](manifest/cat-s22-aosp16.xml): AOSP `android-16.0.0_r2` plus TrebleDroid, every project pinned to an exact commit |
| Source changes | [patches/](patches): 208 patches over 30 projects, applied with `git am`. These are the TrebleDroid patches from [ponces/treble_aosp](https://github.com/ponces/treble_aosp) (Apache-2.0) plus five changes for this device |
| Device configuration | [device/cat/s22](device/cat/s22): product, init, SELinux, cover-screen service and settings app |
| Hardware helpers and IMS APK | fetched at build time from [ajimsjames/cat-s22-flip-android15-fixes](https://github.com/ajimsjames/cat-s22-flip-android15-fixes) at a pinned commit. That repository has no license, so its files are not copied here |
| Stock V30 kernel, vendor, vbmeta | rebuilt by each builder from CAT's official OTA packages; never redistributed |

## Quick start

You need a Linux x86-64 machine. See [docs/BUILDING.md](docs/BUILDING.md) for
requirements.

```bash
git clone https://github.com/Arleckin75/cat-s22-flip-aosp-go.git
cd cat-s22-flip-aosp-go
scripts/run.sh
```

`run.sh` runs these steps in order: `stock` (download and rebuild V30),
`sync`, `patch`, `device`, `build`, `package`, `vintf`. The flashable files
end up in `$CAT_S22_OUT`, by default `~/aosp16/out/cat-s22-package`.

## Repository layout

```
manifest/   pinned repo manifest
patches/    per-project patch series (SERIES lists projects and counts)
device/     device/cat/s22, copied into the tree by install-device.py
config/     stock V30 kernel config, used for the offline VINTF check
scripts/    build steps (run.sh is the entry point)
tools/      maintainer tools (export_patches.py)
docs/       building, flashing, hardware notes, maintenance
```

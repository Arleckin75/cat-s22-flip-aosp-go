# Building

## Requirements

These were checked on the reference build host:

- Linux x86-64. The reference host is Ubuntu 22.04 in a Proxmox LXC.
- RAM: 24 GiB plus 4 GiB swap worked. Soong's graph analysis peaks near 20 GiB.
  Patches cap Soong's Go heap at 12 GiB (`GOMEMLIMIT`), see
  `patches/build/soong`.
- CPU: 8 cores or more. A full `scripts/run.sh` from a fresh clone (download,
  sync, build, package) took about 4 hours on 10 cores without ccache.
- Disk: about 190 GB for the tree after a build, plus about 7 GB for the
  stock work directory.
- Packages: the standard AOSP build dependencies, plus `repo`, `git-lfs`,
  `brotli`, `e2fsprogs` (for `debugfs`), `curl`, `python3` and OpenJDK 17.
  Pillow is only needed for the optional `scripts/make-bootanimation.py`.
- Android SDK at `$ANDROID_HOME` with `platforms;android-33` and
  `build-tools;33.0.2`. treble_app's Gradle build needs them.

## Settings

All paths come from [scripts/env.sh](../scripts/env.sh). To change one, export
it before running a script:

| Variable | Default | Meaning |
| --- | --- | --- |
| `CAT_S22_SOURCE` | `~/aosp16` | Android tree |
| `CAT_S22_STOCK` | `~/cat-s22-stock` | OTA downloads and rebuilt V30 images |
| `CAT_S22_OUT` | `$CAT_S22_SOURCE/out/cat-s22-package` | flashable output |
| `ANDROID_HOME` | `~/android-sdk` | SDK for treble_app |
| `CAT_S22_JOBS` | `nproc` | build parallelism |
| `CAT_S22_SYNC_JOBS` | `4` | `repo sync` parallelism |
| `CAT_S22_ADB_KEY` | unset | **private debugging only**: an adb public key to trust without a prompt |

## Steps

`scripts/run.sh` runs all steps. You can also name single steps, for example
`scripts/run.sh device build package`.

1. **stock**: downloads the official V29 (full) and V30 (incremental) OTA
   packages from Google's OTA CDN and checks their SHA-256 hashes. It extracts
   V29, then applies the V30 OTA with
   [imgpatchtools](https://github.com/erfanoabdi/imgpatchtools) at a pinned
   commit. Every source hash and every target write is verified against the
   OTA. The result goes to `$CAT_S22_STOCK/v30`.
2. **sync**: runs `repo init` on AOSP `android-16.0.0_r2`, then switches to
   the pinned manifest and runs `repo sync -c`.
3. **patch**: replays `patches/` with `git am`. Each project must be at its
   pinned revision. Finished projects are recorded in
   `$CAT_S22_SOURCE/.cat-s22-patches/`, so a re-run skips them.
4. **device**: copies `device/cat/s22` into the tree and fetches the upstream
   hardware helpers and IMS APK. It also generates the VINTF matrices, the
   stock key layout and the IMS privileged-permission allowlist from your V30
   images.
5. **build**: builds treble_app (Gradle), then `m systemimage` for
   `aosp_cat_s22-bp2a-userdebug`, plus the host tools that `package` and
   `vintf` use (`lpmake`, `lpdump`, `lpunpack`, `simg2img`, `checkvintf`).
6. **package**: builds `boot.img`, `vbmeta-disabled.img` and a sparse
   `super.img` (system plus vendor) from your V30 images. It verifies every
   edit and writes `manifest.json` with hashes.
7. **vintf**: offline `checkvintf --check-compat` against the packaged vendor
   and the real stock kernel config. It must print `COMPATIBLE`.

## Rebuilding after a change

- Changed something in `device/cat/s22`: run
  `scripts/run.sh device build package`.
- Changed a patch: re-sync that project (`repo sync -c <path>`), delete its
  stamp in `.cat-s22-patches/`, then run `scripts/run.sh patch build package`.

## Checking the patch set

`tools/verify_patches.sh <tree>` replays every series in a temporary worktree
and compares the resulting git tree hash with a reference tree. The current
set reproduces the reference tree exactly for all 32 projects.

## Output

`boot.img` and `vbmeta-disabled.img` are byte-for-byte reproducible from the
same stock V30. The `vendor.img` hash changes from run to run: `debugfs`
stamps the edited `fstab.qcom` with the current time, while the content stays
the same.

| File | Flash to |
| --- | --- |
| `vbmeta-disabled.img` | `vbmeta` (bootloader mode) |
| `boot.img` | `boot` (bootloader mode) |
| `super.img` | `super` (fastbootd) |

See [FLASHING.md](FLASHING.md).

## Checking your result

A full run from a fresh clone was compared file by file with the reference
image. `vendor`, the kernel, DTB and ramdisk, and every file in `system` are
the same, except:

- the build dates in the three `build.prop` files;
- `system/lib/libminikin.so`. Its CFI/LTO object code depends on the absolute
  path of the source tree, so it differs when the tree is not at the same
  path. The same tree path gives the same bytes.

`boot.img` may differ only in the 20-byte `id` field of its header; the
bootloader does not check it on an unlocked phone.

# Maintaining the patch set

`patches/` is the exact delta between the pinned manifest and a tree that was
built and booted. It was produced with
[tools/export_patches.py](../tools/export_patches.py).

## Changing a patch

1. Make the change in the tree as a commit in the affected project. You can
   also leave it as an uncommitted edit: the tool exports that as a
   `CAT-S22-local-build-adaptation` patch.
2. Export:

   ```bash
   python3 tools/export_patches.py "$CAT_S22_SOURCE" manifest/cat-s22-aosp16.xml /tmp/patches
   ```

3. Review `git diff --stat`, then replace `patches/` with `/tmp/patches`.

The tool only exports projects whose pinned revision is an ancestor of their
current `HEAD`. To use a different upstream commit, change its `revision` in
the manifest instead of patching it.

## Updating upstream

To move to newer AOSP or TrebleDroid revisions, follow these steps:

1. Sync the newer sources.
2. Apply the upstream treble patch series and resolve its conflicts.
3. Build, flash and test on a phone.
4. Write a new pinned manifest with `repo manifest -r` **before** applying
   patches. Afterwards, `HEAD`s point at local commits that cannot be fetched.
5. Re-export the patches.

## Provenance of the current set

- Manifest: AOSP `android-16.0.0_r2` plus the TrebleDroid projects
  (device/phh/treble and vendor/interfaces `android-16.0`, treble_app,
  vendor/hardware_overlay, QcRilAm, VNDK v28/v29 prebuilts) and the project
  removals from [ponces/treble_aosp](https://github.com/ponces/treble_aosp)
  `android-16.0` (Apache-2.0) at `a908967f`.
- Patches: ponces/treble_aosp `patches/trebledroid` at the same commit
  (203 patches; only the Launcher3 one needed a manual merge; they include the old-kernel
  support this 4.9 phone needs), plus five CAT S22 patches: Soong memory
  limits (build/soong), conditional 64-bit inheritance and `ro.adb.secure=0`
  removed (device/phh/treble), the Launcher3 navigation-button fix, AOSP
  WebView as the default provider (vendor/hardware_overlay), and the
  programmable key broadcast in PhoneWindowManager (frameworks/base).
- `prebuilts/vndk/v29` carries a TrebleDroid patch with three prebuilt VNDK
  libraries built from AOSP sources (Apache-2.0).

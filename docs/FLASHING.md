# Flashing

**This erases the phone.** You need an unlocked bootloader, and you should be
comfortable recovering the phone over fastboot. Keep your rebuilt stock V30
images (`$CAT_S22_STOCK/v30`): they are your way back.

## Tools

- Use platform-tools **fastboot 37 or newer**. Version 34.0.4 crashed while
  formatting on this phone.
- On Windows, bind the Google USB driver as "Android Bootloader Interface" for
  bootloader mode.
- `fastboot boot <image>` does not work on this phone (the bootloader answers
  `dtb not found`). Images have to be flashed.

## Unlock the bootloader (once)

1. Enable *OEM unlocking* in Developer options.
2. Reboot to the bootloader and run `fastboot flashing unlock`.
3. Confirm on the phone. This wipes the phone.

Never lock the bootloader again while a modified vbmeta or boot is installed.

## Install

Before you start, enable *USB debugging* in Developer options so that
`adb reboot bootloader` works, and save everything you need from the phone.

Coming from stock or from any other ROM, **the data wipe in step 3 is
required**. Skipping it keeps the old apps, settings and `/data/adb` (Magisk)
and leads to reboots when apps are installed or removed, and to boot loops.

```bash
# 1. Bootloader mode (fastboot getvar is-userspace -> no)
adb reboot bootloader
fastboot flash vbmeta vbmeta-disabled.img
fastboot flash boot boot.img

# 2. fastbootd (fastboot getvar is-userspace -> yes)
fastboot reboot fastboot
fastboot flash super super.img

# 3. Wipe data, still in fastbootd
fastboot -w
fastboot reboot
```

In fastbootd (and only there) `fastboot -w` just erases userdata, because
fastbootd reports it as `raw`. It then reports an error about the missing
`cache` partition and may exit with an error code; that is harmless, run
`fastboot reboot` anyway. The phone formats `/data` as F2FS on the first boot,
which takes about three minutes. Do not run `fastboot -w` in bootloader mode
(`is-userspace -> no`): that formats EXT4.

Do not rely on **Wipe data/factory reset** in the stock recovery instead: for
one tester it left all apps and `/data/adb` in place.

If the phone already runs with old data, wipe now: `adb reboot fastboot`,
then `fastboot -w` and `fastboot reboot`.

## Update from an earlier build of this port

`boot` and `vbmeta` do not change between builds; flash `super` only. Your
data stays:

```bash
adb reboot fastboot
fastboot flash super super.img
fastboot reboot
```

Going back to an older Android version needs a wipe: add `fastboot -w`
before `fastboot reboot`, as in step 3 above.

## First boot

The first boot shows the CAT logo for a while. If the screen stays black,
check `adb devices`. A userdebug build may already be running with the
display asleep.

## Roll back to stock V30

From `$CAT_S22_STOCK/v30`:

```bash
adb reboot bootloader
fastboot flash boot boot.img
fastboot flash vbmeta vbmeta.img     # original, without disable flags
fastboot reboot fastboot
fastboot flash system system.img
fastboot flash vendor vendor.img
fastboot create-logical-partition product 1221832704
fastboot flash product product.img
fastboot -w                          # wipe data, still in fastbootd
fastboot reboot
```

Stock logical sizes for reference: system `0x3F91D000`, product
`0x48D3B000`, vendor `0x10E4E000`. `fastboot getvar partition-size:<name>`
in fastbootd shows them.

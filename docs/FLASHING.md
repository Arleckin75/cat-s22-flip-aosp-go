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

```bash
# 1. Bootloader mode (fastboot getvar is-userspace -> no)
adb reboot bootloader
fastboot flash vbmeta vbmeta-disabled.img
fastboot flash boot boot.img

# 2. fastbootd (fastboot getvar is-userspace -> yes)
fastboot reboot fastboot
fastboot flash super super.img

# 3. Factory reset from the stock recovery
fastboot reboot recovery
```

In recovery, choose **Wipe data/factory reset**, then **Reboot system now**.
Use the stock recovery and not `fastboot -w`: `/data` must be F2FS, and
fastboot formats it as EXT4.

## Update from an earlier build of this port

`boot` and `vbmeta` do not change between builds; flash `super` only:

```bash
adb reboot fastboot
fastboot flash super super.img
fastboot -w        # only when coming from another ROM, see below
fastboot reboot
```

A newer build of the same ROM keeps your data. Coming from another ROM, or
going back to an older Android version, wipe data. In
fastbootd (and only there), `fastboot -w` just erases userdata, because
fastbootd reports it as `raw`; it then fails on the missing `cache` partition,
which is harmless. The phone formats `/data` as F2FS on the next boot (about
three minutes). Do not run `fastboot -w` in bootloader mode: that formats
EXT4.

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
fastboot reboot recovery             # factory reset, then reboot
```

Stock logical sizes for reference: system `0x3F91D000`, product
`0x48D3B000`, vendor `0x10E4E000`. `fastboot getvar partition-size:<name>`
in fastbootd shows them.

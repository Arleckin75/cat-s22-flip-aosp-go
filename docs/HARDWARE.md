# Hardware notes

Facts found on the device with stock V30 kernel 4.9.227 and vendor V30.

## Platform

- 32-bit ARM userspace with 64-bit binder. This is a Treble `a64` target, not
  `arm64`.
- `ro.product.first_api_level=29`, VNDK 30, vendor declares FCM level 4.
- The stock fstab mounts `product` in first-stage mount. The TrebleDroid
  system image carries product inside `system`, so `package.py` removes exactly that one line from
  the boot ramdisk and from the vendor fstab.
- `/data` is F2FS.

## Lid (hall sensor)

- The input device `hall_sensor` (`/dev/input/eventN`) advertises `SW_LID`
  but **never reports it**. `getevent` stays silent while the lid opens and
  closes, and the switch state always reads 0.
- The working source is `/sys/devices/virtual/switch_hall/hall_switch/state`:
  `1` = open (`hall far` in dmesg), `0` = closed (`hall near`). Every change
  also comes with a kernel uevent.
- The kernel itself blanks and unblanks the main panel with the lid, so no
  userspace help is needed for that.
- The cover app answers a ringing call when the lid opens and, if enabled,
  ends the call when it closes (Settings → Display → Cover screen → Lid;
  answering is on by default, ending off). It listens to the same uevents
  through `UEventObserver`; [cat_cover.te](../device/cat/s22/sepolicy/cat_cover.te)
  lets `platform_app` receive kernel uevents (receive only: app domains may
  never send them). Not yet tested on a real call.

## Outer (cover) screen

- 128×128 panel on `/dev/graphics/fb2` (SPI, 32 bpp ARGB, stride 512),
  backlight `/sys/class/leds/lcd-backlight2/brightness`.
- The mdss driver **ignores brightness 0 while the panel is unblanked**. The
  backlight only goes dark after `FBIOBLANK FB_BLANK_POWERDOWN` (or
  `echo 4 > /sys/class/graphics/fb2/blank`).
- Drawing over SPI needs the CPU awake: the service holds a self-expiring
  wakelock (`cat_s22_cover`, 3 s, renewed on every frame).
- `cat_s22_cover` ([cover/cover.c](../device/cat/s22/cover/cover.c)) is a
  small native service, no polling: it waits on hall uevents and on inotify in
  the companion app's directory. It draws with Roboto glyphs baked in at build
  time ([tools/gen_cover_font.py](../tools/gen_cover_font.py)).
  - Lid closes: clock, date, signal/Wi-Fi/battery, missed calls and unread SMS
    (or the operator) for the configured time.
  - A new missed call or SMS while the lid is closed wakes the screen.
  - A ringing or active call keeps it on with the name or number and a timer.
  - Brightness follows a day/night schedule.
- `CatS22Cover` ([CoverApp](../device/cat/s22/CoverApp), package
  `cats22.cover`, system_ext, persistent) reads call state, the call log, SMS
  and contacts, and writes `config.txt` and `state.txt` to
  `/data/user_de/0/cats22.cover/files`. It also provides the settings page
  under Settings → Display → Cover screen (on/off, duration, brightness,
  night hours, what to show). Its permissions are pre-granted through
  `default-permissions-cat-cover.xml`. Nothing leaves the phone.
- Notes found while testing:
  - CallLogProvider notifies `content://call_log`, not `.../calls`.
  - The SMS provider notifies when it creates a thread, before the message
    row exists, so the app re-counts 0.5 s after the last notification.
- Preview without the panel: `cat_s22_cover --dir DIR --preview out.raw`.
- Turn the service off completely with `setprop persist.cat.s22.outer 0`.

## Keypad

- `cat_s22_keypad` grabs `soc:matrix_keypad` and re-emits debounced events
  through a virtual `Cat_Keypad_Clean` device. It uses AOSP's `Generic.kl`:
  the stock CAT layout names keycodes AOSP does not have (`PTT`, `MICMUTE`),
  so Android rejected it as a whole and fell back to `Generic.kl` anyway.
  Stopping it (`setprop persist.cat.s22.keypad 0`) gives back the raw device.
- Keypad backlight: LED `/sys/class/leds/keyboard_light` (0–255). The lights
  HAL (`lights-service.qti`) has no BUTTONS/KEYBOARD light, so the framework
  cannot drive it. `cat_s22_keylight`
  ([keylight.c](../device/cat/s22/keylight.c)) lights it only while the lid is
  open **and** the main panel is powered. The lid comes from `switch_hall`
  uevents and the panel from mdss `/sys/class/graphics/fb0/show_blank_event`
  (`panel_power_on = N`, sysfs-notified), so there is no polling.
  `--brightness N` sets the level (default 255). Toggle with
  `setprop persist.cat.s22.keylight 0|1`.
- The speaker key (`KEY_MICMUTE`) sends Tab on a short press and Menu on a
  long press (upstream behaviour).
- The programmable key (CAT "PTT", `KEY_NUMERIC_B`) has no Android keycode.
  `cat_s22_keypad` sends a short press as `KEY_MACRO1` and a press longer than
  0.5 s as `KEY_MACRO2`. A frameworks/base patch makes PhoneWindowManager
  broadcast both to the cover app (whatever has focus, also with the screen
  off), which runs the action picked in Settings → Display → Cover screen →
  Programmable key: sound/vibrate/silent cycle (default for a press),
  flashlight (default for press and hold), camera or nothing.

## IMS

- `org.codeaurora.ims` is installed as a platform-signed privileged app. The
  allowlist is generated from the APK's requested permissions; without it,
  `system_server` aborts at boot.
- V30's vendor `seapp_contexts` labels it `vendor_qtelephony`. Android 15 and
  later reject that for an app on `system`, so
  [sepolicy/seapp_contexts](../device/cat/s22/sepolicy/seapp_contexts) maps
  it to `priv_app`. The binder rules toward `rild` are kept narrow.
- Registration, calls and VoLTE are not tested yet.

## Camera

- The stock CAT camera (`TED2Camera.apk`, `com.android.camera`, targetSdk 30)
  is extracted from your V30 `product.img` and installed unchanged
  (`presigned`, `preprocessed`). It replaces Aperture.
- It refuses to start without all-files access. As a preinstalled app it
  gets `MANAGE_EXTERNAL_STORAGE` (`signature|appop|preinstalled`)
  automatically. Camera and microphone are pre-granted through
  `default-permissions-cat-camera.xml`.
- The APK is marked `testOnly`. A manual `adb install` needs `-t`.

## Memory profile

- The phone is an Android Go device: the stock V30 vendor already sets
  `ro.config.low_ram=true`.
- [go_profile.mk](../device/cat/s22/go_profile.mk) adds the rest of the AOSP Go
  profile that `go_defaults_common.mk` would put on vendor: Java heap
  256m/128m instead of V30's 512m/196m, Go LMK tuning, leaner debug info,
  the Go launcher (`Launcher3QuickStepGo`) and the `android.hardware.ram.low`
  feature. It also removes unused preinstalled apps through
  [Android.mk](../device/cat/s22/Android.mk).
- system_server is compiled `speed-profile` either way. Soong uses that
  filter for system_server jars that ship a profile, so Go's
  `PRODUCT_SYSTEM_SERVER_COMPILER_FILTER` would change nothing and is left
  unset.
- lmkd uses PSI (`ro.lmk.use_psi=true`, overriding V30's `false`). The 4.9
  kernel provides `/proc/pressure/*`. lmkd's BPF memevent listener is not
  supported on 4.9 and logs an error, which is harmless.
- Android 15 and later honour `LOCAL_OVERRIDES_MODULES` only on EXECUTABLES,
  SHARED_LIBRARIES and ETC modules. After you change the list, run
  `m installclean`, or stale apps stay in the image.
- Memory test (reboot, wait 300 s, open camera, browser, Settings, Gallery,
  Contacts, Dialer, Messaging and Calendar one after another, after
  background dexopt), measured on the Android 15 base of this build: memory
  stalls 0.61/0.32 s, stalls during boot and the first 300 s 0.46 s, zram use
  52 MB, MemAvailable 957 MB, no kills, all apps survived. Not repeated on
  Android 16 yet. The CAT camera takes about 1.7 s to open.

## Small screen (480×640, 200 dpi)

- The phone has hardware Back/Home/Recents keys.
  - TrebleDroid turns the on-screen bar on by default through
    `persist.sys.phh.mainkeys=0`. The product sets it to `1`.
  - Launcher3 since Android 15 also draws the navigation buttons itself in its
    taskbar window (navbar unification, a read-only aconfig flag). The
    Launcher3 patch draws them only when the window manager reports a
    navigation bar.
- No `bootanimation.zip` is installed: AOSP's built-in animation is drawn in
  code and fits 480×640 by itself. (TrebleDroid's own zip is a seasonal one.)
  [scripts/make-bootanimation.py](../scripts/make-bootanimation.py) can rescale
  any other zip to 480×640 if you want one; its copy rule must come before
  `aosp_arm.mk` is included, because the first `PRODUCT_COPY_FILES` entry for
  a path wins.
- First boot, once (`persist.cat.s22.ui_defaults`): single-line lock-screen
  clock and a 4×4 home-screen grid ([cat-s22-settings.sh](../device/cat/s22/cat-s22-settings.sh)).

## WebView

- Since Android 15, `WebViewUpdateServiceImpl2` takes the first
  `availableByDefault` provider in `config_webview_packages.xml` as the
  default and, unless the user chose another, uses only that one (`isFallback`
  no longer matters). TrebleDroid's `HighPriorityMisc` overlay lists Google
  WebView first, so on an AOSP build without it no WebView loaded at all
  (`WebViewPackageMissingException`). The `vendor/hardware_overlay` patch lists
  AOSP WebView first. On an older build, select it by hand:
  `cmd webviewupdate set-webview-implementation com.android.webview`.

## Development-build caveats

- The native helpers run as root in the userdebug `su` domain. A release build
  needs proper SELinux domains for them.
- There are no Google apps.

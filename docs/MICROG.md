# microG (optional)

The ROM has no Google apps. If you want push notifications in apps that use
Google's Firebase Cloud Messaging (most messengers and many other apps), you
can install [microG](https://microg.org), a free replacement for Google Play
services, as ordinary apps.

> Not tested on this phone yet. The steps follow microG's own instructions;
> report what works for you.

## Why it works on this ROM

microG has to present Google's signature to other apps. The ROM includes
TrebleDroid's two frameworks/base patches for that
(`Allow signature spoofing for microG Companion/Services` and
`Allow spoofing signingInfo for microG`). Spoofing is allowed only for
`com.google.android.gms` and `com.android.vending`, and only when they are
signed with the official microG key, so no other app can use it.

## Install

1. Download the two APKs of the latest release from
   [github.com/microg/GmsCore/releases](https://github.com/microg/GmsCore/releases):
   - `com.google.android.gms-<version>.apk` (microG Services, about 110 MB)
   - `com.android.vending-<version>.apk` (microG Companion, about 4 MB)

   Take the files **without** `-hw` (those are for Huawei phones) and not
   `org.microg.gms-...-user.apk` (that one is installed under a different
   package name and cannot take Google's place).
   Instead you can add microG's F-Droid repository
   (`https://microg.org/fdroid/repo`) to F-Droid and install both from there;
   updates then come through F-Droid.
2. Install both, Services first:

   ```bash
   adb install com.google.android.gms-<version>.apk
   adb install com.android.vending-<version>.apk
   ```

   or open the files on the phone and allow installing from that source.
3. Open **microG Settings** → **Self-Check**. Every line must be ticked;
   tap a line that is not to grant the missing permission. The
   "signature spoofing" lines must be ticked: if they are not, the ROM's
   patches are not active and nothing below will work.
4. **Google account**: microG Settings → Accounts → add your account.
5. **Cloud Messaging**: microG Settings → Cloud Messaging → turn on. Apps
   installed *before* this step register only after they are restarted or
   reinstalled.
6. **Battery**: Settings → Apps → microG Services → App battery usage →
   **Unrestricted**. Otherwise the push connection can be held back while
   the phone sleeps and notifications arrive late.

Get apps from Google Play with [Aurora Store](https://f-droid.org/packages/com.aurora.store/)
(from F-Droid), anonymously or with your account.

## What works and what does not

| Works | Does not work or only partly |
| --- | --- |
| Push notifications (Firebase Cloud Messaging) | Play Integrity: banking apps that check it, Google Pay / Wallet, some games |
| Sign in with Google in many apps | Paid apps and in-app purchases through Google Play |
| Network location (Wi-Fi/cell) for apps that ask Google for it | Google Maps inside other apps: partly |
| | Android Auto, Wear OS, Google Fit |

Play Integrity cannot pass on this ROM at all: it is a userdebug build and
the bootloader is unlocked. That is the same with or without microG.

## Memory

microG Services keeps a background process for the push connection. On a
1.9 GB phone expect several tens of megabytes more in use (not measured on
this phone yet). It is uninstalled like any app.

## Without microG

- Telegram and WhatsApp keep their own connection and notify without Google
  services, at some battery cost.
- Some free apps support [UnifiedPush](https://unifiedpush.org) (for example
  through the ntfy app) instead of Google.

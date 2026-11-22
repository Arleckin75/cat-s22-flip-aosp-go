# Testing

What is checked on a real CAT S22 Flip before a build is called working, and
what has been checked so far. The README's "What works" table is the summary;
this file says how and on which build.

## After every flash

Automatic, over adb (about a minute):

- boot completes, SELinux enforcing;
- `cat_s22_outer`, `cat_s22_keypad`, `cat_s22_keylight` running, CatS22Cover
  app running;
- no FATAL entries in the crash log, no unexpected SELinux denials;
- AOSP WebView is the current provider;
- removing an app (Gallery for user 0, restored at once) does not restart
  `system_server`;
- Messaging holds the SMS role, has `RECEIVE_WAP_PUSH` and
  `READ_CELL_BROADCASTS`, and opens without crashing;
- with a SIM: SIM loaded, network type, and the cover app's telephony
  callback follows the SIM subscription.

Manual, when the change touches them:

| Area | Steps |
| --- | --- |
| Cover screen, lid | close the lid: clock shows and goes dark after the configured time |
| Cover screen, volume keys | lid closed, screen dark: a volume key lights it |
| Sleep | unplug USB, lid closed 5–10 min (the phone suspends), then a volume key or an incoming call must light the cover within a second |
| Incoming call | caller's number or name on the cover; answer by opening the lid |
| Calls | outgoing and incoming, audio both ways, earpiece and Bluetooth headset |
| SMS | send (should leave within seconds) and receive; unread count on the cover |
| VoLTE / VoWiFi | `adb logcat` shows IMS registered; a call on LTE and one on Wi-Fi |
| Small screen | lock screen PIN pad, Settings pages, cover settings page readable at 170 dpi |

Logs: `adb logcat -d -s CatS22Cover CatS22CoverSvc` for the cover app and
service. The Go build keeps only minutes of logcat, so read it right after the
test.

## Results

All on A1 Bulgaria (MCC 284, MNC 01) unless noted.

| Date | Repo commit | Checked |
| --- | --- | --- |
| 2026-11-13 | 27584d7 | LTE by default; IMS registers over LTE; VoLTE calls both ways with HD voice, earpiece and Bluetooth; answering by opening the lid; Bluetooth A2DP stereo (Pixel Buds 2a) |
| 2026-11-13 | 9119bbb | SMS sent over CS within a second and received; VoWiFi calls both ways (IMS over IWLAN while on the mobile network; airplane mode + Wi-Fi did not register) |
| 2026-11-17 | e565d8f | removing and installing apps no longer restarts `system_server`; F-Droid installs; volume keys light the cover with the lid closed |
| 2026-11-18 | b3be0ee | cover settings page: first switch visible below the title; 170 dpi chosen on the phone |
| 2026-11-20 | ab8a60e | after 5 and 11 minutes of suspend, a volume key lights the cover within 0.5 s |
| 2026-11-21 | a43030d | after 7 minutes of suspend, an incoming call lights the cover within 0.5 s and shows the caller's number |

Not tested yet: a first install on a wiped phone with the current build
(Messaging's two permissions were granted by hand on the test phone after an
update), rollback to stock V30, battery life, carriers other than A1.

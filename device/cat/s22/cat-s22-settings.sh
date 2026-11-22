#!/system/bin/sh
# Preserve normal display timeout, power saving and kernel governor settings.
settings put secure incall_power_button_behavior 2

# One-time UI defaults for the 480x640 screen. Applied once so the user's own
# later choices are kept.
if [ "$(getprop persist.cat.s22.ui_defaults)" != "1" ]; then
    # Single-line lock screen clock; the two-line clock fills half the screen.
    settings put secure lockscreen_use_double_line_clock 0
    # 4x4 home grid instead of the 3x3 the launcher picks for this size.
    # The launcher provider may not be up yet right after boot; retry briefly.
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        if content query --uri content://com.android.launcher3.grid_control/list_options \
                >/dev/null 2>&1; then
            content update --uri content://com.android.launcher3.grid_control/default_grid \
                --bind name:s:4_by_4 && setprop persist.cat.s22.ui_defaults 1
            break
        fi
        sleep 3
    done
fi

# The default SMS app needs RECEIVE_WAP_PUSH and READ_CELL_BROADCASTS, or
# SmsApplication stops treating it as default and SMS cannot be sent. The SMS
# role grants them on a first install, but not after an update to a build that
# newly requests them (GitHub issue #1). Grant them to the role holder if
# missing; a user who picked another SMS app is left alone.
sms_app=$(cmd role get-role-holders android.app.role.SMS 2>/dev/null)
if [ "$sms_app" = "com.android.messaging" ]; then
    for perm in RECEIVE_WAP_PUSH READ_CELL_BROADCASTS; do
        if ! dumpsys package com.android.messaging \
                | grep -q "android.permission.$perm: granted=true"; then
            pm grant com.android.messaging android.permission.$perm \
                && log -t cat-s22-settings "granted $perm to com.android.messaging"
        fi
    done
fi

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

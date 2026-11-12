package cats22.cover;

import android.content.Context;
import android.os.UEventObserver;
import android.telecom.TelecomManager;
import android.util.Log;

/**
 * Answers a ringing call when the lid opens and, if enabled, ends the call
 * when it closes. The hall sensor's input device never reports SW_LID, so the
 * lid comes from the switch_hall uevents, like in cat_s22_cover.
 */
final class LidActions {
    private static final String TAG = "CatS22Cover";
    private static final String DEVPATH = "DEVPATH=/devices/virtual/switch_hall/hall_switch";

    private final Context mContext;
    private String mLastState;

    LidActions(Context context) {
        mContext = context;
    }

    void start() {
        new UEventObserver() {
            @Override
            public void onUEvent(UEvent event) {
                String state = event.get("SWITCH_STATE");  // 1 = open, 0 = closed
                if (state == null || state.equals(mLastState)) return;
                mLastState = state;
                onLid(state.equals("1"));
            }
        }.startObserving(DEVPATH);
    }

    private void onLid(boolean open) {
        TelecomManager telecom = mContext.getSystemService(TelecomManager.class);
        Log.i(TAG, "lid " + (open ? "open" : "closed") + ", ringing=" + telecom.isRinging());
        try {
            if (open) {
                if (telecom.isRinging() && Config.prefs(mContext).getBoolean("lid_answer", true)) {
                    telecom.acceptRingingCall();
                }
            } else if (telecom.isInCall() && !telecom.isRinging()
                    && Config.prefs(mContext).getBoolean("lid_end", false)) {
                telecom.endCall();
            }
        } catch (SecurityException e) {
            Log.w(TAG, "lid call action", e);
        }
    }
}

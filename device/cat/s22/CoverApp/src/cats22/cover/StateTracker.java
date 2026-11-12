package cats22.cover;

import android.content.BroadcastReceiver;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.database.ContentObserver;
import android.database.Cursor;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Handler;
import android.os.HandlerThread;
import android.provider.CallLog;
import android.provider.ContactsContract;
import android.provider.Telephony;
import android.telephony.ServiceState;
import android.telephony.SignalStrength;
import android.telephony.TelephonyCallback;
import android.telephony.TelephonyManager;
import android.text.TextUtils;
import android.util.Log;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Collects what the cover screen shows and writes state.txt on every change.
 * All callbacks run on one background thread, so no locking is needed.
 */
final class StateTracker {
    private static final String TAG = "CatS22Cover";

    private final Context mContext;
    private final Handler mHandler;
    private final Map<String, String> mState = new LinkedHashMap<>();
    private String mLastWritten = "";
    private int mCallState = TelephonyManager.CALL_STATE_IDLE;
    private String mIncomingNumber = "";
    private int mMissed = -1;
    private int mSms = -1;
    private int mSignal = -1;
    private boolean mInService;

    StateTracker(Context context) {
        mContext = context;
        HandlerThread thread = new HandlerThread("cover-state");
        thread.start();
        mHandler = new Handler(thread.getLooper());
        mState.put("call", "idle");
        mState.put("seq", "0");
        mState.put("signal", "-1");
        mState.put("wifi", "-1");
    }

    void start() {
        mHandler.post(this::startOnThread);
    }

    private void startOnThread() {
        TelephonyManager tm = mContext.getSystemService(TelephonyManager.class);
        try {
            tm.registerTelephonyCallback(mHandler::post, new Callback());
        } catch (SecurityException e) {
            Log.w(TAG, "telephony callback", e);
        }

        // The number of an incoming call is only delivered with this broadcast.
        IntentFilter phone = new IntentFilter(TelephonyManager.ACTION_PHONE_STATE_CHANGED);
        mContext.registerReceiver(new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                String number = intent.getStringExtra(TelephonyManager.EXTRA_INCOMING_NUMBER);
                if (!TextUtils.isEmpty(number)) {
                    mIncomingNumber = number;
                    updateCall();
                }
            }
        }, phone, null, mHandler, Context.RECEIVER_EXPORTED);

        // Call log and SMS live in credential-encrypted storage: refresh after unlock.
        mContext.registerReceiver(new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                refreshCounts();
            }
        }, new IntentFilter(Intent.ACTION_USER_UNLOCKED), null, mHandler,
                Context.RECEIVER_EXPORTED);

        // Providers notify in bursts, and the SMS provider notifies when it creates
        // the thread, before the message row itself is written: refresh once the
        // burst has settled.
        Runnable refresh = this::refreshCounts;
        ContentObserver observer = new ContentObserver(mHandler) {
            @Override
            public void onChange(boolean selfChange) {
                mHandler.removeCallbacks(refresh);
                mHandler.postDelayed(refresh, 500);
            }
        };
        ContentResolver resolver = mContext.getContentResolver();
        // CallLogProvider notifies content://call_log, not content://call_log/calls.
        resolver.registerContentObserver(CallLog.CONTENT_URI, true, observer);
        resolver.registerContentObserver(Telephony.Sms.CONTENT_URI, true, observer);

        ConnectivityManager cm = mContext.getSystemService(ConnectivityManager.class);
        NetworkRequest wifi = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build();
        cm.registerNetworkCallback(wifi, new ConnectivityManager.NetworkCallback() {
            @Override
            public void onCapabilitiesChanged(Network network, NetworkCapabilities caps) {
                WifiManager wm = mContext.getSystemService(WifiManager.class);
                int level = wm.calculateSignalLevel(caps.getSignalStrength());
                int max = Math.max(1, wm.getMaxSignalLevel());
                put("wifi", String.valueOf(Math.round(level * 4f / max)));
            }

            @Override
            public void onLost(Network network) {
                put("wifi", "-1");
            }
        }, mHandler);

        refreshCounts();
        updateSignal();
    }

    private final class Callback extends TelephonyCallback implements
            TelephonyCallback.CallStateListener,
            TelephonyCallback.SignalStrengthsListener,
            TelephonyCallback.ServiceStateListener {
        @Override
        public void onCallStateChanged(int state) {
            int previous = mCallState;
            mCallState = state;
            if (state == TelephonyManager.CALL_STATE_OFFHOOK
                    && previous != TelephonyManager.CALL_STATE_OFFHOOK) {
                mState.put("call_since", String.valueOf(System.currentTimeMillis()));
            }
            if (state == TelephonyManager.CALL_STATE_IDLE) mIncomingNumber = "";
            updateCall();
            if (state == TelephonyManager.CALL_STATE_IDLE) refreshCounts();
        }

        @Override
        public void onSignalStrengthsChanged(SignalStrength strength) {
            mSignal = strength.getLevel();
            updateSignal();
        }

        @Override
        public void onServiceStateChanged(ServiceState state) {
            mInService = state.getState() == ServiceState.STATE_IN_SERVICE;
            mState.put("operator", mInService ? nonNull(state.getOperatorAlphaShort()) : "");
            updateSignal();
        }
    }

    private void updateSignal() {
        put("signal", String.valueOf(mInService ? Math.max(0, mSignal) : -1));
    }

    private void updateCall() {
        String call;
        switch (mCallState) {
            case TelephonyManager.CALL_STATE_RINGING: call = "ringing"; break;
            case TelephonyManager.CALL_STATE_OFFHOOK: call = "active"; break;
            default: call = "idle";
        }
        mState.put("call", call);
        if (call.equals("idle")) {
            mState.put("call_name", "");
            mState.put("call_number", "");
            mState.remove("call_since");
        } else if (!mIncomingNumber.isEmpty()) {
            mState.put("call_number", mIncomingNumber);
            mState.put("call_name", contactName(mIncomingNumber));
        }
        write();
    }

    private void refreshCounts() {
        int missed = mMissed, sms = mSms;
        try (Cursor c = mContext.getContentResolver().query(CallLog.Calls.CONTENT_URI,
                new String[] {CallLog.Calls.NUMBER, CallLog.Calls.CACHED_NAME},
                CallLog.Calls.TYPE + "=" + CallLog.Calls.MISSED_TYPE + " AND "
                        + CallLog.Calls.NEW + "=1",
                null, CallLog.Calls.DATE + " DESC")) {
            if (c != null) {
                missed = c.getCount();
                String who = "";
                if (c.moveToFirst()) {
                    who = nonNull(c.getString(1));
                    if (who.isEmpty()) who = contactNameOrNumber(c.getString(0));
                }
                mState.put("missed", String.valueOf(missed));
                mState.put("missed_name", who);
            }
        } catch (RuntimeException e) {
            Log.i(TAG, "call log not available yet: " + e.getMessage());
        }
        try (Cursor c = mContext.getContentResolver().query(Telephony.Sms.Inbox.CONTENT_URI,
                new String[] {Telephony.Sms.ADDRESS, Telephony.Sms.BODY},
                Telephony.Sms.READ + "=0", null, Telephony.Sms.DATE + " DESC")) {
            if (c != null) {
                sms = c.getCount();
                String who = "", body = "";
                if (c.moveToFirst()) {
                    who = contactNameOrNumber(c.getString(0));
                    body = nonNull(c.getString(1));
                }
                mState.put("sms", String.valueOf(sms));
                mState.put("sms_name", who);
                mState.put("sms_body", body);
            }
        } catch (RuntimeException e) {
            Log.i(TAG, "SMS not available yet: " + e.getMessage());
        }
        // A new missed call or message wakes the cover screen; the first read after
        // boot only establishes the baseline.
        boolean grew = (mMissed >= 0 && missed > mMissed) || (mSms >= 0 && sms > mSms);
        mMissed = missed;
        mSms = sms;
        if (grew) mState.put("seq", String.valueOf(System.currentTimeMillis()));
        write();
    }

    private String contactNameOrNumber(String number) {
        String name = contactName(number);
        return name.isEmpty() ? nonNull(number) : name;
    }

    private String contactName(String number) {
        if (TextUtils.isEmpty(number)) return "";
        Uri uri = Uri.withAppendedPath(ContactsContract.PhoneLookup.CONTENT_FILTER_URI,
                Uri.encode(number));
        try (Cursor c = mContext.getContentResolver().query(uri,
                new String[] {ContactsContract.PhoneLookup.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return nonNull(c.getString(0));
        } catch (RuntimeException e) {
            Log.i(TAG, "contacts not available: " + e.getMessage());
        }
        return "";
    }

    private void put(String key, String value) {
        mState.put(key, value);
        write();
    }

    /** Writes state.txt only when something actually changed. */
    private void write() {
        String content = Config.dump(mState);
        if (content.equals(mLastWritten)) return;
        mLastWritten = content;
        Config.atomicWrite(mContext, "state.txt", content);
    }

    private static String nonNull(String s) {
        return s == null ? "" : s;
    }
}

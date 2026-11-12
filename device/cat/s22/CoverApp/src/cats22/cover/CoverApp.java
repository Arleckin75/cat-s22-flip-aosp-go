package cats22.cover;

import android.app.Application;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;

/**
 * Persistent process: keeps config.txt and state.txt current for cat_s22_cover
 * and runs the programmable key and lid actions.
 */
public class CoverApp extends Application {
    private StateTracker mTracker;
    // Held so the listener is not garbage collected.
    private SharedPreferences.OnSharedPreferenceChangeListener mPrefsListener;

    @Override
    public void onCreate() {
        super.onCreate();
        Config.write(this);
        mPrefsListener = (prefs, key) -> Config.write(this);
        Config.prefs(this).registerOnSharedPreferenceChangeListener(mPrefsListener);
        registerReceiver(new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                Config.write(CoverApp.this);  // day/month names and labels follow the locale
            }
        }, new IntentFilter(Intent.ACTION_LOCALE_CHANGED));
        mTracker = new StateTracker(this);
        mTracker.start();
        new KeyActions(this).start();
        new LidActions(this).start();
    }
}

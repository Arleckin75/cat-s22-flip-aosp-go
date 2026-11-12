package cats22.cover;

import android.app.Activity;
import android.os.Bundle;
import android.preference.PreferenceFragment;

/** "Cover screen" settings, reachable from Settings > Display. */
public class SettingsActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (savedInstanceState == null) {
            getFragmentManager().beginTransaction()
                    .replace(android.R.id.content, new SettingsFragment())
                    .commit();
        }
    }

    /** Framework preferences: Settings-like look and D-pad navigation out of the box. */
    @SuppressWarnings("deprecation")
    public static class SettingsFragment extends PreferenceFragment {
        @Override
        public void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            getPreferenceManager().setStorageDeviceProtected();
            addPreferencesFromResource(R.xml.settings);
        }
    }
}

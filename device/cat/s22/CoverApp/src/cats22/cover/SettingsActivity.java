package cats22.cover;

import android.app.Activity;
import android.graphics.Insets;
import android.os.Bundle;
import android.preference.PreferenceFragment;
import android.view.View;
import android.view.WindowInsets;

/** "Cover screen" settings, reachable from Settings > Display. */
public class SettingsActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Android 15+ draws apps edge to edge, and the framework action bar does
        // not push the content down: the list started under the status bar and
        // the title, hiding the first preference. Pad the content down to the
        // bottom of the action bar (or of the status bar without one).
        View content = findViewById(android.R.id.content);
        content.setOnApplyWindowInsetsListener((v, insets) -> {
            mBars = insets.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            applyPadding(v);
            return WindowInsets.CONSUMED;
        });
        getWindow().getDecorView().addOnLayoutChangeListener(
                (v, l, t, r, b, ol, ot, or, ob) -> applyPadding(content));
        if (savedInstanceState == null) {
            getFragmentManager().beginTransaction()
                    .replace(android.R.id.content, new SettingsFragment())
                    .commit();
        }
    }

    private Insets mBars = Insets.NONE;

    private void applyPadding(View content) {
        int top = mBars.top;
        int id = getResources().getIdentifier("action_bar_container", "id", "android");
        View bar = id == 0 ? null : getWindow().getDecorView().findViewById(id);
        if (bar != null && bar.getVisibility() == View.VISIBLE && bar.getHeight() > 0) {
            int[] barPos = new int[2];
            int[] contentPos = new int[2];
            bar.getLocationInWindow(barPos);
            content.getLocationInWindow(contentPos);
            top = Math.max(top, barPos[1] + bar.getHeight() - contentPos[1]);
        }
        if (content.getPaddingTop() != top || content.getPaddingLeft() != mBars.left
                || content.getPaddingRight() != mBars.right
                || content.getPaddingBottom() != mBars.bottom) {
            content.setPadding(mBars.left, top, mBars.right, mBars.bottom);
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

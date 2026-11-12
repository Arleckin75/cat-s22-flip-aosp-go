package cats22.cover;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.text.DateFormatSymbols;
import java.util.Locale;
import java.util.Map;

/** Settings shared with cat_s22_cover through config.txt (key=value lines). */
final class Config {
    private static final String TAG = "CatS22Cover";

    private Config() {}

    /** The same device-protected file PreferenceFragment uses (see SettingsActivity). */
    static SharedPreferences prefs(Context context) {
        Context dp = context.createDeviceProtectedStorageContext();
        return dp.getSharedPreferences(context.getPackageName() + "_preferences",
                Context.MODE_PRIVATE);
    }

    static void write(Context context) {
        SharedPreferences p = prefs(context);
        StringBuilder out = new StringBuilder();
        line(out, "enabled", p.getBoolean("enabled", true) ? "1" : "0");
        line(out, "timeout_s", p.getString("timeout_s", "10"));
        line(out, "bright_day", p.getString("bright_day", "192"));
        line(out, "bright_night", p.getString("bright_night", "25"));
        line(out, "night_start", p.getString("night_start", "22"));
        line(out, "night_end", p.getString("night_end", "7"));
        line(out, "show_call", p.getBoolean("show_call", true) ? "1" : "0");
        line(out, "show_missed", p.getBoolean("show_missed", true) ? "1" : "0");
        line(out, "show_sms", p.getBoolean("show_sms", true) ? "1" : "0");
        line(out, "sms_text", p.getBoolean("sms_text", false) ? "1" : "0");
        DateFormatSymbols symbols = DateFormatSymbols.getInstance(Locale.getDefault());
        String[] weekdays = symbols.getShortWeekdays();  // index 1 = Sunday
        StringBuilder days = new StringBuilder();
        for (int i = 1; i <= 7; ++i) days.append(i > 1 ? "," : "").append(clean(weekdays[i]));
        line(out, "days", days.toString());
        StringBuilder months = new StringBuilder();
        String[] shortMonths = symbols.getShortMonths();
        for (int i = 0; i < 12; ++i) months.append(i > 0 ? "," : "").append(clean(shortMonths[i]));
        line(out, "months", months.toString());
        line(out, "lbl_incoming", context.getString(R.string.cover_incoming));
        line(out, "lbl_call", context.getString(R.string.cover_call));
        line(out, "lbl_unknown", context.getString(R.string.cover_unknown));
        line(out, "lbl_no_service", context.getString(R.string.cover_no_service));
        atomicWrite(context, "config.txt", out.toString());
    }

    static void line(StringBuilder out, String key, String value) {
        out.append(key).append('=').append(value == null ? "" : oneLine(value)).append('\n');
    }

    /** A list element: one line and no commas (the list separator). */
    static String clean(String value) {
        return oneLine(value.replace(',', ' '));
    }

    /** One line, length-limited. */
    static String oneLine(String value) {
        String s = value.replace('\n', ' ').replace('\r', ' ').trim();
        return s.length() > 150 ? s.substring(0, 150) : s;
    }

    /** Write-then-rename so the service never reads a half-written file. */
    static synchronized void atomicWrite(Context context, String name, String content) {
        File dir = context.createDeviceProtectedStorageContext().getFilesDir();
        File tmp = new File(dir, name + ".tmp");
        try (FileOutputStream stream = new FileOutputStream(tmp)) {
            stream.write(content.getBytes(StandardCharsets.UTF_8));
            stream.getFD().sync();
        } catch (Exception e) {
            Log.w(TAG, "write " + name, e);
            return;
        }
        if (!tmp.renameTo(new File(dir, name))) Log.w(TAG, "rename " + name + " failed");
    }

    static String dump(Map<String, String> values) {
        StringBuilder out = new StringBuilder();
        for (Map.Entry<String, String> e : values.entrySet()) line(out, e.getKey(), e.getValue());
        return out.toString();
    }
}

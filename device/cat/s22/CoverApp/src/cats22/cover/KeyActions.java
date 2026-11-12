package cats22.cover;

import android.Manifest;
import android.app.KeyguardManager;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.media.AudioManager;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.MediaStore;
import android.util.Log;
import android.widget.Toast;

/**
 * Actions for the programmable key. cat_s22_keypad turns a short press into
 * KEYCODE_MACRO_1 and a long press into KEYCODE_MACRO_2; PhoneWindowManager
 * broadcasts both to this app whatever has focus, also with the screen off.
 */
final class KeyActions {
    private static final String TAG = "CatS22Cover";
    static final String ACTION_KEY = "cats22.cover.action.PROGRAMMABLE_KEY";
    static final String EXTRA_LONG_PRESS = "long_press";

    private final Context mContext;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private String mTorchCamera;
    private boolean mTorchOn;

    KeyActions(Context context) {
        mContext = context;
    }

    void start() {
        CameraManager camera = mContext.getSystemService(CameraManager.class);
        try {
            for (String id : camera.getCameraIdList()) {
                Boolean flash = camera.getCameraCharacteristics(id)
                        .get(CameraCharacteristics.FLASH_INFO_AVAILABLE);
                if (Boolean.TRUE.equals(flash)) {
                    mTorchCamera = id;
                    break;
                }
            }
        } catch (CameraAccessException | RuntimeException e) {
            Log.w(TAG, "no torch", e);
        }
        if (mTorchCamera != null) {
            camera.registerTorchCallback(new CameraManager.TorchCallback() {
                @Override
                public void onTorchModeChanged(String id, boolean enabled) {
                    if (id.equals(mTorchCamera)) mTorchOn = enabled;
                }
            }, mHandler);
        }
        // Only system_server (which holds every permission) can send this.
        mContext.registerReceiver(new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                boolean longPress = intent.getBooleanExtra(EXTRA_LONG_PRESS, false);
                run(Config.prefs(mContext).getString(longPress ? "key_long" : "key_short",
                        longPress ? "torch" : "ringer"));
            }
        }, new IntentFilter(ACTION_KEY), Manifest.permission.STATUS_BAR_SERVICE, mHandler,
                Context.RECEIVER_EXPORTED);
    }

    private void run(String action) {
        switch (action) {
            case "ringer": cycleRinger(); break;
            case "torch": toggleTorch(); break;
            case "camera": openCamera(); break;
            default: break;
        }
    }

    /** Sound -> vibrate -> silent -> sound, like the volume panel's ringer button. */
    private void cycleRinger() {
        AudioManager audio = mContext.getSystemService(AudioManager.class);
        int next;
        int label;
        switch (audio.getRingerModeInternal()) {
            case AudioManager.RINGER_MODE_NORMAL:
                next = AudioManager.RINGER_MODE_VIBRATE;
                label = R.string.ringer_vibrate;
                break;
            case AudioManager.RINGER_MODE_VIBRATE:
                next = AudioManager.RINGER_MODE_SILENT;
                label = R.string.ringer_silent;
                break;
            default:
                next = AudioManager.RINGER_MODE_NORMAL;
                label = R.string.ringer_normal;
        }
        audio.setRingerModeInternal(next);
        if (next == AudioManager.RINGER_MODE_VIBRATE) {
            mContext.getSystemService(Vibrator.class).vibrate(
                    VibrationEffect.createPredefined(VibrationEffect.EFFECT_HEAVY_CLICK));
        }
        Toast.makeText(mContext, label, Toast.LENGTH_SHORT).show();
    }

    private void toggleTorch() {
        if (mTorchCamera == null) return;
        try {
            mContext.getSystemService(CameraManager.class).setTorchMode(mTorchCamera, !mTorchOn);
        } catch (CameraAccessException | RuntimeException e) {
            // The camera app holds the flash while it is open.
            Log.w(TAG, "torch", e);
        }
    }

    private void openCamera() {
        // Over the lock screen if the camera app supports it, otherwise after unlock.
        boolean locked = mContext.getSystemService(KeyguardManager.class).isKeyguardLocked();
        if (locked && start(MediaStore.INTENT_ACTION_STILL_IMAGE_CAMERA_SECURE)) return;
        start(MediaStore.INTENT_ACTION_STILL_IMAGE_CAMERA);
    }

    private boolean start(String action) {
        try {
            mContext.startActivity(new Intent(action).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            return true;
        } catch (ActivityNotFoundException e) {
            Log.w(TAG, "no activity for " + action);
            return false;
        }
    }
}

package org.dvfel.settings;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.TextView;
import android.widget.Toast;

/**
 * Shows the playback badge. The module's monitor sends this when a FEL session starts.
 * It draws a small overlay at the top right; the module grants the overlay permission.
 * Toasts are the fallback, but Android suppresses them while the app's notifications are off.
 */
public class BadgeReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        String text = intent.getStringExtra("text");
        if (text == null || text.isEmpty()) text = "Dolby Vision";
        Context app = context.getApplicationContext();
        if (!Settings.canDrawOverlays(app)) {
            longToast(app, text, goAsync());
            return;
        }
        final PendingResult pending = goAsync();
        final WindowManager wm = (WindowManager) app.getSystemService(Context.WINDOW_SERVICE);
        final TextView badge = new TextView(app);
        float d = app.getResources().getDisplayMetrics().density;
        badge.setText(text);
        badge.setTextSize(16);
        badge.setTextColor(Color.WHITE);
        badge.setPadding(Math.round(18 * d), Math.round(10 * d), Math.round(18 * d), Math.round(10 * d));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.argb(200, 16, 24, 40));
        bg.setStroke(Math.round(2 * d), Color.rgb(120, 180, 255));
        bg.setCornerRadius(12 * d);
        badge.setBackground(bg);
        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE,
            PixelFormat.TRANSLUCENT);
        lp.alpha = 0.8f; // above 0.8 Android treats the overlay as untrusted
        lp.gravity = Gravity.TOP | Gravity.END;
        lp.x = Math.round(32 * d);
        lp.y = Math.round(32 * d);
        try {
            wm.addView(badge, lp);
        } catch (Exception e) {
            longToast(app, text, pending);
            return;
        }
        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            try { wm.removeView(badge); } catch (Exception ignored) { }
            pending.finish();
        }, SHOW_MS);
    }

    /** On screen this long: a refresh rate switch at the start of a play blanks the first seconds. */
    private static final int SHOW_MS = 8000;
    /** One LENGTH_LONG toast (3.5 s): a second queued toast flashed on the TV. */
    private static void longToast(Context app, String text, final PendingResult pending) {
        Toast.makeText(app, text, Toast.LENGTH_LONG).show();
        pending.finish();
    }
}

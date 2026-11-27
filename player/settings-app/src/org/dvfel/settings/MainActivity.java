package org.dvfel.settings;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.SeekBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.Map;

/**
 * Settings for the onn 4K Pro Dolby Vision FEL module. The app has no root: it writes
 * request.conf in its own files folder, and the module's root monitor applies it and
 * writes status.txt back here.
 */
public class MainActivity extends Activity {
    static final int[] NITS = {320, 372, 499, 578, 775, 1037};
    static final String[] MODES = {"auto", "displayled", "lldv", "off"};
    static final String[] MODE_LABELS = {
        "Automatic (recommended)", "Display-led Dolby Vision", "LLDV sent as HDR10", "Off (HDR10 base layer)"};
    static final String[] META = {"A", "B", "C"};
    static final String[] META_LABELS = {
        "A: brightness target (default)", "B: movie's own values *", "C: movie's values, capped *"};

    final Handler ui = new Handler(Looper.getMainLooper());
    RadioGroup modeGroup, metaGroup;
    SeekBar nitsBar;
    TextView nitsValue, statusView, messageView;
    CheckBox badgeBox, advancedBox, alwaysBox;
    LinearLayout advanced;
    Button applyButton;
    boolean loaded;
    long pendingReq;

    final Runnable poll = new Runnable() {
        public void run() {
            readStatus();
            ui.postDelayed(this, 1000);
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.HORIZONTAL);
        page.setPadding(dp(40), dp(20), dp(40), dp(16));

        LinearLayout left = column();
        LinearLayout right = column();
        page.addView(left, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1.1f));
        page.addView(right, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f));
        right.setPadding(dp(32), 0, 0, 0);

        left.addView(text("onn 4K Pro Dolby Vision FEL", 20, true));
        left.addView(header("Output mode"));
        modeGroup = new RadioGroup(this);
        for (int i = 0; i < MODES.length; i++) modeGroup.addView(radio(MODE_LABELS[i], 1 + i));
        modeGroup.setOnCheckedChangeListener((g, id) -> updateEnabled());
        left.addView(modeGroup);
        left.addView(small("Automatic uses display-led when the display reports Dolby Vision, otherwise LLDV. "
            + "LLDV stays on, including the home screen, so FEL composes from the first frame."));

        left.addView(header("LLDV brightness target"));
        nitsBar = new SeekBar(this);
        nitsBar.setMax(NITS.length - 1);
        nitsBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar s, int p, boolean user) { nitsValue.setText(NITS[p] + " nits"); }
            public void onStartTrackingTouch(SeekBar s) {}
            public void onStopTrackingTouch(SeekBar s) {}
        });
        left.addView(nitsBar);
        nitsValue = text("", 16, true);
        nitsValue.setGravity(Gravity.CENTER_HORIZONTAL);
        left.addView(nitsValue);

        alwaysBox = new CheckBox(this);
        alwaysBox.setText("LLDV only while a FEL video plays (experimental: FEL does not show yet)");
        alwaysBox.setTextSize(13);
        left.addView(alwaysBox);

        badgeBox = new CheckBox(this);
        badgeBox.setText("Show a badge when Dolby Vision playback starts");
        badgeBox.setTextSize(13);
        left.addView(badgeBox);

        advancedBox = new CheckBox(this);
        advancedBox.setText("Advanced: HDR10 metadata for LLDV");
        advancedBox.setTextSize(13);
        left.addView(advancedBox);
        advanced = new LinearLayout(this);
        advanced.setOrientation(LinearLayout.VERTICAL);
        metaGroup = new RadioGroup(this);
        metaGroup.setOrientation(RadioGroup.VERTICAL);
        for (int i = 0; i < META.length; i++) metaGroup.addView(radio(META_LABELS[i], 101 + i));
        advanced.addView(metaGroup);
        advanced.addView(small("* needs the player to send the movie's values; until then it reports the target."));
        advanced.setVisibility(View.GONE);
        advancedBox.setOnCheckedChangeListener((b, on) -> advanced.setVisibility(on ? View.VISIBLE : View.GONE));
        left.addView(advanced);

        applyButton = new Button(this);
        applyButton.setText("Apply");
        applyButton.setTextSize(11.2f);
        styleApply(applyButton);
        applyButton.setOnClickListener(v -> apply());
        // 80% of the column width, left-aligned with the status table.
        LinearLayout applyRow = new LinearLayout(this);
        applyRow.setOrientation(LinearLayout.HORIZONTAL);
        applyRow.setWeightSum(1f);
        applyRow.addView(applyButton, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 0.8f));
        right.addView(applyRow);
        messageView = text("Waiting for the module...", 13, false);
        messageView.setTextColor(Color.rgb(255, 200, 80));
        right.addView(messageView);
        right.addView(header("Status"));
        LinearLayout table = column();
        for (String[] r : ROWS) {
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setPadding(0, 0, 0, dp(2));
            TextView name = text(r[1], 9.6f, false);
            name.setTypeface(Typeface.MONOSPACE);
            name.setTextColor(Color.rgb(200, 200, 200));
            row.addView(name, new LinearLayout.LayoutParams(dp(94), LinearLayout.LayoutParams.WRAP_CONTENT));
            LinearLayout cell = column();
            TextView value = text("-", 9.6f, false);
            value.setTypeface(Typeface.MONOSPACE);
            value.setTextColor(Color.WHITE);
            statusValues.put(r[0], value);
            cell.addView(value);
            TextView hint = text(r[2], 7.2f, false);
            hint.setTextColor(Color.rgb(140, 140, 140));
            cell.addView(hint);
            row.addView(cell, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            table.addView(row);
        }
        right.addView(table);
        statusView = small("");
        right.addView(statusView);

        setContentView(page);
        modeGroup.check(1);
        metaGroup.check(101);
        nitsBar.setProgress(2);
        badgeBox.setChecked(true);
        modeGroup.requestFocus();
    }

    @Override
    protected void onResume() {
        super.onResume();
        ui.post(poll);
    }

    @Override
    protected void onPause() {
        super.onPause();
        ui.removeCallbacks(poll);
    }

    void apply() {
        int mode = Math.max(0, modeGroup.getCheckedRadioButtonId() - 1);
        int meta = Math.max(0, metaGroup.getCheckedRadioButtonId() - 101);
        pendingReq = System.currentTimeMillis();
        String req = "MODE=" + MODES[mode] + "\nNITS=" + NITS[nitsBar.getProgress()] + "\nMETA=" + META[meta]
            + "\nBADGE=" + (badgeBox.isChecked() ? 1 : 0)
            + "\nWHEN=" + (alwaysBox.isChecked() ? "playback" : "always") + "\nREQ=" + pendingReq + "\n";
        try {
            File tmp = new File(getFilesDir(), "request.tmp");
            try (FileOutputStream out = new FileOutputStream(tmp)) { out.write(req.getBytes(StandardCharsets.UTF_8)); }
            if (!tmp.renameTo(new File(getFilesDir(), "request.conf"))) throw new Exception("rename failed");
            applyButton.setEnabled(false);
            messageView.setText("Applying...");
        } catch (Exception e) {
            messageView.setText("Could not save: " + e.getMessage());
        }
    }

    void readStatus() {
        File f = new File(getFilesDir(), "status.txt");
        if (!f.exists()) {
            messageView.setText("Waiting for the module. Is the onn 4K Pro Dolby Vision FEL Magisk module installed?");
            return;
        }
        Map<String, String> s = new HashMap<>();
        try {
            for (String line : new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8).split("\n")) {
                int i = line.indexOf('=');
                if (i > 0) s.put(line.substring(0, i), line.substring(i + 1).trim());
            }
        } catch (Exception e) {
            return;
        }
        if (!loaded) {
            loaded = true;
            modeGroup.check(1 + indexOf(MODES, s.get("MODE")));
            metaGroup.check(101 + indexOf(META, s.get("META")));
            for (int i = 0; i < NITS.length; i++) if (String.valueOf(NITS[i]).equals(s.get("NITS"))) nitsBar.setProgress(i);
            nitsValue.setText(NITS[nitsBar.getProgress()] + " nits");
            badgeBox.setChecked(!"0".equals(s.get("BADGE")));
            alwaysBox.setChecked("playback".equals(s.get("WHEN")));
            updateEnabled();
            messageView.setText("");
        }
        if (pendingReq != 0 && String.valueOf(pendingReq).equals(s.get("req"))) {
            pendingReq = 0;
            applyButton.setEnabled(true);
            messageView.setText("Applied.");
        }
        showStatus(s);
    }

    // key, name, what the line means
    static final String[][] ROWS = {
        {"module", "Module", "kernel module version"},
        {"now", "Current Mode", "how the box is outputting right now"},
        {"fel", "FEL playing", "FEL video composing"},
        {"display_dv", "Display DV", "TV supports Dolby Vision"},
        {"display_lldv", "LLDV Supported", "TV can support LLDV (Player LED DV)"},
        {"hdmi", "HDMI", "current mode and colour format"},
        {"hdr", "Signal", "HDR format the TV receives"},
        {"dv", "DV Engine", "status of DV engine"},
        {"firmware", "Firmware", "box firmware build"},
    };
    final Map<String, TextView> statusValues = new HashMap<>();

    void showStatus(Map<String, String> s) {
        set("module", "1".equals(s.get("loaded")) ? "loaded " + s.get("version") : "NOT LOADED");
        set("now", nowLabel(s.get("active")));
        set("fel", yes(s.get("fel")));
        set("display_dv", yes(s.get("display_dv")));
        set("display_lldv", yes(s.get("display_lldv")));
        set("hdmi", s.get("output") + ", " + s.get("link"));
        set("hdr", s.get("hdr"));
        set("dv", s.get("dv"));
        set("firmware", s.get("firmware"));
        statusView.setText("1".equals(s.get("asleep")) ? "Display asleep" : "");
    }

    void set(String key, String v) {
        statusValues.get(key).setText(v == null || v.isEmpty() ? "-" : v);
    }

    // Grey at rest, dark yellow when focused, dimmed while applying.
    void styleApply(Button b) {
        android.graphics.drawable.StateListDrawable bg = new android.graphics.drawable.StateListDrawable();
        bg.addState(new int[] {-android.R.attr.state_enabled}, box(Color.rgb(60, 60, 60)));
        bg.addState(new int[] {android.R.attr.state_focused}, box(Color.rgb(176, 128, 10)));
        bg.addState(new int[] {android.R.attr.state_pressed}, box(Color.rgb(176, 128, 10)));
        bg.addState(new int[] {}, box(Color.rgb(85, 85, 85)));
        b.setBackground(bg);
        b.setTextColor(new android.content.res.ColorStateList(
            new int[][] {{-android.R.attr.state_enabled}, {}},
            new int[] {Color.rgb(140, 140, 140), Color.WHITE}));
        b.setMinHeight(0);
        b.setMinimumHeight(0);
        b.setPadding(dp(13), dp(8), dp(13), dp(8));
    }

    android.graphics.drawable.GradientDrawable box(int fill) {
        android.graphics.drawable.GradientDrawable d = new android.graphics.drawable.GradientDrawable();
        d.setColor(fill);
        d.setCornerRadius(dp(6));
        return d;
    }

    static String nowLabel(String v) {
        if ("idle".equals(v)) return "Standard (no Dolby Vision)";
        if ("lldv".equals(v)) return "LLDV (Dolby Vision as HDR10)";
        if ("displayled".equals(v)) return "Display-led Dolby Vision";
        if ("off".equals(v)) return "HDR10 only (FEL off)";
        if ("restoring".equals(v)) return "Switching to standard...";
        return v == null || v.isEmpty() ? "-" : v;
    }


    void updateEnabled() {
        int mode = modeGroup.getCheckedRadioButtonId() - 1;
        boolean lldv = mode == 0 || mode == 2;
        nitsBar.setEnabled(lldv);
        nitsValue.setAlpha(lldv ? 1f : 0.4f);
        alwaysBox.setEnabled(lldv);
    }

    static String yes(String v) { return "1".equals(v) ? "yes" : "no"; }

    static int indexOf(String[] a, String v) {
        for (int i = 0; i < a.length; i++) if (a[i].equals(v)) return i;
        return 0;
    }

    LinearLayout column() {
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        return l;
    }

    RadioButton radio(String label, int id) {
        RadioButton r = new RadioButton(this);
        r.setText(label);
        r.setId(id);
        r.setTextSize(14);
        return r;
    }

    TextView header(String s) {
        TextView t = text(s, 15, true);
        t.setPadding(0, dp(10), 0, dp(2));
        t.setTextColor(Color.rgb(120, 180, 255));
        return t;
    }

    TextView small(String s) {
        TextView t = text(s, 11, false);
        t.setTextColor(Color.rgb(170, 170, 170));
        return t;
    }

    TextView text(String s, float size, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(size);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    int dp(int v) { return Math.round(v * getResources().getDisplayMetrics().density); }
}

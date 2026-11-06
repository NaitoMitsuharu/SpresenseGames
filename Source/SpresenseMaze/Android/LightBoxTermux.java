package com.lati.lightbox;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.os.Bundle;

/**
 * Bridge to Termux for the readiness check that runs when Hikari Atelier opens.
 *
 * Uses Termux's documented RUN_COMMAND service. Results come back through a
 * PendingIntent addressed to this app only; the Gateway key is stored only
 * when the app has none yet (same rule as the `am start` handover).
 *
 * C++ drives the sequence and polls {@link #poll()} for events:
 *   "doctor:ok", "doctor:scripts", "doctor:tools", "doctor:external", "doctor:error"
 *   "token:stored", "token:kept", "token:missing", "token:external", "token:error"
 */
public final class LightBoxTermux {
    public static final int READY = 0;
    public static final int NOT_INSTALLED = 1;
    public static final int NO_PERMISSION = 2;

    private static final String TERMUX = "com.termux";
    private static final String PERMISSION = "com.termux.permission.RUN_COMMAND";
    private static final String HOME = "/data/data/com.termux/files/home";
    private static final String BASH = "/data/data/com.termux/files/usr/bin/bash";
    private static final String ACTION_RESULT = "com.lati.lightbox.TERMUX_RESULT";
    private static final String ENDPOINT = "http://127.0.0.1:18789";
    private static final int PERMISSION_REQUEST = 0x4c42;

    // Exit 10: scripts not installed, 11: node/openclaw missing.
    private static final String DOCTOR =
            "test -f ~/lightbox/start-lightbox.sh && test -f ~/lightbox/lightbox-token.cjs || exit 10; "
            + "command -v node >/dev/null && command -v openclaw >/dev/null || exit 11; exit 0";
    // Gateway session: start-lightbox.sh exits early when the Gateway already runs.
    private static final String START =
            "cd ~/lightbox && LIGHTBOX_AUTO_PAIR=0 exec bash start-lightbox.sh";
    // Read-only: waits for start-lightbox.sh to save the key, never creates one.
    private static final String READ_TOKEN =
            "cd ~/lightbox || exit 10; for i in $(seq 60); do node lightbox-token.cjs --read && exit 0; sleep 1; done; exit 3";

    private final Activity activity;
    private final java.util.concurrent.ConcurrentLinkedQueue<String> events =
            new java.util.concurrent.ConcurrentLinkedQueue<>();
    private BroadcastReceiver receiver;
    private int requestCode = 1;
    private volatile boolean resumed;

    public LightBoxTermux(Activity activity) { this.activity = activity; }

    public int availability() {
        try {
            activity.getPackageManager().getPackageInfo(TERMUX, 0);
        } catch (PackageManager.NameNotFoundException missing) {
            return NOT_INSTALLED;
        }
        return activity.checkSelfPermission(PERMISSION) == PackageManager.PERMISSION_GRANTED
                ? READY : NO_PERMISSION;
    }

    public void requestPermission() {
        activity.requestPermissions(new String[] {PERMISSION}, PERMISSION_REQUEST);
    }

    public boolean doctor() { return run(DOCTOR, true, "doctor"); }

    public boolean startService() { return run(START, false, null); }

    public boolean fetchToken() { return run(READ_TOKEN, true, "token"); }

    /** Oldest pending event, or "" when none. */
    public String poll() {
        String event = events.poll();
        return event == null ? "" : event;
    }

    /** True once after the activity came back to the foreground. */
    public boolean consumeResumed() {
        boolean value = resumed;
        resumed = false;
        return value;
    }

    public void onResume() { resumed = true; }

    /** Copies the one-line setup command and opens Termux so it can be pasted. */
    public boolean copyAndOpenTermux(String command) {
        ClipboardManager clipboard = (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
        if (clipboard != null) {
            clipboard.setPrimaryClip(ClipData.newPlainText("ひかりのアトリエ セットアップ", command));
        }
        return openTermux();
    }

    public boolean openTermux() {
        Intent launch = activity.getPackageManager().getLaunchIntentForPackage(TERMUX);
        if (launch == null) return false;
        activity.startActivity(launch);
        return true;
    }

    public synchronized void stop() {
        if (receiver != null) {
            try { activity.unregisterReceiver(receiver); } catch (IllegalArgumentException ignored) {}
            receiver = null;
        }
    }

    private boolean run(String script, boolean background, String resultKind) {
        Intent intent = new Intent("com.termux.RUN_COMMAND");
        intent.setClassName(TERMUX, "com.termux.app.RunCommandService");
        intent.putExtra("com.termux.RUN_COMMAND_PATH", BASH);
        intent.putExtra("com.termux.RUN_COMMAND_ARGUMENTS", new String[] {"-lc", script});
        intent.putExtra("com.termux.RUN_COMMAND_WORKDIR", HOME);
        intent.putExtra("com.termux.RUN_COMMAND_BACKGROUND", background);
        // New terminal session for the Gateway, without leaving this app.
        intent.putExtra("com.termux.RUN_COMMAND_SESSION_ACTION", "1");
        if (resultKind != null) intent.putExtra("com.termux.RUN_COMMAND_PENDING_INTENT", resultIntent(resultKind));
        try {
            activity.startForegroundService(intent);
            return true;
        } catch (RuntimeException failed) {
            return false;
        }
    }

    private synchronized PendingIntent resultIntent(String kind) {
        if (receiver == null) {
            receiver = new BroadcastReceiver() {
                @Override public void onReceive(Context context, Intent intent) { handleResult(intent); }
            };
            activity.registerReceiver(receiver, new IntentFilter(ACTION_RESULT),
                    Context.RECEIVER_NOT_EXPORTED);
        }
        Intent target = new Intent(ACTION_RESULT).setPackage(activity.getPackageName())
                .putExtra("kind", kind);
        // Mutable: Termux fills in the "result" bundle. Distinct request codes keep
        // concurrent doctor/token requests from replacing each other.
        return PendingIntent.getBroadcast(activity, requestCode++, target,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE);
    }

    private void handleResult(Intent intent) {
        String kind = intent.getStringExtra("kind");
        if (kind == null) return;
        Bundle result = intent.getBundleExtra("result");
        if (result == null) { events.add(kind + ":error"); return; }
        String error = result.getString("errmsg", "");
        if (error != null && error.contains("allow-external-apps")) { events.add(kind + ":external"); return; }
        int exitCode = result.getInt("exitCode", -1);
        if ("doctor".equals(kind)) {
            events.add(kind + (exitCode == 0 ? ":ok" : exitCode == 10 ? ":scripts"
                    : exitCode == 11 ? ":tools" : ":error"));
            return;
        }
        String token = result.getString("stdout", "");
        token = token == null ? "" : token.trim();
        if (exitCode == 10) { events.add("token:scripts"); return; }
        if (exitCode != 0 || token.isEmpty()) { events.add("token:missing"); return; }
        Intent handover = new Intent();
        handover.putExtra(LightBoxCredentials.EXTRA_ENDPOINT, ENDPOINT);
        handover.putExtra(LightBoxCredentials.EXTRA_TOKEN, token);
        boolean stored = LightBoxCredentials.acceptProvisioning(activity, handover);
        boolean available = stored || !new LightBoxCredentials(activity).load(ENDPOINT).isEmpty();
        events.add(stored ? "token:stored" : available ? "token:kept" : "token:missing");
    }
}

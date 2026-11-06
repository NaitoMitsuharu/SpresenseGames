package com.lati.lightbox;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.view.Surface;
import java.util.Locale;
import java.util.function.BooleanSupplier;

/**
 * Phone sensors for the Light Box: breath and claps from the microphone, plus
 * ambient light, proximity and the magnetic field. UE polls one CSV line:
 * breath,claps,lux,near,magX,magY,magZ,hasLight,hasNear,hasMag,hasMic
 * (magnetic X/Y are already rotated to screen right/up; -1 lux = no sensor).
 */
public final class LightBoxSensors implements SensorEventListener {
    private static final int RATE = 16000;
    private static final int FRAME = 320;  // 20 ms

    private final Activity activity;
    private final BooleanSupplier voiceListening;
    private final SensorManager sensors;
    private volatile boolean running = false;
    private Thread micThread;

    private volatile float breath = 0f;
    private volatile int claps = 0;
    private volatile boolean micOpen = false;
    private volatile float lux = -1f;
    private volatile boolean near = false;
    private volatile float magX = 0f, magY = 0f, magZ = 0f;
    private boolean hasLight, hasNear, hasMag;
    private float nearRange = 5f;
    private boolean askedPermission = false;

    public LightBoxSensors(Activity activity, BooleanSupplier voiceListening) {
        this.activity = activity;
        this.voiceListening = voiceListening;
        this.sensors = (SensorManager) activity.getSystemService(Context.SENSOR_SERVICE);
    }

    public synchronized void start() {
        if (running) { return; }
        running = true;
        if (sensors != null) {
            Sensor light = sensors.getDefaultSensor(Sensor.TYPE_LIGHT);
            Sensor proximity = sensors.getDefaultSensor(Sensor.TYPE_PROXIMITY);
            Sensor magnet = sensors.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD);
            hasLight = light != null && sensors.registerListener(this, light, SensorManager.SENSOR_DELAY_UI);
            hasNear = proximity != null && sensors.registerListener(this, proximity, SensorManager.SENSOR_DELAY_UI);
            if (proximity != null) { nearRange = Math.max(0.5f, Math.min(5f, proximity.getMaximumRange())); }
            hasMag = magnet != null && sensors.registerListener(this, magnet, SensorManager.SENSOR_DELAY_GAME);
        }
        if (activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            if (!askedPermission) {
                askedPermission = true;
                activity.runOnUiThread(() -> activity.requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO }, 4418));
            }
        }
        micThread = new Thread(this::micLoop, "LightBoxMic");
        micThread.start();
    }

    public synchronized void stop() {
        if (!running) { return; }
        // An explicit stop (leaving the Light Box) also cancels a pending resume; pause() re-arms it.
        resumeOnForeground = false;
        running = false;
        if (sensors != null) { sensors.unregisterListener(this); }
        if (micThread != null) { micThread.interrupt(); micThread = null; }
        breath = 0f; near = false; lux = -1f;
    }

    private boolean resumeOnForeground = false;

    /** Activity left the foreground: never keep the microphone open off-screen. */
    public synchronized void pause() {
        boolean wasRunning = running;
        stop();
        resumeOnForeground = wasRunning;
    }

    public synchronized void resume() {
        if (resumeOnForeground) { resumeOnForeground = false; start(); }
    }

    public String poll() {
        return String.format(Locale.ROOT, "%.3f,%d,%.1f,%d,%.1f,%.1f,%.1f,%d,%d,%d,%d",
            breath, claps, lux, near ? 1 : 0, magX, magY, magZ,
            hasLight ? 1 : 0, hasNear ? 1 : 0, hasMag ? 1 : 0, micOpen ? 1 : 0);
    }

    @Override public void onSensorChanged(SensorEvent e) {
        switch (e.sensor.getType()) {
            case Sensor.TYPE_LIGHT: lux = e.values[0]; break;
            case Sensor.TYPE_PROXIMITY: near = e.values[0] < nearRange * 0.5f; break;
            case Sensor.TYPE_MAGNETIC_FIELD: {
                // Device axes -> screen right/up for the current rotation.
                float x = e.values[0], y = e.values[1];
                int rotation = activity.getWindowManager().getDefaultDisplay().getRotation();
                float sx = x, sy = y;
                if (rotation == Surface.ROTATION_90) { sx = -y; sy = x; }
                else if (rotation == Surface.ROTATION_180) { sx = -x; sy = -y; }
                else if (rotation == Surface.ROTATION_270) { sx = y; sy = -x; }
                magX = sx; magY = sy; magZ = e.values[2];
                break;
            }
            default: break;
        }
    }

    @Override public void onAccuracyChanged(Sensor sensor, int accuracy) { }

    private AudioRecord open() {
        if (activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) { return null; }
        try {
            int min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
            AudioRecord rec = new AudioRecord(MediaRecorder.AudioSource.UNPROCESSED, RATE,
                AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, Math.max(min, FRAME * 8));
            if (rec.getState() != AudioRecord.STATE_INITIALIZED) {
                rec.release();
                rec = new AudioRecord(MediaRecorder.AudioSource.MIC, RATE,
                    AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, Math.max(min, FRAME * 8));
            }
            if (rec.getState() != AudioRecord.STATE_INITIALIZED) { rec.release(); return null; }
            rec.startRecording();
            return rec;
        } catch (Exception error) {
            return null;
        }
    }

    private void micLoop() {
        short[] buffer = new short[FRAME];
        AudioRecord rec = null;
        float low = 0f, smoothBreath = 0f, background = -60f;
        int breathFrames = 0, loudFrames = 0;
        float previousDb = -90f;
        try {
            while (running && !Thread.currentThread().isInterrupted()) {
                // Speech recognition needs the microphone: step aside while it listens.
                if (voiceListening.getAsBoolean()) {
                    if (rec != null) { rec.stop(); rec.release(); rec = null; micOpen = false; }
                    breath = 0f;
                    Thread.sleep(100);
                    continue;
                }
                if (rec == null) {
                    rec = open();
                    micOpen = rec != null;
                    if (rec == null) { Thread.sleep(500); continue; }
                }
                int n = rec.read(buffer, 0, FRAME);
                if (n <= 0) { Thread.sleep(10); continue; }
                double total = 0, lowEnergy = 0;
                int peak = 0;
                for (int i = 0; i < n; ++i) {
                    float s = buffer[i] / 32768f;
                    low += 0.11f * (s - low);  // ~300 Hz one-pole low-pass
                    total += s * s;
                    lowEnergy += low * low;
                    peak = Math.max(peak, Math.abs(buffer[i]));
                }
                float rms = (float) Math.sqrt(total / n);
                float db = 20f * (float) Math.log10(Math.max(rms, 1e-5f));
                float lowRatio = total > 0 ? (float) (lowEnergy / total) : 0f;
                if (db < background + 6f) { background += 0.02f * (db - background); }

                // Clap: a sudden loud transient that is over within ~120 ms.
                // loudFrames > 0: inside a transient; < 0: refractory after one.
                if (loudFrames == 0 && peak > 16000 && db > previousDb + 14f) {
                    loudFrames = 1;
                } else if (loudFrames > 0) {
                    if (db > background + 20f) {
                        ++loudFrames;
                        if (loudFrames > 6) { loudFrames = -15; }  // too long: a shout, not a clap
                    } else {
                        ++claps; loudFrames = -15;
                    }
                } else if (loudFrames < 0) {
                    ++loudFrames;
                }
                previousDb = 0.7f * previousDb + 0.3f * db;

                // Breath: sustained, low-frequency dominated noise well above the room.
                boolean blowing = db > Math.max(-42f, background + 12f) && lowRatio > 0.55f;
                breathFrames = blowing ? Math.min(breathFrames + 1, 50) : Math.max(breathFrames - 2, 0);
                float target = breathFrames >= 4 ? Math.min(1f, Math.max(0f, (db - Math.max(-42f, background + 12f)) / 24f + 0.25f)) : 0f;
                smoothBreath += (target > smoothBreath ? 0.35f : 0.08f) * (target - smoothBreath);
                breath = smoothBreath;
            }
        } catch (InterruptedException ignored) {
        } finally {
            if (rec != null) { try { rec.stop(); } catch (Exception ignored) { } rec.release(); }
            micOpen = false; breath = 0f;
        }
    }
}

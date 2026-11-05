package com.lati.lightbox;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import java.util.ArrayList;
import org.json.JSONObject;

/** Android owns microphone permission and speech-to-text; UE polls short events. */
public final class LightBoxVoice {
    private final Activity activity;
    private SpeechRecognizer recognizer;
    private String event = "";
    private int generation = 0;
    private boolean active = false;

    public LightBoxVoice(Activity activity) { this.activity = activity; }

    private synchronized void publish(String kind, String text) {
        try {
            JSONObject value = new JSONObject();
            value.put("kind", kind); value.put("text", text);
            event = value.toString();
        } catch (Exception ignored) { }
    }

    public synchronized String poll() {
        String result = event; event = ""; return result;
    }

    private void release() {
        if (recognizer != null) {
            recognizer.cancel(); recognizer.destroy(); recognizer = null;
        }
        active = false;
    }

    public void start() {
        synchronized (this) { event = ""; }
        activity.runOnUiThread(() -> {
            final int request = ++generation;
            release();
            synchronized (this) { event = ""; }
            if (activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
                publish("error", "マイクを許可してから、もう一度マイクを押してください");
                activity.requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO }, 4417);
                return;
            }
            if (!SpeechRecognizer.isRecognitionAvailable(activity)) {
                publish("error", "日本語の音声認識サービスを有効にしてください。文字入力も使えます");
                return;
            }
            try {
                recognizer = SpeechRecognizer.createSpeechRecognizer(activity);
                recognizer.setRecognitionListener(new RecognitionListener() {
                    private boolean current() { return active && request == generation; }
                    private String words(Bundle results) {
                        ArrayList<String> list = results == null ? null : results.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                        return list != null && !list.isEmpty() ? list.get(0) : "";
                    }
                    @Override public void onReadyForSpeech(Bundle params) { }
                    @Override public void onBeginningOfSpeech() { }
                    @Override public void onRmsChanged(float rmsdB) { }
                    @Override public void onBufferReceived(byte[] buffer) { }
                    @Override public void onEndOfSpeech() { }
                    @Override public void onEvent(int type, Bundle params) { }
                    @Override public void onPartialResults(Bundle results) {
                        if (current()) { publish("partial", words(results)); }
                    }
                    @Override public void onResults(Bundle results) {
                        if (!current()) { return; }
                        String text = words(results);
                        publish(text.isEmpty() ? "error" : "result", text.isEmpty() ? "声を聞き取れませんでした。もう一度お試しください" : text);
                        // Ignore callbacks from this completed recognizer until UE disposes it.
                        active = false;
                    }
                    @Override public void onError(int code) {
                        if (!current()) { return; }
                        String message = "声を聞き取れませんでした。もう一度お試しください";
                        if (code == SpeechRecognizer.ERROR_NETWORK || code == SpeechRecognizer.ERROR_NETWORK_TIMEOUT) {
                            message = "音声認識の通信を確認してください";
                        } else if (code == SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS) {
                            message = "設定からマイクの使用を許可してください";
                        } else if (code == SpeechRecognizer.ERROR_LANGUAGE_NOT_SUPPORTED || code == SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE) {
                            message = "日本語の音声認識を設定してください";
                        }
                        publish("error", message); active = false;
                    }
                });
                Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
                intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
                intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, "ja-JP");
                intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true);
                intent.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1);
                active = true;
                recognizer.startListening(intent);
            } catch (Exception error) {
                release(); publish("error", "マイクを開始できませんでした。文字入力も使えます");
            }
        });
    }

    public void stop() {
        activity.runOnUiThread(() -> { ++generation; release(); });
    }

    public void pause() {
        // Called on the UI thread by GameActivity. Do not keep a live mic off-screen.
        if (active) {
            ++generation; release(); publish("error", "聞き取りを中断しました。もう一度話しかけてください");
        }
    }
}

package com.lati.lightbox.inputreview;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import java.util.Locale;

/** Test-only helper: plays a Japanese instruction through the device speaker. */
public final class AudioPromptActivity extends Activity {
    private TextToSpeech speech;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private static android.content.ClipData savedClip;
    private static boolean clipCaptured;
    private boolean clipboardHandled;
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if(!focused||clipboardHandled||!getIntent().getBooleanExtra("clipboard",false))return;
        clipboardHandled=true;
        android.content.ClipboardManager clipboard=(android.content.ClipboardManager)getSystemService(CLIPBOARD_SERVICE);
        if(getIntent().getBooleanExtra("restore",false)){
            if(clipCaptured){if(savedClip!=null)clipboard.setPrimaryClip(savedClip);else clipboard.clearPrimaryClip();}
        }else{
            if(!clipCaptured){savedClip=clipboard.getPrimaryClip();clipCaptured=true;}
            clipboard.setPrimaryClip(android.content.ClipData.newPlainText("Input review",getIntent().getStringExtra("text")));
        }
        startActivity(new Intent().setClassName("com.lati.spresensemaze","com.epicgames.unreal.GameActivity").addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
        finish();
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if(getIntent().getBooleanExtra("clipboard",false))return;
        final String text=getIntent().getStringExtra("text");
        if(text==null||text.length()>500){finish();return;}
        speech=new TextToSpeech(this,status->{
            if(status!=TextToSpeech.SUCCESS){android.util.Log.i("LightBoxInputReview","TTS unavailable");finish();return;}
            int language=speech.setLanguage(Locale.JAPAN);
            android.util.Log.i("LightBoxInputReview","Japanese TTS language status: "+language);
            speech.setSpeechRate(.85f);
            speech.setOnUtteranceProgressListener(new UtteranceProgressListener(){
                public void onStart(String id){android.util.Log.i("LightBoxInputReview","Audio prompt started");}
                public void onDone(String id){handler.post(()->{speech.shutdown();finish();});}
                public void onError(String id){handler.post(()->{speech.shutdown();finish();});}
            });
            Intent game=new Intent().setClassName("com.lati.spresensemaze","com.epicgames.unreal.GameActivity").addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
            startActivity(game);
            handler.postDelayed(()->speech.speak(text,TextToSpeech.QUEUE_FLUSH,null,"review"),4500);
        });
    }
}

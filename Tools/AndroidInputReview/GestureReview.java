package com.lati.lightbox.inputreview;

import android.app.Instrumentation;
import android.app.UiAutomation;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;

/** Injects a real two-finger pinch. Targets this helper, so the game is not restarted. */
public final class GestureReview extends Instrumentation {
    private Bundle arguments;
    @Override public void onCreate(Bundle args) { arguments=args;start(); }
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            float factor=Float.parseFloat(arguments.getString("factor","1.6"));
            if(factor<.4f||factor>2.5f)throw new IllegalArgumentException("Pinch factor out of range");
            UiAutomation ui=getUiAutomation();long down=SystemClock.uptimeMillis();
            send(ui,down,MotionEvent.ACTION_DOWN,1,160);
            send(ui,down,MotionEvent.ACTION_POINTER_DOWN|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),2,160);
            for(int i=1;i<=30;i++){SystemClock.sleep(16);send(ui,down,MotionEvent.ACTION_MOVE,2,160*(1+(factor-1)*i/30f));}
            send(ui,down,MotionEvent.ACTION_POINTER_UP|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),2,160*factor);
            send(ui,down,MotionEvent.ACTION_UP,1,160*factor);
            result.putString("result","Two-finger pinch delivered");finish(0,result);
        } catch(Exception e) { result.putString("error",e.toString());finish(1,result); }
    }
    private void send(UiAutomation ui,long down,int action,int count,float radius) {
        MotionEvent.PointerProperties[] properties=new MotionEvent.PointerProperties[count];
        MotionEvent.PointerCoords[] coords=new MotionEvent.PointerCoords[count];
        for(int i=0;i<count;i++){
            properties[i]=new MotionEvent.PointerProperties();properties[i].id=i;properties[i].toolType=MotionEvent.TOOL_TYPE_FINGER;
            coords[i]=new MotionEvent.PointerCoords();coords[i].x=1170+(i==0?-radius:radius);coords[i].y=540;coords[i].pressure=1;coords[i].size=.01f;
        }
        MotionEvent event=MotionEvent.obtain(down,SystemClock.uptimeMillis(),action,count,properties,coords,0,0,1,1,0,0,InputDevice.SOURCE_TOUCHSCREEN,0);
        try{if(!ui.injectInputEvent(event,true))throw new IllegalStateException("Touch injection rejected");}finally{event.recycle();}
    }
}

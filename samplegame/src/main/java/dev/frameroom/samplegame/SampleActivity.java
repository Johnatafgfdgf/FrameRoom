package dev.frameroom.samplegame;
import android.app.Activity;
import android.os.Bundle;
public final class SampleActivity extends Activity {
    @Override public void onCreate(Bundle b) { super.onCreate(b); setContentView(GameEntry.createView(this)); }
}

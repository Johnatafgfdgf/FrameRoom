package dev.frameroom.samplegame;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;
import dev.frameroom.sdk.GameScene;

/**
 * A tiny independent, open-source Android APK. The exact same compiled
 * GameEntry is run normally in SampleActivity or via FrameRoom's
 * DexClassLoader. It exposes a standalone Canvas view and a hosted Vulkan scene.
 */
public final class GameEntry {
    private GameEntry() {}

    public static View createView(Context context) {
        return new PlayView(context);
    }

    public static GameScene createVulkanScene(Context context) {
        return new HostedArcade(context);
    }

    private static final class PlayView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private float x = 0.5f, y = 0.45f;
        private float speed = 0.004f, direction = 1.0f;
        private int taps;
        private long lastFrameNs;

        PlayView(Context context) {
            super(context);
            setBackgroundColor(Color.rgb(14, 22, 38));
        }

        @Override protected void onDraw(Canvas c) {
            super.onDraw(c);
            long now = System.nanoTime();
            if (lastFrameNs != 0) {
                float dt = Math.min(48f, (now - lastFrameNs) / 1000000f);
                x += speed * direction * dt;
                if (x > 0.86f) { x = 0.86f; direction = -1f; }
                if (x < 0.14f) { x = 0.14f; direction = 1f; }
            }
            lastFrameNs = now;
            float w = getWidth(), h = getHeight();
            paint.setColor(Color.rgb(161, 141, 255));
            paint.setStyle(Paint.Style.FILL);
            c.drawCircle(w * x, h * y, Math.min(w, h) * 0.075f, paint);
            paint.setColor(Color.WHITE);
            paint.setTextSize(Math.max(24f, w * 0.052f));
            c.drawText("FrameRoom — jogo de teste", 30, 58, paint);
            paint.setTextSize(Math.max(18f, w * 0.030f));
            c.drawText("Toque para mover a esfera • Toques: " + taps, 30, 99, paint);
            c.drawText("Executado pelo APK independente (Canvas; SEM FG)", 30, 136, paint);
            if (isAttachedToWindow() && getVisibility() == VISIBLE) postInvalidateDelayed(16);
        }

        @Override public boolean onTouchEvent(MotionEvent event) {
            if (event.getAction() == MotionEvent.ACTION_DOWN) {
                x = Math.max(0.14f, Math.min(0.86f, event.getX() / Math.max(1f, getWidth())));
                y = Math.max(0.20f, Math.min(0.82f, event.getY() / Math.max(1f, getHeight())));
                direction *= -1;
                taps++;
                invalidate();
                return true;
            }
            return true;
        }
    }
}

package dev.frameroom.samplegame;

import android.content.Context;
import android.media.AudioManager;
import android.media.ToneGenerator;
import android.view.MotionEvent;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import dev.frameroom.sdk.GameScene;

/** A touch-controlled target game whose simulation lives in the independent APK. */
final class HostedArcade implements GameScene {
    private final File progress;
    private ToneGenerator audio;
    private int best;
    private int score;
    private float playerX = .45f;
    private float playerY = .67f;
    private float targetX = .13f;
    private float targetY = .42f;
    private float time;
    private long previousFrame;
    private boolean resumed;

    HostedArcade(Context context) {
        progress = new File(context.getFilesDir(), "arcade-best.txt");
        try { best = Math.max(0, Integer.parseInt(new String(Files.readAllBytes(progress.toPath()), java.nio.charset.StandardCharsets.UTF_8).trim())); }
        catch (Exception ignored) {}
    }

    @Override public String controlsLabel() { return "Arraste o quadrado verde até o alvo laranja."; }

    @Override public synchronized void resume() {
        previousFrame = 0;
        resumed = true;
        try { audio = new ToneGenerator(AudioManager.STREAM_MUSIC, 42); }
        catch (RuntimeException ignored) {}
    }

    @Override public synchronized void pause() {
        resumed = false;
        if (audio != null) {
            audio.release();
            audio = null;
        }
        try {
            File temporary = new File(progress.getParentFile(), "arcade-best.tmp");
            Files.write(temporary.toPath(), Integer.toString(best).getBytes(java.nio.charset.StandardCharsets.UTF_8));
            Files.move(temporary.toPath(), progress.toPath(), StandardCopyOption.REPLACE_EXISTING);
        } catch (Exception ignored) {}
    }

    @Override public synchronized void touch(float x, float y, int action) {
        if (action != MotionEvent.ACTION_DOWN && action != MotionEvent.ACTION_MOVE) return;
        playerX = Math.max(.055f, Math.min(.945f, x));
        playerY = Math.max(.17f, Math.min(.83f, y));
    }

    @Override public synchronized int render(long now, float[] rectangles) {
        if (resumed) {
            if (previousFrame != 0) time += Math.min(.06f, Math.max(0, (now - previousFrame) / 1_000_000_000f));
            previousFrame = now;
            targetX = .1f + .78f * triangle(time * .38f + score * .17f);
            targetY = .22f + .52f * triangle(time * .44f + score * .34f);
            if (Math.abs(targetX - playerX) < .075f && Math.abs(targetY - playerY) < .10f) {
                ++score;
                best = Math.max(best, score);
                if (audio != null) audio.startTone(ToneGenerator.TONE_PROP_BEEP, 90);
            }
        }
        int count = rect(rectangles, 0, 0, 0, 1, 1, .043f, .061f, .10f);
        for (int i = 0; i < 8; i++) {
            float x = (i / 8f + time * .22f) % 1f;
            count = rect(rectangles, count, x, .90f, .045f, .014f, .23f, .30f, .45f);
        }
        count = rect(rectangles, count, targetX - .050f, targetY - .065f, .10f, .13f, .98f, .63f, .40f);
        count = rect(rectangles, count, targetX - .024f, targetY - .032f, .048f, .064f, .25f, .13f, .16f);
        count = rect(rectangles, count, playerX - .049f, playerY - .055f, .098f, .11f, .55f, .90f, .79f);
        count = rect(rectangles, count, playerX - .020f, playerY - .015f, .040f, .027f, .13f, .32f, .34f);
        for (int i = 0; i < Math.min(8, best); ++i)
            count = rect(rectangles, count, .035f + .047f * i, .063f, .031f, .035f, .53f, .42f, .81f);
        for (int i = 0; i < Math.min(8, score); ++i)
            count = rect(rectangles, count, .035f + .047f * i, .063f, .031f, .035f, .97f, .66f, .39f);
        return count;
    }

    private static float triangle(float x) {
        return 1f - Math.abs(2f * (x - (float) Math.floor(x)) - 1f);
    }

    private static int rect(float[] data, int index, float x, float y, float width, float height,
                            float r, float g, float b) {
        int offset = index * FIELDS_PER_RECTANGLE;
        data[offset] = x;
        data[offset + 1] = y;
        data[offset + 2] = width;
        data[offset + 3] = height;
        data[offset + 4] = r;
        data[offset + 5] = g;
        data[offset + 6] = b;
        return index + 1;
    }
}

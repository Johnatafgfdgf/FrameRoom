package dev.frameroom;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import dev.frameroom.sdk.GameScene;

/** Owns a Vulkan surface and forwards input only to its cooperative scene. */
final class VulkanPlayerView extends LinearLayout implements SurfaceHolder.Callback {
    static { System.loadLibrary("lsfgprobe"); }
    private static native long nativeStart(Surface surface, int fps, boolean ahb, boolean live,
                                          String dllPath, boolean fg, GameScene scene);
    private static native void nativeStop(long id);
    private static native void nativeSetFrameGeneration(long id, boolean enabled);
    private static native String nativeStatus(long id, boolean detailed);

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final SurfaceView viewport;
    private final TextView status;
    private final GameScene scene;
    private final String dllPath;
    private final Button fgButton;
    private final Button modeButton;
    private final Button fpsButton;
    private final Runnable ticker = new Runnable() {
        @Override public void run() {
            if (!resumed) return;
            status.setText(nativeStatus(session, false));
            handler.postDelayed(this, 450);
        }
    };
    private long session;
    private int fps;
    private boolean resumed;
    private boolean live = true;
    private boolean ahb = false;
    private boolean fg;
    private boolean surfaceReady;

    VulkanPlayerView(Activity activity, String title, GameScene scene, int fps, boolean fg, boolean advancedModes) {
        super(activity);
        this.scene = scene;
        this.fps = fps;
        this.dllPath = DllStore.location(activity).isFile() ? DllStore.location(activity).getAbsolutePath() : null;
        this.fg = fg && dllPath != null;
        setOrientation(VERTICAL);
        setBackgroundColor(Ui.BG);
        LinearLayout header = Ui.row(activity);
        Ui.pad(header, 14, 4, 14, 4);
        Button back = Ui.button(activity, "‹", false, activity::finish);
        back.setContentDescription("Voltar");
        header.addView(back, new LinearLayout.LayoutParams(Ui.dp(activity, 48), -2));
        TextView label = Ui.text(activity, title, 19, Ui.TEXT, true);
        Ui.pad(label, 12, 0, 4, 0);
        header.addView(label, new LinearLayout.LayoutParams(0, -2, 1));
        addView(header);

        FrameLayout stage = new FrameLayout(activity);
        stage.setBackgroundColor(0xff060810);
        viewport = new SurfaceView(activity);
        viewport.getHolder().addCallback(this);
        if (scene != null) {
            viewport.setContentDescription(title + ". " + scene.controlsLabel());
            viewport.setOnTouchListener((view, event) -> {
                if (event.getActionMasked() == MotionEvent.ACTION_UP) view.performClick();
                return forwardTouch(event);
            });
        }
        stage.addView(viewport, new FrameLayout.LayoutParams(-1, -1));
        status = Ui.text(activity, "Abrindo Vulkan…", 13, Ui.TEXT, true);
        status.setBackground(Ui.shape(activity, 0xe6101418, 10, 0x99627079));
        Ui.pad(status, 10, 7, 10, 7);
        FrameLayout.LayoutParams overlay = new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.START);
        int margin = Ui.dp(activity, 10);
        overlay.setMargins(margin, margin, margin, margin);
        stage.addView(status, overlay);
        addView(stage, new LinearLayout.LayoutParams(-1, 0, 1));

        LinearLayout controls = Ui.column(activity);
        Ui.pad(controls, 14, 8, 14, 10);
        if (scene != null && !scene.controlsLabel().isEmpty()) {
            controls.addView(Ui.text(activity, scene.controlsLabel(), 13, Ui.MUTED, false));
            Ui.space(controls, 6);
        }
        LinearLayout first = Ui.row(activity);
        fgButton = Ui.button(activity, "", true, this::toggleFg);
        fpsButton = Ui.button(activity, "", false, this::changeFps);
        Ui.tabWeight(first, fgButton, 8);
        Ui.tabWeight(first, fpsButton, 0);
        controls.addView(first);
        Ui.space(controls, 6);
        LinearLayout second = Ui.row(activity);
        if (advancedModes) {
            modeButton = Ui.button(activity, "", false, this::changeMode);
            Ui.tabWeight(second, modeButton, 8);
        } else modeButton = null;
        Button copy = Ui.button(activity, "Copiar diagnóstico", false, this::copyDiagnostic);
        Ui.tabWeight(second, copy, 0);
        controls.addView(second);
        addView(controls);
        updateControls();
    }

    private boolean forwardTouch(MotionEvent event) {
        float w = viewport.getWidth();
        float h = viewport.getHeight();
        if (w <= 0 || h <= 0) return false;
        float sceneWidth = Math.min(w, h * 16f / 9f);
        float sceneHeight = Math.min(h, w * 9f / 16f);
        float x = (event.getX() - (w - sceneWidth) / 2f) / sceneWidth;
        float y = (event.getY() - (h - sceneHeight) / 2f) / sceneHeight;
        if (x >= 0 && x <= 1 && y >= 0 && y <= 1)
            scene.touch(x, y, event.getActionMasked());
        return true;
    }

    private void toggleFg() {
        if (dllPath == null || !live) return;
        fg = !fg;
        nativeSetFrameGeneration(session, fg);
        setSurfaceRate();
        updateControls();
    }
    private void changeFps() {
        fps = fps == 20 ? 30 : fps == 30 ? 45 : fps == 45 ? 60 : 20;
        updateControls();
        restart();
    }
    private void changeMode() {
        if (live) { live = false; ahb = false; }
        else if (!ahb) ahb = true;
        else { live = true; ahb = false; }
        updateControls();
        restart();
    }
    private void updateControls() {
        fgButton.setEnabled(live && dllPath != null);
        fgButton.setAlpha(fgButton.isEnabled() ? 1f : .55f);
        fgButton.setText(dllPath == null ? "Importe a DLL" : fg && live ? "Desligar FG" : "Ligar FG 2×");
        fpsButton.setText("Limite " + fps + " FPS");
        if (modeButton != null) modeButton.setText(live ? "Cena LSFG" : ahb ? "AHB duplo" : "Vulkan direto");
    }
    private void copyDiagnostic() {
        String info = "FrameRoom " + BuildConfig.VERSION_NAME + "\nAndroid " + Build.VERSION.RELEASE + " · " +
            Build.MODEL + "\nLimite original " + fps + " FPS\n\n" + nativeStatus(session, true);
        ClipboardManager clipboard = getContext().getSystemService(ClipboardManager.class);
        if (clipboard != null) {
            clipboard.setPrimaryClip(ClipData.newPlainText("Diagnóstico FrameRoom", info));
            Toast.makeText(getContext(), "Diagnóstico copiado", Toast.LENGTH_SHORT).show();
        }
    }
    private void setSurfaceRate() {
        Surface surface = viewport.getHolder().getSurface();
        if (!surface.isValid() || Build.VERSION.SDK_INT < 30) return;
        float rate = (fg && live) ? Math.min(fps * 2f, 120) : fps;
        try {
            surface.setFrameRate(rate, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
        } catch (IllegalArgumentException | IllegalStateException ignored) {}
    }
    private void restart() {
        stopSurface();
        if (!resumed || !surfaceReady) return;
        Surface surface = viewport.getHolder().getSurface();
        if (surface.isValid()) {
            setSurfaceRate();
            session = nativeStart(surface, fps, ahb, live, dllPath, fg, scene);
        }
    }
    private void stopSurface() {
        if (session != 0) {
            nativeStop(session);
            session = 0;
        }
    }
    @Override public void surfaceCreated(SurfaceHolder holder) {}
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        surfaceReady = width > 0 && height > 0;
        restart();
    }
    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        surfaceReady = false;
        stopSurface();
    }
    void resume() {
        if (resumed) return;
        resumed = true;
        if (scene != null) scene.resume();
        restart();
        handler.post(ticker);
    }
    void pause() {
        if (!resumed) return;
        resumed = false;
        handler.removeCallbacks(ticker);
        stopSurface();
        if (scene != null) scene.pause();
    }
    void close() {
        pause();
        if (scene != null) scene.close();
    }
}

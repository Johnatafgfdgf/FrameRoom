package dev.frameroom;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Native Vulkan swapchain demo. No MediaProjection, no generated frames. */
public final class VulkanDemoActivity extends Activity implements SurfaceHolder.Callback {
    static { System.loadLibrary("lsfgprobe"); }
    private static native void nativeStart(Surface surface, int fps, boolean ahb);
    private static native void nativeStop();
    private static native String nativeStatus();

    private final Handler handler = new Handler(Looper.getMainLooper());
    private SurfaceView surfaceView;
    private TextView status;
    private int targetFps = 30;
    private boolean useAhb;
    private boolean resumed;
    private final Runnable ticker = new Runnable() {
        @Override public void run() {
            if (status != null && resumed) status.setText(nativeStatus());
            if (resumed) handler.postDelayed(this, 500);
        }
    };

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(13, 16, 27));
        root.setPadding(16, 24, 16, 24);
        TextView title = new TextView(this);
        title.setText("Vulkan • teste direto");
        title.setTextSize(23);
        title.setTextColor(Color.WHITE);
        root.addView(title);
        TextView explainer = new TextView(this);
        explainer.setText("Modo direto: faixa roxa na swapchain. Modo AHB: imagem RGBA8 compartilhável, importada pelo Vulkan e copiada pela GPU para a tela. Nenhum deles usa LSFG nem MediaProjection.");
        explainer.setTextSize(14);
        explainer.setTextColor(Color.LTGRAY);
        explainer.setPadding(0, 10, 0, 12);
        root.addView(explainer);
        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        root.addView(surfaceView, new LinearLayout.LayoutParams(-1, 0, 1f));
        status = new TextView(this);
        status.setText("Aguardando superfície...");
        status.setTextColor(Color.WHITE);
        status.setPadding(0, 16, 0, 12);
        root.addView(status);
        Button fpsButton = new Button(this);
        fpsButton.setText("Meta de renderização: 30 FPS (alternar)");
        fpsButton.setAllCaps(false);
        fpsButton.setOnClickListener(v -> {
            targetFps = (targetFps == 30) ? 60 : 30;
            fpsButton.setText("Meta de renderização: " + targetFps + " FPS (alternar)");
            restartIfValid();
        });
        root.addView(fpsButton);
        Button modeButton = new Button(this);
        modeButton.setText("Modo: Vulkan direto (trocar para AHB)");
        modeButton.setAllCaps(false);
        modeButton.setOnClickListener(v -> {
            useAhb = !useAhb;
            modeButton.setText(useAhb ? "Modo: Vulkan + AHardwareBuffer (experimental)" :
                    "Modo: Vulkan direto (trocar para AHB)");
            restartIfValid();
        });
        root.addView(modeButton);
        setContentView(root);
    }

    private void restartIfValid() {
        nativeStop();
        if (resumed && surfaceView.getHolder().getSurface().isValid()) {
            nativeStart(surfaceView.getHolder().getSurface(), targetFps, useAhb);
        }
    }
    @Override public void surfaceCreated(SurfaceHolder holder) { restartIfValid(); }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) { restartIfValid(); }
    @Override public void surfaceDestroyed(SurfaceHolder holder) { nativeStop(); }
    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        handler.removeCallbacks(ticker);
        handler.post(ticker);
        if (surfaceView != null && surfaceView.getHolder().getSurface().isValid()) restartIfValid();
    }
    @Override protected void onPause() {
        resumed = false;
        handler.removeCallbacks(ticker);
        nativeStop();
        super.onPause();
    }
    @Override protected void onDestroy() {
        handler.removeCallbacks(ticker);
        nativeStop();
        super.onDestroy();
    }
}

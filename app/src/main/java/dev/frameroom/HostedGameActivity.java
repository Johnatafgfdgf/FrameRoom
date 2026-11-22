package dev.frameroom;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Bundle;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.lang.reflect.Method;

import dalvik.system.DexClassLoader;

/**
 * Experimental cooperative-APK runtime. NOT a virtual Android install.
 * Only runs our co-signed sample-game APK, never arbitrary third-party code.
 * Runs in :virtualgame (different PID, SAME UID as FrameRoom).
 */
public final class HostedGameActivity extends Activity {
    private static final String TRUSTED_SAMPLE = "dev.frameroom.samplegame";
    public static final String EXTRA_PACKAGE = "packageName";

    @Override public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        final String pkg = getIntent().getStringExtra(EXTRA_PACKAGE);
        showMessage("Iniciando jogo em processo separado...");
        if (!TRUSTED_SAMPLE.equals(pkg)) {
            showMessage("Este runtime só executa o jogo de demonstração assinado com o FrameRoom. " +
                    "Jogos Android comuns ainda não têm suporte de virtualização.");
            return;
        }
        // Avoid running untrusted bytecode with FrameRoom's identity.
        if (getPackageManager().checkSignatures(getPackageName(), pkg) != PackageManager.SIGNATURE_MATCH) {
            showMessage("Assinaturas diferentes. Instale o FrameRoom e o jogo de teste do MESMO build do GitHub. " +
                    "Não carregaremos código desconhecido.");
            return;
        }
        new Thread(() -> {
            try {
                final ApplicationInfo app = getPackageManager().getApplicationInfo(
                        pkg, PackageManager.GET_META_DATA);
                if (app.metaData == null ||
                        !"dev.frameroom.samplegame.GameEntry".equals(
                                app.metaData.getString("dev.frameroom.GAME_ENTRY"))) {
                    throw new SecurityException("APK sem protocolo cooperativo esperado.");
                }
                // Rehash every file and verify that the staged copy equals the installed,
                // signed APK. APK archives must be read-only on Android 14+.
                final String dexPath = GamePackageSnapshot.verifiedDexPath(this, pkg);
                final DexClassLoader loader = new DexClassLoader(dexPath, getCodeCacheDir()
                        .getAbsolutePath(), null, getClassLoader());
                final Class<?> entry = loader.loadClass(app.metaData.getString("dev.frameroom.GAME_ENTRY"));
                final Method create = entry.getMethod("createView", Context.class);
                runOnUiThread(() -> {
                    try {
                        // Its view renders here, not in the original APK's Activity.
                        // This separate process still shares FrameRoom's Android UID:
                        // it is NOT a hardened security boundary.
                        Object obj = create.invoke(null, this);
                        if (!(obj instanceof View)) throw new IllegalStateException("Plugin did not provide a View");
                        setContentView((View) obj);
                    } catch (Throwable error) {
                        showMessage("Falha ao iniciar código cooperativo: " + explain(error));
                    }
                });
            } catch (Throwable error) {
                runOnUiThread(() -> showMessage("Falha ao hospedar APK: " + explain(error)));
            }
        }, "trusted-apk-load").start();
    }

    private String explain(Throwable error) {
        if (error.getCause() != null) error = error.getCause();
        String message = error.getMessage();
        return error.getClass().getSimpleName() + (message == null ? "" : ": " + message);
    }

    private void showMessage(String message) {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(34, 64, 34, 30);
        root.setBackgroundColor(Color.rgb(12, 16, 27));
        TextView label = new TextView(this);
        label.setTextColor(Color.WHITE);
        label.setTextSize(18);
        label.setText(message);
        root.addView(label);
        setContentView(root);
    }
}

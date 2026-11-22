package dev.frameroom;

import android.app.Activity;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.view.View;
import android.widget.LinearLayout;

import java.lang.reflect.Method;
import dalvik.system.DexClassLoader;
import dev.frameroom.sdk.GameScene;

/** Loads the entrypoint of a verified, co-signed APK. Both processes share FrameRoom's UID. */
public final class HostedGameActivity extends Activity {
    static final String TRUSTED_SAMPLE = "dev.frameroom.samplegame";
    static final String EXTRA_PACKAGE = "packageName";
    static final String EXTRA_FG = "frameGeneration";
    static final String EXTRA_FPS = "originalFps";
    private static final String ENTRY_KEY = "dev.frameroom.GAME_ENTRY";
    private static final String VULKAN_API = "dev.frameroom.VULKAN_GAME_API";
    private VulkanPlayerView player;
    private boolean resumed;
    private boolean destroyed;

    private static boolean optsIn(String pkg, ApplicationInfo app) {
        if (app.metaData == null || app.metaData.getInt(VULKAN_API) != 1) return false;
        String entry = app.metaData.getString(ENTRY_KEY);
        return entry != null && entry.startsWith(pkg + ".");
    }

    static boolean supportsVulkan(Context context, String pkg) {
        if (pkg == null || !pkg.matches("[a-zA-Z0-9_]+(\\.[a-zA-Z0-9_]+)+")) return false;
        PackageManager pm = context.getPackageManager();
        try {
            ApplicationInfo app = pm.getApplicationInfo(pkg, PackageManager.GET_META_DATA);
            return pm.checkSignatures(context.getPackageName(), pkg) == PackageManager.SIGNATURE_MATCH &&
                optsIn(pkg, app);
        } catch (PackageManager.NameNotFoundException error) { return false; }
    }

    static boolean acceptsPackage(Context context, String pkg) {
        return supportsVulkan(context, pkg) || (TRUSTED_SAMPLE.equals(pkg) &&
            context.getPackageManager().checkSignatures(context.getPackageName(), pkg) == PackageManager.SIGNATURE_MATCH);
    }

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        String pkg = getIntent().getStringExtra(EXTRA_PACKAGE);
        showMessage("Preparando jogo hospedado…");
        if (!acceptsPackage(this, pkg)) {
            showMessage("É necessário um APK cooperativo com a mesma assinatura do FrameRoom.");
            return;
        }
        new Thread(() -> load(pkg), "frameroom-apk-load").start();
    }

    private void load(String pkg) {
        try {
            ApplicationInfo app = getPackageManager().getApplicationInfo(pkg, PackageManager.GET_META_DATA);
            String entrypoint = app.metaData == null ? null : app.metaData.getString(ENTRY_KEY);
            boolean vulkan = optsIn(pkg, app);
            if (!vulkan && !(TRUSTED_SAMPLE.equals(pkg) && "dev.frameroom.samplegame.GameEntry".equals(entrypoint)))
                throw new SecurityException("O APK não possui uma entrada cooperativa compatível.");
            GamePackageSnapshot.Snapshot snapshot = GamePackageSnapshot.latest(this, pkg);
            String version = getPackageManager().getPackageInfo(pkg, 0).versionName;
            int archiveCount = 1 + (app.splitSourceDirs == null ? 0 : app.splitSourceDirs.length);
            if (snapshot == null || !snapshot.version.equals(version) || snapshot.files != archiveCount)
                GamePackageSnapshot.prepare(this, pkg);
            String dexPath = GamePackageSnapshot.verifiedDexPath(this, pkg);
            DexClassLoader loader = new DexClassLoader(dexPath, getCodeCacheDir().getAbsolutePath(), null, getClassLoader());
            Class<?> entry = loader.loadClass(entrypoint);
            if (entry.getClassLoader() != loader)
                throw new SecurityException("A entrada precisa pertencer ao APK hospedado.");
            String title = getPackageManager().getApplicationLabel(app).toString();
            Method factory = entry.getMethod(vulkan ? "createVulkanScene" : "createView", Context.class);
            GameStorage storage = new GameStorage(this, pkg);
            if (vulkan) {
                GameScene scene = (GameScene) factory.invoke(null, storage);
                if (scene == null) throw new IllegalStateException("O APK não entregou uma cena.");
                runOnUiThread(() -> startScene(title, scene));
            } else runOnUiThread(() -> startCanvas(factory, storage));
        } catch (Exception | LinkageError error) {
            runOnUiThread(() -> { if (!destroyed) showMessage("Falha ao hospedar o APK\n" + describe(error)); });
        }
    }

    private void startScene(String title, GameScene scene) {
        if (destroyed || isFinishing()) { scene.close(); return; }
        player = new VulkanPlayerView(this, title, scene,
            getIntent().getIntExtra(EXTRA_FPS, 30), getIntent().getBooleanExtra(EXTRA_FG, false), false);
        LinearLayout root = Ui.screen(this);
        root.addView(player, new LinearLayout.LayoutParams(-1, -1));
        setContentView(root);
        if (resumed) player.resume();
    }

    private void startCanvas(Method factory, Context storage) {
        if (destroyed || isFinishing()) return;
        try { setContentView((View) factory.invoke(null, storage)); }
        catch (Exception | LinkageError error) { showMessage("O jogo de teste não abriu\n" + describe(error)); }
    }

    private static String describe(Throwable error) {
        Throwable cause = error.getCause() == null ? error : error.getCause();
        return cause.getMessage() == null ? cause.getClass().getSimpleName() : cause.getMessage();
    }
    private void showMessage(String message) {
        LinearLayout root = Ui.screen(this);
        LinearLayout body = Ui.column(this);
        Ui.pad(body, 18, 16, 18, 16);
        body.addView(Ui.button(this, "Voltar", false, this::finish));
        Ui.space(body, 14);
        body.addView(Ui.text(this, message, 18, Ui.TEXT, false));
        root.addView(body);
        setContentView(root);
    }
    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        if (player != null) player.resume();
    }
    @Override protected void onPause() {
        resumed = false;
        if (player != null) player.pause();
        super.onPause();
    }
    @Override protected void onDestroy() {
        destroyed = true;
        if (player != null) player.close();
        super.onDestroy();
    }
}

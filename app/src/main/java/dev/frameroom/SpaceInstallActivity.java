package dev.frameroom;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

public final class SpaceInstallActivity extends Activity {
    private static final int INSTALL_PACKAGE = 171;
    private LinearLayout body;
    private String pkg;
    private String label;
    private boolean waiting;
    private String error;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        pkg = getIntent().getStringExtra(SpaceProfile.PACKAGE);
        if (!SpaceProfile.isOwned(this) || !SpaceTarget.validPackage(pkg) || getPackageName().equals(pkg)) {
            finish();
            return;
        }
        label = getIntent().getStringExtra(SpaceProfile.APP_LABEL);
        if (state != null) {
            waiting = state.getBoolean("waiting");
            error = state.getString("error");
        }
        LinearLayout root = Ui.screen(this);
        body = Ui.column(this);
        Ui.pad(body, 18, 18, 18, 18);
        root.addView(Ui.scrolling(this, body));
        setContentView(root);
        render();
        if (state == null && !isInstalled()) install();
    }

    @Override protected void onResume() {
        super.onResume();
        if (body != null) render();
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putBoolean("waiting", waiting);
        state.putString("error", error);
    }

    private boolean isInstalled() {
        try {
            ApplicationInfo app = getPackageManager().getApplicationInfo(pkg, 0);
            return (app.flags & ApplicationInfo.FLAG_INSTALLED) != 0;
        } catch (PackageManager.NameNotFoundException missing) { return false; }
    }

    @SuppressWarnings("deprecation") // The documented package: route keeps the existing APK and splits intact.
    private void install() {
        if (waiting || isInstalled() || !SpaceProfile.isOwned(this)) return;
        Intent request = new Intent(Intent.ACTION_INSTALL_PACKAGE, Uri.fromParts("package", pkg, null))
            .putExtra(Intent.EXTRA_RETURN_RESULT, true);
        for (var handler : getPackageManager().queryIntentActivities(request, PackageManager.MATCH_DEFAULT_ONLY)) {
            if (handler.activityInfo == null ||
                (handler.activityInfo.applicationInfo.flags & ApplicationInfo.FLAG_SYSTEM) == 0) continue;
            request.setComponent(new ComponentName(handler.activityInfo.packageName, handler.activityInfo.name));
            try {
                startActivityForResult(request, INSTALL_PACKAGE);
                waiting = true;
                error = null;
                render();
            } catch (ActivityNotFoundException | SecurityException rejected) {
                error = "O Android bloqueou a instalação no perfil FrameRoom.";
                render();
            }
            return;
        }
        error = "Este Android não oferece a instalação de pacotes existentes no perfil.";
        render();
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != INSTALL_PACKAGE) return;
        waiting = false;
        if (!isInstalled()) error = result == RESULT_CANCELED
            ? "Instalação cancelada ou bloqueada pelo Android."
            : "O Android ainda não disponibilizou este aplicativo no perfil.";
        render();
    }

    private void render() {
        body.removeAllViews();
        boolean installed = isInstalled();
        body.addView(Ui.text(this, installed ? "Cópia pronta" : "Instalar no espaço", 24, Ui.TEXT, true));
        Ui.space(body, 8);
        body.addView(Ui.text(this, label == null ? pkg : label, 17, Ui.TEXT, true));
        Ui.space(body, 8);
        String detail = installed ? "Aplicativo disponível no perfil FrameRoom com dados separados."
            : error == null ? "Confirme a instalação pelo Android para ativar a cópia." : error;
        TextView information = Ui.text(this, detail, 14, !installed && error != null ? Ui.AMBER : Ui.MUTED, false);
        information.setAccessibilityLiveRegion(TextView.ACCESSIBILITY_LIVE_REGION_POLITE);
        body.addView(information);
        if (installed) {
            if (getPackageManager().getLaunchIntentForPackage(pkg) != null) {
                addButton("Abrir no espaço", true, () -> {
                    if (!ParallelSpaceActivity.openClone(this, pkg)) toast("O Android não conseguiu abrir esta cópia.");
                });
            }
        } else if (!waiting) {
            addButton("Instalar no perfil", true, this::install);
            if (error != null && !getPackageManager().canRequestPackageInstalls()) {
                addButton("Permissão de instalação do Android", false, () -> {
                    try {
                        startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                            Uri.fromParts("package", getPackageName(), null)));
                    } catch (ActivityNotFoundException | SecurityException rejected) {
                        toast("O Android não disponibilizou esta permissão neste perfil.");
                    }
                });
            }
        }
        addButton("Ver apps do espaço", false, () -> {
            startActivity(new Intent(this, ParallelSpaceActivity.class).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP));
            finish();
        });
    }

    private void addButton(String text, boolean primary, Runnable action) {
        Ui.space(body, 10);
        body.addView(Ui.button(this, text, primary, action));
    }

    private void toast(String text) { Toast.makeText(this, text, Toast.LENGTH_LONG).show(); }
}

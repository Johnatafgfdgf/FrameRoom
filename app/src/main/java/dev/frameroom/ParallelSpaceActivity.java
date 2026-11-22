package dev.frameroom;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;

public final class ParallelSpaceActivity extends Activity {
    private static final int CREATE_PROFILE = 110;
    private static final int PICK_APP = 111;
    private LinearLayout body;
    private String message;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) message = state.getString("message");
        LinearLayout root = Ui.screen(this);
        LinearLayout header = Ui.row(this);
        Ui.pad(header, 18, 10, 18, 8);
        Button back = Ui.button(this, "‹", false, this::finish);
        back.setContentDescription("Voltar");
        header.addView(back, new LinearLayout.LayoutParams(Ui.dp(this, 48), -2));
        TextView title = Ui.text(this, "Espaço paralelo", 24, Ui.TEXT, true);
        Ui.pad(title, 12, 0, 0, 0);
        header.addView(title);
        root.addView(header);
        body = Ui.column(this);
        Ui.pad(body, 18, 10, 18, 18);
        root.addView(Ui.scrolling(this, body), new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root);
    }

    @Override protected void onResume() {
        super.onResume();
        render();
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putString("message", message);
    }

    private void render() {
        body.removeAllViews();
        if (SpaceProfile.isOwned(this)) showWorkApps();
        else showPersonal();
        if (message != null) {
            Ui.space(body, 10);
            TextView status = Ui.text(this, message, 14, Ui.AMBER, false);
            status.setAccessibilityLiveRegion(TextView.ACCESSIBILITY_LIVE_REGION_POLITE);
            body.addView(status);
        }
    }

    private void showPersonal() {
        boolean ready = SpaceProfile.canOpen(this);
        LinearLayout panel = Ui.column(this);
        panel.setBackground(Ui.shape(this, Ui.PANEL, 18, Ui.OUTLINE));
        Ui.pad(panel, 16, 16, 16, 16);
        panel.addView(Ui.text(this, ready ? "Perfil FrameRoom disponível" : "Seu segundo espaço", 19, Ui.TEXT, true));
        Ui.space(panel, 6);
        panel.addView(Ui.text(this, "As cópias abrem num perfil do Android com contas e dados próprios.",
            14, Ui.MUTED, false));
        body.addView(panel);
        if (ready) {
            String preferred = getIntent().getStringExtra(SpaceProfile.PACKAGE);
            if (SpaceTarget.validPackage(preferred)) {
                String label = label(preferred);
                addButton("Clonar " + label, true, () -> cloneApp(preferred, label));
            }
            addButton("Escolher aplicativo", preferred == null, () -> {
                startActivityForResult(new Intent(this, AppPickerActivity.class)
                    .putExtra(AppPickerActivity.EXTRA_CLONE_PICK, true), PICK_APP);
            });
            addButton("Abrir espaço", false, () -> forward(SpaceProfile.openIntent(this)));
        } else {
            Intent provision = SpaceProfile.provision(this);
            if (provision != null) {
                addButton("Criar espaço", true, () -> {
                    message = null;
                    try { startActivityForResult(provision, CREATE_PROFILE); }
                    catch (ActivityNotFoundException | SecurityException error) {
                        setMessage("A configuração de um perfil de trabalho está indisponível neste Android.");
                    }
                });
            } else {
                Ui.space(body, 12);
                body.addView(Ui.text(this, "O Android não liberou um novo perfil. Se o perfil FrameRoom já existe, ative o modo trabalho e volte aqui.",
                    14, Ui.MUTED, false));
            }
        }
    }

    private void showWorkApps() {
        body.addView(Ui.text(this, "Perfil FrameRoom", 21, Ui.TEXT, true));
        Ui.space(body, 5);
        body.addView(Ui.text(this, "As cópias usam seus próprios dados. Apps deste perfil rodam sem Frame Generation.",
            14, Ui.MUTED, false));
        Ui.space(body, 14);
        PackageManager packages = getPackageManager();
        Intent filter = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        var apps = new ArrayList<>(packages.queryIntentActivities(filter, 0));
        apps.sort(Comparator.comparing(item -> item.loadLabel(packages).toString(), String.CASE_INSENSITIVE_ORDER));
        var shown = new HashSet<String>();
        for (ResolveInfo app : apps) {
            if (app.activityInfo == null) continue;
            String pkg = app.activityInfo.packageName;
            if (getPackageName().equals(pkg) || !shown.add(pkg)) continue;
            LinearLayout row = Ui.row(this);
            row.setBackground(Ui.shape(this, Ui.PANEL, 15, Ui.OUTLINE));
            Ui.pad(row, 12, 8, 10, 8);
            ImageView icon = new ImageView(this);
            icon.setImageDrawable(app.loadIcon(packages));
            icon.setImportantForAccessibility(ImageView.IMPORTANT_FOR_ACCESSIBILITY_NO);
            row.addView(icon, new LinearLayout.LayoutParams(Ui.dp(this, 40), Ui.dp(this, 40)));
            CharSequence label = app.loadLabel(packages);
            TextView name = Ui.text(this, label, 16, Ui.TEXT, true);
            Ui.pad(name, 10, 0, 6, 0);
            row.addView(name, new LinearLayout.LayoutParams(0, -2, 1));
            Button open = Ui.button(this, "Abrir", true, () -> {
                if (!openClone(this, pkg)) setMessage("Esta cópia não tem uma tela que o Android possa abrir.");
            });
            open.setContentDescription("Abrir " + label + " no espaço paralelo");
            row.addView(open);
            row.setOnLongClickListener(view -> {
                startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", pkg, null)));
                return true;
            });
            body.addView(row);
            Ui.space(body, 8);
        }
        if (shown.isEmpty()) body.addView(Ui.text(this, "Escolha um aplicativo para clonar na edição pessoal do FrameRoom.",
            14, Ui.MUTED, false));
    }

    static boolean openClone(Activity activity, String pkg) {
        if (!SpaceProfile.isOwned(activity) || !SpaceTarget.validPackage(pkg)) return false;
        Intent launch = activity.getPackageManager().getLaunchIntentForPackage(pkg);
        if (launch == null) return false;
        try { activity.startActivity(launch); return true; }
        catch (ActivityNotFoundException | SecurityException error) { return false; }
    }

    private void addButton(String label, boolean primary, Runnable action) {
        Ui.space(body, 10);
        body.addView(Ui.button(this, label, primary, action));
    }

    private String label(String pkg) {
        PackageManager packages = getPackageManager();
        try {
            ApplicationInfo app = packages.getApplicationInfo(pkg, 0);
            return packages.getApplicationLabel(app).toString();
        } catch (PackageManager.NameNotFoundException error) { return pkg; }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == CREATE_PROFILE) {
            if (result != RESULT_OK && !SpaceProfile.canOpen(this))
                setMessage("O Android não concluiu a criação do perfil.");
            else render();
        }
        if (request == PICK_APP && result == RESULT_OK && data != null) {
            String pkg = data.getStringExtra(SpaceProfile.PACKAGE);
            if (SpaceTarget.validPackage(pkg)) cloneApp(pkg, label(pkg));
        }
    }

    private void cloneApp(String pkg, String label) {
        if (SpaceProfile.isOwned(this) || getPackageName().equals(pkg)) return;
        if (getPackageManager().getLaunchIntentForPackage(pkg) == null) {
            setMessage("O aplicativo escolhido não está disponível no perfil pessoal.");
            return;
        }
        setMessage(null);
        forward(SpaceProfile.cloneIntent(this, pkg, label));
    }

    private void forward(Intent intent) {
        try { SpaceProfile.forward(this, intent); }
        catch (IllegalStateException | ActivityNotFoundException | SecurityException error) {
            Toast.makeText(this, "O Android bloqueou a abertura do perfil FrameRoom.", Toast.LENGTH_LONG).show();
            setMessage("Confira se o perfil de trabalho está ativo e tente novamente.");
        }
    }

    private void setMessage(String text) {
        message = text;
        render();
    }
}

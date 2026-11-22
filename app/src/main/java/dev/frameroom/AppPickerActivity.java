package dev.frameroom;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

public final class AppPickerActivity extends Activity {
    private record InstalledApp(String pkg, String label, ResolveInfo launcher) {}
    private final List<InstalledApp> apps = new ArrayList<>();
    private LinearLayout list;
    private PackageManager packages;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        packages = getPackageManager();
        LinearLayout root = Ui.screen(this);
        LinearLayout body = Ui.column(this);
        Ui.pad(body, 18, 12, 18, 10);
        LinearLayout heading = Ui.row(this);
        Button back = Ui.button(this, "‹", false, this::finish);
        back.setContentDescription("Voltar");
        heading.addView(back, new LinearLayout.LayoutParams(Ui.dp(this, 48), -2));
        TextView title = Ui.text(this, "Adicionar aplicativo", 23, Ui.TEXT, true);
        Ui.pad(title, 11, 0, 0, 0);
        heading.addView(title);
        body.addView(heading);
        Ui.space(body, 12);
        EditText search = Ui.search(this, "Pesquisar apps instalados");
        body.addView(search);
        Ui.space(body, 10);
        list = Ui.column(this);
        body.addView(Ui.scrolling(this, list), new LinearLayout.LayoutParams(-1, 0, 1));
        root.addView(body, new LinearLayout.LayoutParams(-1, -1));
        root.setFocusableInTouchMode(true);
        setContentView(root);
        root.requestFocus();

        Set<String> seen = new HashSet<>();
        Intent filter = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        for (ResolveInfo item : packages.queryIntentActivities(filter, 0)) {
            if (item.activityInfo == null) continue;
            String pkg = item.activityInfo.packageName;
            if (!getPackageName().equals(pkg) && seen.add(pkg))
                apps.add(new InstalledApp(pkg, item.loadLabel(packages).toString(), item));
        }
        apps.sort(Comparator.comparing(item -> item.label.toLowerCase(Locale.ROOT)));
        search.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence value, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence value, int start, int before, int count) { showApps(value.toString()); }
            @Override public void afterTextChanged(Editable value) {}
        });
        showApps("");
    }

    private void showApps(String query) {
        list.removeAllViews();
        String term = query.trim().toLowerCase(Locale.ROOT);
        Set<String> added = AppLibrary.all(this);
        for (InstalledApp app : apps) {
            if (!app.label.toLowerCase(Locale.ROOT).contains(term) &&
                !app.pkg.toLowerCase(Locale.ROOT).contains(term)) continue;
            LinearLayout line = Ui.row(this);
            line.setBackground(Ui.shape(this, Ui.PANEL, 13, Ui.OUTLINE));
            Ui.pad(line, 10, 8, 10, 8);
            ImageView icon = new ImageView(this);
            icon.setImageDrawable(app.launcher.loadIcon(packages));
            icon.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            line.addView(icon, new LinearLayout.LayoutParams(Ui.dp(this, 40), Ui.dp(this, 40)));
            LinearLayout caption = Ui.column(this);
            Ui.pad(caption, 10, 2, 8, 2);
            caption.addView(Ui.text(this, app.label, 15, Ui.TEXT, true));
            if (HostedGameActivity.supportsVulkan(this, app.pkg))
                caption.addView(Ui.text(this, "Execução hospedada", 12, Ui.ACCENT, false));
            line.addView(caption, new LinearLayout.LayoutParams(0, -2, 1));
            boolean present = added.contains(app.pkg);
            Button add = Ui.button(this, present ? "Adicionado" : "Adicionar", !present, () -> {
                AppLibrary.add(this, app.pkg);
                setResult(RESULT_OK);
                finish();
            });
            add.setContentDescription((present ? "Na biblioteca: " : "Adicionar ") + app.label);
            add.setEnabled(!present);
            add.setAlpha(present ? .60f : 1f);
            line.addView(add);
            list.addView(line);
            Ui.space(list, 8);
        }
        if (list.getChildCount() == 0)
            list.addView(Ui.text(this, "Nenhum aplicativo encontrado.", 14, Ui.MUTED, false));
    }
}

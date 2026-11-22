package dev.frameroom;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Color;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Button;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.HashSet;
import java.util.Set;
import java.util.Locale;

public class AppPickerActivity extends Activity {
    private LinearLayout list;
    private final List<ResolveInfo> apps = new ArrayList<>();
    private PackageManager pm;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        pm = getPackageManager();
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(20, 30, 20, 18);
        root.setBackgroundColor(Color.rgb(12, 16, 27));
        TextView title = new TextView(this);
        title.setText("Adicionar aplicativo");
        title.setTextSize(24);
        title.setTextColor(Color.WHITE);
        root.addView(title);
        TextView notice = new TextView(this);
        notice.setText("Adiciona o jogo à lista. Você poderá preparar uma cópia privada dos APKs, mas não jogá-la em espaço paralelo nesta versão.");
        notice.setTextColor(Color.LTGRAY);
        notice.setPadding(0, 10, 0, 14);
        root.addView(notice);
        EditText search = new EditText(this);
        search.setSingleLine(true);
        search.setHint("Pesquisar aplicativos");
        search.setTextColor(Color.WHITE);
        search.setHintTextColor(Color.GRAY);
        root.addView(search);
        ScrollView scroll = new ScrollView(this);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root);
        Intent filter = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        Set<String> seen = new HashSet<>();
        for (ResolveInfo item : pm.queryIntentActivities(filter, 0)) {
            if (item.activityInfo != null && !getPackageName().equals(item.activityInfo.packageName)
                    && seen.add(item.activityInfo.packageName)) apps.add(item);
        }
        apps.sort(Comparator.comparing(a -> a.loadLabel(pm).toString().toLowerCase(Locale.ROOT)));
        render("");
        search.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) { render(s.toString()); }
            @Override public void afterTextChanged(Editable e) {}
        });
    }
    private void render(String query) {
        list.removeAllViews();
        String filter = query.toLowerCase(Locale.ROOT).trim();
        Set<String> alreadyAdded = AppLibrary.all(this);
        for (ResolveInfo r : apps) {
            String label = r.loadLabel(pm).toString();
            String pkg = r.activityInfo.packageName;
            if (!label.toLowerCase(Locale.ROOT).contains(filter) && !pkg.toLowerCase(Locale.ROOT).contains(filter)) continue;
            Button button = new Button(this);
            button.setAllCaps(false);
            button.setText(label + "\n" + pkg + (alreadyAdded.contains(pkg) ? "\n(na biblioteca)" : ""));
            button.setCompoundDrawablesWithIntrinsicBounds(r.loadIcon(pm), null, null, null);
            button.setCompoundDrawablePadding(18);
            button.setOnClickListener(v -> {
                AppLibrary.add(this, pkg);
                setResult(RESULT_OK);
                finish();
            });
            list.addView(button);
        }
    }
}

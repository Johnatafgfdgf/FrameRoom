package dev.frameroom;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final int PICK_DLL = 301;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private LinearLayout root;
    private LinearLayout list;
    private TextView dllLabel;
    private TextView engineLabel;
    private String engineReport = "";
    private boolean destroyed;
    private boolean busy;
    private int page;
    private String query = "";

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (SpaceProfile.isOwned(this)) {
            openParallelSpace(null);
            finish();
            return;
        }
        if (state != null) page = state.getInt("tab", 0);
        root = Ui.screen(this);
        root.setFocusableInTouchMode(true);
        setContentView(root);
        renderPage();
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        state.putInt("tab", page);
        super.onSaveInstanceState(state);
    }

    @Override protected void onResume() {
        super.onResume();
        if (page == 0 && list != null) renderGames();
    }

    private void selectPage(int tab) {
        if (page == tab) return;
        page = tab;
        renderPage();
    }

    private void renderPage() {
        root.removeAllViews();
        list = null;
        dllLabel = null;
        engineLabel = null;
        LinearLayout header = Ui.row(this);
        Ui.pad(header, 18, 12, 18, 11);
        ImageView mark = new ImageView(this);
        mark.setImageResource(dev.frameroom.R.drawable.ic_frameroom);
        header.addView(mark, new LinearLayout.LayoutParams(Ui.dp(this, 39), Ui.dp(this, 39)));
        TextView wordmark = Ui.text(this, "FrameRoom", 24, Ui.TEXT, true);
        Ui.pad(wordmark, 11, 0, 0, 0);
        header.addView(wordmark, new LinearLayout.LayoutParams(0, -2, 1));
        if (page == 2) header.addView(Ui.text(this, "v" + BuildConfig.VERSION_NAME, 13, Ui.MUTED, false));
        root.addView(header);

        LinearLayout body = Ui.column(this);
        Ui.pad(body, 18, 8, 18, 22);
        if (page == 0) buildLibrary(body);
        if (page == 1) buildEngine(body);
        if (page == 2) buildDiagnostics(body);
        ScrollView scroll = Ui.scrolling(this, body);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));

        LinearLayout tabs = Ui.row(this);
        Ui.pad(tabs, 18, 6, 18, 12);
        String[] titles = {"Jogos", "Motor", "Avançado"};
        for (int i = 0; i < titles.length; i++) {
            final int tab = i;
            Button button = Ui.button(this, titles[i], tab == page, () -> selectPage(tab));
            button.setSelected(tab == page);
            Ui.tabWeight(tabs, button, i == titles.length - 1 ? 0 : 7);
        }
        root.addView(tabs);
        root.requestFocus();
    }

    private void buildLibrary(LinearLayout body) {
        LinearLayout hero = Ui.column(this);
        GradientDrawable gradient = new GradientDrawable(GradientDrawable.Orientation.TL_BR,
            new int[]{0xff283928, 0xff1c2928, 0xff202832});
        gradient.setCornerRadius(Ui.dp(this, 22));
        gradient.setStroke(Ui.dp(this, 1), 0xff405346);
        hero.setBackground(gradient);
        Ui.pad(hero, 17, 17, 17, 16);
        hero.addView(Ui.text(this, "Biblioteca de jogos", 26, Ui.TEXT, true));
        Ui.space(hero, 6);
        boolean dll = DllStore.location(this).isFile();
        hero.addView(Ui.text(this, dll ? "Lossless.dll importada" : "Importe a Lossless.dll para usar o FG.",
                14, dll ? Ui.ACCENT : Ui.MUTED, false));
        Ui.space(hero, 14);
        hero.addView(Ui.button(this, "Espaço paralelo", true, () -> openParallelSpace(null)));
        Ui.space(hero, 8);
        hero.addView(Ui.button(this, "+  Adicionar aplicativo", false,
            () -> startActivity(new Intent(this, AppPickerActivity.class))));
        body.addView(hero);
        Ui.space(body, 18);

        EditText search = Ui.search(this, "Pesquisar na biblioteca");
        search.setText(query);
        search.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence value, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence value, int start, int before, int count) {
                query = value.toString();
                renderGames();
            }
            @Override public void afterTextChanged(Editable value) {}
        });
        body.addView(search);
        Ui.space(body, 12);
        list = Ui.column(this);
        body.addView(list);
        renderGames();
    }

    private void renderGames() {
        list.removeAllViews();
        String term = query.trim().toLowerCase(Locale.ROOT);
        List<String> packages = new ArrayList<>(AppLibrary.all(this));
        packages.remove(HostedGameActivity.TRUSTED_SAMPLE);
        if ("arena frameroom".contains(term) || HostedGameActivity.TRUSTED_SAMPLE.contains(term))
            list.addView(arenaCard());
        packages.sort(Comparator.comparing(pkg -> label(pkg).toLowerCase(Locale.ROOT)));
        for (String pkg : packages) {
            String name = label(pkg);
            if (!name.toLowerCase(Locale.ROOT).contains(term) && !pkg.toLowerCase(Locale.ROOT).contains(term)) continue;
            boolean supported = HostedGameActivity.supportsVulkan(this, pkg);
            GamePackageSnapshot.Snapshot snapshot = GamePackageSnapshot.latest(this, pkg);
            LinearLayout card = Ui.column(this);
            card.setBackground(Ui.shape(this, Ui.PANEL, 16, Ui.OUTLINE));
            Ui.pad(card, 12, 12, 12, 10);
            LinearLayout line = Ui.row(this);
            ImageView icon = gameIcon(pkg);
            line.addView(icon, new LinearLayout.LayoutParams(Ui.dp(this, 48), Ui.dp(this, 48)));
            LinearLayout title = Ui.column(this);
            Ui.pad(title, 11, 0, 2, 0);
            title.addView(Ui.text(this, name, 17, Ui.TEXT, true));
            String state = supported ? (snapshot == null ? "Compatível · prepara ao jogar" : "Compatível · cópia pronta")
                : "Espaço paralelo disponível para apps compatíveis";
            title.addView(Ui.text(this, state, 13, supported ? Ui.ACCENT : Ui.MUTED, false));
            line.addView(title, new LinearLayout.LayoutParams(0, -2, 1));
            card.addView(line);
            Ui.space(card, 8);
            LinearLayout actions = Ui.row(this);
            if (supported) Ui.tabWeight(actions, Ui.button(this, "Jogar", true, () -> play(pkg)), 7);
            else Ui.tabWeight(actions, Ui.button(this, "Clonar", true, () -> openParallelSpace(pkg)), 7);
            Ui.tabWeight(actions, Ui.button(this, supported ? "Configurar" : "Detalhes", false,
                    () -> gameDetails(pkg)), 0);
            card.addView(actions);
            list.addView(card);
            Ui.space(list, 10);
        }
        if (list.getChildCount() == 0) {
            TextView empty = Ui.text(this, packages.isEmpty() ? "Seus aplicativos adicionados aparecem aqui."
                : "Nenhum aplicativo corresponde à pesquisa.", 14, Ui.MUTED, false);
            Ui.pad(empty, 4, 15, 4, 10);
            list.addView(empty);
        }
    }

    private View arenaCard() {
        LinearLayout card = Ui.row(this);
        card.setBackground(Ui.shape(this, Ui.PANEL, 16, Ui.OUTLINE));
        Ui.pad(card, 12, 10, 12, 10);
        card.addView(gameIcon(HostedGameActivity.TRUSTED_SAMPLE),
            new LinearLayout.LayoutParams(Ui.dp(this, 42), Ui.dp(this, 42)));
        LinearLayout title = Ui.column(this);
        Ui.pad(title, 10, 0, 7, 0);
        title.addView(Ui.text(this, "Arena FrameRoom", 16, Ui.TEXT, true));
        TextView settings = Ui.text(this, "Configurar FPS e FG", 13, Ui.ACCENT, false);
        settings.setOnClickListener(view -> gameDetails(HostedGameActivity.TRUSTED_SAMPLE));
        settings.setMinHeight(Ui.dp(this, 30));
        title.addView(settings);
        card.addView(title, new LinearLayout.LayoutParams(0, -2, 1));
        card.addView(Ui.button(this, "Jogar", true, () -> play(HostedGameActivity.TRUSTED_SAMPLE)));
        LinearLayout wrapper = Ui.column(this);
        wrapper.addView(card);
        Ui.space(wrapper, 10);
        return wrapper;
    }

    private ImageView gameIcon(String pkg) {
        ImageView icon = new ImageView(this);
        try { icon.setImageDrawable(getPackageManager().getApplicationIcon(pkg)); }
        catch (PackageManager.NameNotFoundException missing) {
            icon.setImageResource(dev.frameroom.R.drawable.ic_frameroom);
        }
        icon.setBackground(Ui.shape(this, 0xff273035, 12, 0));
        icon.setClipToOutline(true);
        icon.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        return icon;
    }

    private String label(String pkg) {
        PackageManager pm = getPackageManager();
        try { return pm.getApplicationLabel(pm.getApplicationInfo(pkg, 0)).toString(); }
        catch (PackageManager.NameNotFoundException error) { return pkg; }
    }

    private void play(String pkg) {
        if (busy) { toast("Aguarde a operação atual."); return; }
        GameSettings settings = new GameSettings(this, pkg);
        Intent intent = new Intent(this, HostedGameActivity.class);
        intent.putExtra(HostedGameActivity.EXTRA_PACKAGE, pkg);
        intent.putExtra(HostedGameActivity.EXTRA_EMBEDDED, HostedGameActivity.TRUSTED_SAMPLE.equals(pkg));
        intent.putExtra(HostedGameActivity.EXTRA_FG, settings.enabled());
        intent.putExtra(HostedGameActivity.EXTRA_FPS, settings.fps());
        startActivity(intent);
    }

    private void openParallelSpace(String pkg) {
        Intent space = new Intent(this, ParallelSpaceActivity.class);
        if (pkg != null) space.putExtra(SpaceProfile.PACKAGE, pkg);
        startActivity(space);
    }

    private void gameDetails(String pkg) {
        Dialog sheet = new Dialog(this);
        sheet.requestWindowFeature(Window.FEATURE_NO_TITLE);
        LinearLayout content = Ui.column(this);
        content.setBackground(Ui.shape(this, Ui.BG, 22, Ui.OUTLINE));
        Ui.pad(content, 18, 16, 18, 20);
        LinearLayout header = Ui.row(this);
        header.addView(Ui.text(this, label(pkg), 23, Ui.TEXT, true),
                new LinearLayout.LayoutParams(0, -2, 1));
        Button close = Ui.button(this, "×", false, sheet::dismiss);
        close.setContentDescription("Fechar");
        header.addView(close, new LinearLayout.LayoutParams(Ui.dp(this, 48), -2));
        content.addView(header);
        content.addView(Ui.text(this, pkg, 13, Ui.MUTED, false));
        Ui.space(content, 12);
        GameSettings settings = new GameSettings(this, pkg);
        GamePackageSnapshot.Snapshot snapshot = GamePackageSnapshot.latest(this, pkg);
        boolean arena = HostedGameActivity.TRUSTED_SAMPLE.equals(pkg);
        boolean supported = arena || HostedGameActivity.supportsVulkan(this, pkg);
        if (supported) {
            Switch fg = new Switch(this);
            fg.setText("Frame Generation 2× ao iniciar");
            fg.setTextColor(Ui.TEXT);
            fg.setMinHeight(Ui.dp(this, 48));
            fg.setChecked(settings.enabled() && DllStore.location(this).isFile());
            fg.setEnabled(DllStore.location(this).isFile());
            fg.setOnCheckedChangeListener((control, enabled) -> settings.setEnabled(enabled));
            content.addView(fg);
            if (!fg.isEnabled())
                append(content, "Importe a Lossless.dll na aba Motor.");
            final int[] rates = {20, 30, 45, 60};
            LinearLayout rate = Ui.row(this);
            for (int value : rates) {
                Ui.tabWeight(rate, Ui.button(this, value + " FPS", value == settings.fps(), () -> {
                    settings.setFps(value);
                    sheet.dismiss();
                    gameDetails(pkg);
                }), value == 60 ? 0 : 6);
            }
            append(content, "Limite de FPS original");
            content.addView(rate);
            addButton(content, "Jogar no FrameRoom", true, () -> { sheet.dismiss(); play(pkg); });
            addButton(content, "Limpar dados da instância", false, () -> confirm(
                "Limpar o progresso hospedado?", "O progresso salvo nesta instância será apagado.", () -> {
                    sheet.dismiss();
                    runBusy(() -> GameStorage.clear(this, pkg) ? "Dados hospedados apagados." : "Falha ao limpar alguns dados.");
                }));
        } else {
            addButton(content, "Clonar no espaço paralelo", true, () -> {
                sheet.dismiss();
                openParallelSpace(pkg);
            });
            append(content, GamePackageSnapshot.installedReport(this, pkg));
            addButton(content, "Abrir no Android · sem FG", false, () -> {
                Intent intent = getPackageManager().getLaunchIntentForPackage(pkg);
                if (intent == null) toast("O aplicativo não está instalado.");
                else { sheet.dismiss(); startActivity(intent); }
            });
        }
        if (snapshot != null) append(content, snapshot.report());
        if (!arena) addButton(content, snapshot == null ? "Preparar APKs" : "Atualizar cópia dos APKs", false, () -> {
            sheet.dismiss();
            runBusy(() -> { GamePackageSnapshot.prepare(getApplicationContext(), pkg); return "Cópia dos APKs preparada."; });
        });
        if (snapshot != null) addButton(content, "Apagar APKs copiados", false, () -> confirm(
            "Apagar cópias dos APKs?", "Os arquivos preparados ocuparão espaço novamente se forem copiados.", () -> {
                sheet.dismiss();
                runBusy(() -> "Cópias apagadas: " + GamePackageSnapshot.remove(this, pkg));
            }));
        if (!arena) addButton(content, "Remover da biblioteca", false, () -> {
            AppLibrary.remove(this, pkg);
            sheet.dismiss();
            renderGames();
        });
        ScrollView scroll = Ui.scrolling(this, content);
        scroll.setFillViewport(false);
        sheet.setContentView(scroll);
        Window window = sheet.getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            window.setGravity(Gravity.BOTTOM);
            window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        }
        sheet.show();
        if (window != null) {
            var display = getResources().getDisplayMetrics();
            content.measure(View.MeasureSpec.makeMeasureSpec(display.widthPixels, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
            window.setLayout(-1, Math.min(content.getMeasuredHeight(), display.heightPixels * 4 / 5));
        }
    }

    private void buildEngine(LinearLayout body) {
        body.addView(Ui.text(this, "Frame Generation", 26, Ui.TEXT, true));
        Ui.space(body, 8);
        dllLabel = Ui.text(this, DllStore.location(this).isFile() ? "Lossless.dll importada" : "Nenhuma DLL importada",
            15, DllStore.location(this).isFile() ? Ui.ACCENT : Ui.MUTED, true);
        body.addView(dllLabel);
        addButton(body, "Importar Lossless.dll", true, () -> {
            if (busy) return;
            Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            picker.setType("*/*");
            picker.addCategory(Intent.CATEGORY_OPENABLE);
            startActivityForResult(picker, PICK_DLL);
        });
        if (DllStore.location(this).isFile()) addButton(body, "Remover DLL", false, () -> confirm(
            "Remover Lossless.dll?", "Para reativar o FG, importe o arquivo novamente.", () -> {
                if (busy) return;
                File dll = DllStore.location(this);
                if (dll.delete() || !dll.exists()) { engineReport = ""; renderPage(); }
                else toast("Falha ao remover a DLL.");
            }));
        addButton(body, "Abrir demonstração Vulkan", false, () -> openDemo(DllStore.location(this).isFile()));
        Ui.space(body, 12);
        append(body, "Disponível em 2× na demonstração e no jogo cooperativo da mesma compilação.");
    }

    private void buildDiagnostics(LinearLayout body) {
        body.addView(Ui.text(this, "Diagnósticos", 26, Ui.TEXT, true));
        addButton(body, "Vulkan e AHardwareBuffer", false, () ->
            runDiagnostic(() -> NativeProbe.deviceReport() + "\n" + NativeProbe.bufferReport()));
        addButton(body, "Verificar DLL e inicializar Bloodframe", false, () ->
            runDiagnostic(() -> DllStore.fileReport(this) + "\n\n" +
                NativeProbe.bloodframeReport(DllStore.location(this).getAbsolutePath())));
        addButton(body, "Testar geração de um quadro", false, () ->
            runDiagnostic(() -> NativeProbe.frameGenBatchReport(DllStore.location(this).getAbsolutePath())));
        addButton(body, "Abrir testes Vulkan / AHB", false, () -> openDemo(false));
        Ui.space(body, 12);
        engineLabel = Ui.text(this, engineReport, 14, Ui.MUTED, false);
        engineLabel.setTextIsSelectable(true);
        body.addView(engineLabel);
    }

    private void openDemo(boolean fg) {
        if (!busy) startActivity(new Intent(this, VulkanDemoActivity.class)
                .putExtra(VulkanDemoActivity.EXTRA_FG, fg));
    }

    private void runDiagnostic(Work action) {
        if (busy) return;
        engineReport = "Executando…";
        if (engineLabel != null) engineLabel.setText(engineReport);
        perform(action, result -> {
            engineReport = result;
            if (engineLabel != null) engineLabel.setText(result);
        });
    }

    private void runBusy(Work action) {
        if (busy) { toast("Aguarde a operação atual."); return; }
        toast("Preparando operação…");
        perform(action, result -> {
            toast(result);
            if (list != null) renderGames();
        });
    }

    @Override protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        if (code != PICK_DLL || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        if (dllLabel != null) dllLabel.setText("Validando arquivo…");
        perform(() -> {
            DllStore.importFrom(getApplicationContext(), uri);
            return "Lossless.dll importada.";
        }, message -> {
            engineReport = "";
            toast(message);
            renderPage();
        });
    }

    private interface Work { String run() throws Exception; }
    private interface Completed { void done(String result); }

    private void perform(Work task, Completed completion) {
        busy = true;
        worker.execute(() -> {
            String answer;
            try { answer = task.run(); }
            catch (Exception | LinkageError error) { answer = "Falha: " + error.getMessage(); }
            String result = answer;
            runOnUiThread(() -> {
                if (destroyed) return;
                busy = false;
                completion.done(result);
            });
        });
    }

    private void confirm(String title, String message, Runnable accepted) {
        new AlertDialog.Builder(this).setTitle(title).setMessage(message)
            .setNegativeButton("Cancelar", null)
            .setPositiveButton("Confirmar", (dialog, which) -> accepted.run()).show();
    }
    private void toast(String message) { Toast.makeText(this, message, Toast.LENGTH_LONG).show(); }
    private void addButton(LinearLayout parent, String title, boolean primary, Runnable action) {
        Ui.space(parent, 8);
        parent.addView(Ui.button(this, title, primary, action));
    }
    private void append(LinearLayout parent, String value) {
        TextView text = Ui.text(this, value, 14, Ui.MUTED, false);
        Ui.pad(text, 0, 7, 0, 7);
        parent.addView(text);
    }

    @Override protected void onDestroy() {
        destroyed = true;
        worker.shutdown();
        super.onDestroy();
    }
}

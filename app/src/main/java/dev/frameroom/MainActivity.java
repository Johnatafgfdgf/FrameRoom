package dev.frameroom;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Entry screen for the R0.4 native Vulkan validation milestone, NOT an Android VM. */
public class MainActivity extends Activity {
    private static final int PICK_DLL = 301;
    private static final int BG = Color.rgb(12, 16, 27);
    private static final int PURPLE = Color.rgb(174, 151, 255);
    private static final int MUTED = Color.rgb(176, 183, 205);
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private LinearLayout content;
    private LinearLayout appList;
    private TextView libraryStatus;
    private boolean destroyed;
    private int dllGeneration;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BG);
        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(20), dp(28), dp(20), dp(36));
        scroll.addView(content);
        setContentView(scroll);
        heading("FrameRoom", 29, Color.WHITE);
        info("R0.8 • Execução de APK cooperativo (teste sem root)");
        section("Aplicativos selecionados");
        info("Escolha um jogo. O FrameRoom consegue preparar uma cópia privada do APK base e dos splits, mas ainda não executa o clone. O original permanece intocado.");
        button(content, "+ Adicionar aplicativo", () -> startActivity(new Intent(this, AppPickerActivity.class)));
        appList = new LinearLayout(this);
        appList.setOrientation(LinearLayout.VERTICAL);
        content.addView(appList);
        refreshApps();

        section("Motor de Frame Generation");
        info("Estado: NÃO INTEGRADO. Frame Generation 2× / 3× / 4× permanece desativado, inclusive para apps selecionados.");
        section("Lossless.dll do usuário");
        libraryStatus = info("");
        refreshDll();
        button(content, "Importar Lossless.dll", () -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.setType("*/*");
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            startActivityForResult(intent, PICK_DLL);
        });
        button(content, "Apagar DLL armazenada", () -> {
            ++dllGeneration;
            if (DllStore.location(this).delete() || !DllStore.location(this).exists()) {
                libraryStatus.setText("Nenhuma DLL importada.");
            } else {
                libraryStatus.setText("Não foi possível apagar a DLL armazenada.");
            }
        });
        info("Esta versão tenta inicializar a Bloodframe usando os shaders da DLL fornecida, mas ainda não apresenta quadros gerados.");
        TextView engineStatus = info("Motor LSFG: diagnóstico ainda não executado.");
        button(content, "Diagnosticar Bloodframe + DLL + Vulkan", () -> {
            java.io.File file = DllStore.location(this);
            if (!file.isFile()) { engineStatus.setText("Importe sua Lossless.dll primeiro."); return; }
            engineStatus.setText("Inicializando LSFG para diagnóstico...");
            executor.execute(() -> {
                String answer;
                try { answer = NativeProbe.bloodframeReport(file.getAbsolutePath()); }
                catch (Throwable e) { answer = "Erro LSFG: " + e; }
                final String report = answer;
                if (!destroyed) runOnUiThread(() -> { if (!destroyed) engineStatus.setText(report); });
            });
        });

        TextView batchStatus = info("Lote 2×: ainda não executado.");
        button(content, "Teste experimental: gerar 1 quadro LSFG (sem exibição)", () -> {
            java.io.File file = DllStore.location(this);
            if (!file.isFile()) { batchStatus.setText("Importe sua Lossless.dll primeiro."); return; }
            batchStatus.setText("Gerando quadro sintético pela GPU...");
            executor.execute(() -> {
                String answer;
                try { answer = NativeProbe.frameGenBatchReport(file.getAbsolutePath()); }
                catch (Throwable e) { answer = "Falha no teste 2×: " + e; }
                final String report = answer;
                if (!destroyed) runOnUiThread(() -> { if (!destroyed) batchStatus.setText(report); });
            });
        });
        section("Execução de APK cooperativo — teste R0.8");
        info("Instale o APK FrameRoom Test Game criado na mesma compilação. Adicione-o à lista, prepare uma cópia privada e use o botão EXECUTAR CÓPIA. Apenas este demo assinado funciona; jogos comerciais continuam sem virtualização.");
        section("Processo gráfico separado — prova de conceito");
        info("O renderizador Vulkan agora usa um processo :virtualgfx, separado da interface. Ele só renderiza nossa cena de teste: não executa jogos clonados.");
        section("Vulkan + AHardwareBuffer — Fase 2");
        button(content, "Testar Vulkan direto / AHB duplo", () ->
                startActivity(new Intent(this, VulkanDemoActivity.class)));
        info("A demonstração oferece dois modos: apresentação direta e duas imagens AHardwareBuffer importadas na Vulkan, com cópia GPU para a swapchain. Sem captura, sem LSFG. O modo AHB depende do driver.");
        section("Diagnóstico do dispositivo");
        try { info(NativeProbe.deviceReport() + "\n" + NativeProbe.bufferReport()); }
        catch (Throwable error) { info("Diagnóstico indisponível: " + error.getMessage()); }
    }

    @Override protected void onResume() {
        super.onResume();
        if (appList != null) refreshApps();
    }
    private void refreshApps() {
        if (appList == null) return;
        appList.removeAllViews();
        Set<String> selected = AppLibrary.all(this);
        if (selected.isEmpty()) {
            TextView notice = text("Nenhum aplicativo adicionado. Use + Adicionar aplicativo.");
            appList.addView(notice);
            return;
        }
        List<String> sorted = new ArrayList<>(selected);
        PackageManager pm = getPackageManager();
        sorted.sort(Comparator.comparing(pkg -> label(pm, pkg).toLowerCase(java.util.Locale.ROOT)));
        for (String pkg : sorted) {
            LinearLayout group = new LinearLayout(this);
            group.setOrientation(LinearLayout.VERTICAL);
            group.setPadding(dp(12), dp(10), dp(12), dp(10));
            GradientDrawable shape = new GradientDrawable();
            shape.setColor(Color.rgb(27, 31, 48));
            shape.setCornerRadius(dp(14));
            group.setBackground(shape);
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
            params.bottomMargin = dp(12);
            appList.addView(group, params);
            TextView title = text(label(pm, pkg));
            title.setTextSize(16);
            title.setTextColor(Color.WHITE);
            title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            group.addView(title);
            GamePackageSnapshot.Snapshot current = GamePackageSnapshot.latest(this, pkg);
            TextView sub = text(pkg + "\nExecução em espaço paralelo: AINDA NÃO DISPONÍVEL." +
                    (current == null ? "\nAPKs ainda não preparados." : "\n" + current.report()));
            group.addView(sub);
            if ("dev.frameroom.samplegame".equals(pkg) && current != null) {
                button(group, "Executar cópia do JOGO DE TESTE dentro do FrameRoom", () -> {
                    Intent hosted = new Intent(this, HostedGameActivity.class);
                    hosted.putExtra(HostedGameActivity.EXTRA_PACKAGE, pkg);
                    startActivity(hosted);
                });
            }
            button(group, "Abrir fora do FrameRoom (sem FG)", () -> {
                try {
                    Intent launch = pm.getLaunchIntentForPackage(pkg);
                    if (launch == null) throw new IllegalArgumentException("App não iniciável pelo Launcher.");
                    startActivity(launch);
                } catch (Exception e) {
                    android.widget.Toast.makeText(this, "Falha: " + e.getMessage(), android.widget.Toast.LENGTH_LONG).show();
                }
            });
            button(group, "Preparar cópia dos APKs deste jogo (sem executar)", () -> {
                sub.setText("Preparando arquivos APK instalados... Pode ocupar espaço no celular.");
                executor.execute(() -> {
                    String message;
                    try { message = GamePackageSnapshot.prepare(getApplicationContext(), pkg).report(); }
                    catch (Exception error) { message = "Falha ao copiar APK: " + error.getMessage(); }
                    final String report = message;
                    if (!destroyed) runOnUiThread(() -> {
                        if (!destroyed) {
                            sub.setText(pkg + "\n" + report);
                            if (report.contains("NÃO é um clone executável") ||
                                report.contains("NÃO é um clone executável")) refreshApps();
                        }
                    });
                });
            });
            if (current != null) button(group, "Apagar cópias locais deste jogo", () ->
                new android.app.AlertDialog.Builder(this)
                    .setTitle("Apagar APKs copiados?")
                    .setMessage("Só apaga as cópias do FrameRoom. O jogo original e seus dados permanecem intocados.")
                    .setNegativeButton("Cancelar", null)
                    .setPositiveButton("Apagar", (dialog, which) -> {
                        GamePackageSnapshot.remove(this, pkg);
                        refreshApps();
                    }).show());
            button(group, "Remover da lista", () -> { AppLibrary.remove(this, pkg); refreshApps(); });
        }
    }
    private String label(PackageManager pm, String pkg) {
        try { return pm.getApplicationLabel(pm.getApplicationInfo(pkg, 0)).toString(); }
        catch (Exception ignored) { return pkg; }
    }
    @Override protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        if (code != PICK_DLL || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        final int generation = ++dllGeneration;
        libraryStatus.setText("Validando e importando DLL selecionada...");
        executor.execute(() -> {
            String answer;
            try {
                DllStore.importFrom(getApplicationContext(), uri);
                answer = DllStore.fileReport(getApplicationContext());
            } catch (Exception e) { answer = "Falha na importação: " + e.getMessage(); }
            String resultText = answer;
            if (!destroyed) runOnUiThread(() -> {
                if (!destroyed && generation == dllGeneration) libraryStatus.setText(resultText);
            });
        });
    }
    private void refreshDll() {
        if (libraryStatus == null) return;
        final int generation = ++dllGeneration;
        libraryStatus.setText("Verificando DLL local...");
        executor.execute(() -> {
            final String report = DllStore.fileReport(getApplicationContext());
            if (!destroyed) runOnUiThread(() -> {
                if (!destroyed && generation == dllGeneration) libraryStatus.setText(report);
            });
        });
    }
    @Override protected void onDestroy() {
        destroyed = true;
        executor.shutdown();
        super.onDestroy();
    }
    private int dp(int x) { return (int)(x * getResources().getDisplayMetrics().density + .5f); }
    private void heading(String value, int sp, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(sp);
        view.setTextColor(color);
        view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        view.setPadding(0, dp(8), 0, dp(8));
        content.addView(view);
    }
    private void section(String title) { heading(title, 19, PURPLE); }
    private TextView text(String value) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextColor(MUTED);
        view.setTextSize(14);
        view.setPadding(0, dp(5), 0, dp(12));
        return view;
    }
    private TextView info(String value) {
        TextView view = text(value);
        content.addView(view);
        return view;
    }
    private void button(LinearLayout parent, String title, Runnable action) {
        Button button = new Button(this);
        button.setText(title);
        button.setTextColor(Color.WHITE);
        button.setTextSize(14);
        button.setAllCaps(false);
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(Color.rgb(49, 43, 86));
        shape.setCornerRadius(dp(12));
        button.setBackground(shape);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, dp(52));
        params.bottomMargin = dp(7);
        parent.addView(button, params);
        button.setOnClickListener(v -> action.run());
    }
}

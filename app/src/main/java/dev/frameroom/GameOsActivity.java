package dev.frameroom;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * GameOS is a self-contained guest-Android project, not an Android work profile.
 * This setup screen does NOT claim to boot a guest or enable FG for third-party apps.
 */
public final class GameOsActivity extends Activity {
    private static final int REQUEST_ROM = 407;
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private TextView state;
    private TextView result;
    private Button importButton;
    private boolean destroyed;

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout root = Ui.screen(this);
        LinearLayout header = Ui.row(this);
        Ui.pad(header, 18, 10, 18, 8);
        header.addView(Ui.button(this, "‹", false, this::finish),
            new LinearLayout.LayoutParams(Ui.dp(this, 50), -2));
        TextView title = Ui.text(this, "GameOS", 26, Ui.TEXT, true);
        Ui.pad(title, 10, 0, 0, 0);
        header.addView(title);
        root.addView(header);

        LinearLayout body = Ui.column(this);
        Ui.pad(body, 18, 16, 18, 22);
        LinearLayout panel = Ui.column(this);
        panel.setBackground(Ui.shape(this, Ui.PANEL, 18, Ui.OUTLINE));
        Ui.pad(panel, 16, 16, 16, 16);
        panel.addView(Ui.text(this, "Android dedicado a jogos", 21, Ui.TEXT, true));
        Ui.space(panel, 8);
        panel.addView(Ui.text(this, "Projeto de Android convidado próprio. Não usa perfil de trabalho ou clonagem do Android do celular.", 14, Ui.MUTED, false));
        Ui.space(panel, 14);
        state = Ui.text(this, GameOsRomStore.summary(this), 14, Ui.ACCENT, false);
        panel.addView(state);
        body.addView(panel);

        Ui.space(body, 16);
        importButton = Ui.button(this, "Importar imagem Android (.vrom)", true, this::pickRom);
        body.addView(importButton);
        Ui.space(body, 10);
        result = Ui.text(this, "", 13, Ui.MUTED, false);
        result.setTextIsSelectable(true);
        body.addView(result);

        String selected = getIntent().getStringExtra(SpaceProfile.PACKAGE);
        if (SpaceTarget.validPackage(selected)) {
            Ui.space(body, 16);
            body.addView(Ui.text(this, "Jogo selecionado: " + selected, 14, Ui.TEXT, true));
            body.addView(Ui.text(this, "A instalação e execução desse jogo ocorrerão no Android convidado quando o runtime estiver integrado.", 13, Ui.MUTED, false));
        }

        Ui.space(body, 22);
        body.addView(Ui.text(this, "Estado da execução", 20, Ui.TEXT, true));
        Ui.space(body, 8);
        body.addView(Ui.text(this, "O runtime de Android convidado, os serviços Binder e a ponte de quadros GPU ainda não foram incorporados. Uma ROM importada não é executada pela R0.13 inicial.", 14, Ui.AMBER, false));
        Ui.space(body, 15);
        body.addView(Ui.button(this, "Testar FG no compositor já disponível", false, () -> 
            startActivity(new Intent(this, VulkanDemoActivity.class)
                .putExtra(VulkanDemoActivity.EXTRA_FG, DllStore.location(this).isFile()))));
        root.addView(Ui.scrolling(this, body), new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root);
    }

    private void pickRom() {
        Intent open = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        open.addCategory(Intent.CATEGORY_OPENABLE);
        open.setType("*/*");
        startActivityForResult(open, REQUEST_ROM);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_ROM || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        importButton.setEnabled(false);
        result.setText("Validando imagem Android e copiando para o armazenamento privado…");
        io.execute(() -> {
            String message;
            try { message = GameOsRomStore.importRom(getApplicationContext(), uri); }
            catch (Exception error) { message = "Falha ao importar: " + error.getMessage(); }
            final String report = message;
            runOnUiThread(() -> {
                if (destroyed) return;
                importButton.setEnabled(true);
                result.setText(report);
                state.setText(GameOsRomStore.summary(this));
            });
        });
    }

    @Override protected void onDestroy() {
        destroyed = true;
        io.shutdown();
        super.onDestroy();
    }
}

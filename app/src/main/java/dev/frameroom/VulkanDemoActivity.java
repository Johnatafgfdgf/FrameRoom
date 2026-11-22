package dev.frameroom;

import android.app.Activity;
import android.os.Bundle;
import android.widget.LinearLayout;

public final class VulkanDemoActivity extends Activity {
    static final String EXTRA_FG = "frameGeneration";
    private VulkanPlayerView player;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root = Ui.screen(this);
        player = new VulkanPlayerView(this, "Demonstração Vulkan", null, 30,
                getIntent().getBooleanExtra(EXTRA_FG, false), true);
        root.addView(player, new LinearLayout.LayoutParams(-1, -1));
        setContentView(root);
    }
    @Override protected void onResume() { super.onResume(); player.resume(); }
    @Override protected void onPause() { player.pause(); super.onPause(); }
    @Override protected void onDestroy() { player.close(); super.onDestroy(); }
}

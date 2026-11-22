package dev.frameroom;

import android.content.Context;
import android.content.SharedPreferences;

final class GameSettings {
    private final SharedPreferences preferences;
    private final String id;

    GameSettings(Context context, String id) {
        preferences = context.getSharedPreferences("frame_generation", Context.MODE_PRIVATE);
        this.id = id;
    }

    boolean enabled() { return preferences.getBoolean(id + ".fg", false); }
    void setEnabled(boolean enabled) { preferences.edit().putBoolean(id + ".fg", enabled).apply(); }
    int fps() { return preferences.getInt(id + ".fps", 30); }
    void setFps(int fps) { preferences.edit().putInt(id + ".fps", fps).apply(); }
}

package dev.frameroom;

import android.content.Context;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

/** Persistent shortcuts to system apps. Not a virtual sandbox. */
final class AppLibrary {
    private AppLibrary() {}
    static Set<String> all(Context c) {
        return new HashSet<>(c.getSharedPreferences("apps", Context.MODE_PRIVATE)
                .getStringSet("packages", Collections.emptySet()));
    }
    static void add(Context c, String pkg) {
        Set<String> selected = all(c);
        selected.add(pkg);
        c.getSharedPreferences("apps", Context.MODE_PRIVATE).edit().putStringSet("packages", selected).apply();
    }
    static void remove(Context c, String pkg) {
        Set<String> selected = all(c);
        selected.remove(pkg);
        c.getSharedPreferences("apps", Context.MODE_PRIVATE).edit().putStringSet("packages", selected).apply();
    }
}

package dev.frameroom;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.SharedPreferences;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileNotFoundException;
import java.io.IOException;

/** Per-game storage for a trusted SDK plugin. Java code still shares FrameRoom's UID. */
final class GameStorage extends ContextWrapper {
    private final File root;
    private final String prefix;

    GameStorage(Context host, String packageName) throws IOException {
        super(host);
        root = directory(host, packageName);
        prefix = "guest." + packageName + ".";
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Não foi possível criar o espaço do jogo.");
    }

    static File directory(Context host, String packageName) {
        return new File(new File(host.getFilesDir(), "game_data"), packageName);
    }

    static boolean clear(Context host, String packageName) {
        boolean success = deleteContents(directory(host, packageName));
        String prefix = "guest." + packageName + ".";
        File shared = new File(host.getDataDir(), "shared_prefs");
        File[] preferences = shared.listFiles((file, name) -> name.startsWith(prefix) && name.endsWith(".xml"));
        if (preferences != null)
            for (File file : preferences) {
                String name = file.getName();
                success = host.deleteSharedPreferences(name.substring(0, name.length() - 4)) && success;
            }
        return success;
    }

    private static boolean deleteContents(File root) {
        File[] children = root.listFiles();
        if (children != null)
            for (File child : children) if (!deleteContents(child)) return false;
        return !root.exists() || root.delete();
    }

    @Override public Context getApplicationContext() { return this; }
    @Override public File getFilesDir() { return root; }
    @Override public File getFileStreamPath(String name) {
        if (name.contains("/") || name.equals(".") || name.equals("..") || name.isEmpty())
            throw new IllegalArgumentException("Nome de arquivo inválido.");
        return new File(root, name);
    }
    @Override public FileInputStream openFileInput(String name) throws FileNotFoundException {
        return new FileInputStream(getFileStreamPath(name));
    }
    @Override public FileOutputStream openFileOutput(String name, int mode) throws FileNotFoundException {
        return new FileOutputStream(getFileStreamPath(name), (mode & MODE_APPEND) != 0);
    }
    @Override public boolean deleteFile(String name) { return getFileStreamPath(name).delete(); }
    @Override public String[] fileList() {
        String[] names = root.list();
        return names == null ? new String[0] : names;
    }
    @Override public SharedPreferences getSharedPreferences(String name, int mode) {
        return super.getSharedPreferences(prefix + name, mode);
    }
}

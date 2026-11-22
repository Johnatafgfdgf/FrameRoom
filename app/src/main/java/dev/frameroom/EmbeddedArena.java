package dev.frameroom;

import android.content.Context;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;

/** Loads the cooperative APK shipped as an asset of the signed FrameRoom app. */
final class EmbeddedArena {
    private EmbeddedArena() {}

    static File materialize(Context context) throws Exception {
        // This is a small application asset authenticated with the host APK.
        // Installed third-party archives continue through verifiedDexPath.
        byte[] bundled;
        try (InputStream stream = context.getAssets().open("arena.apk")) {
            byte[] buffer = new byte[8192];
            java.io.ByteArrayOutputStream data = new java.io.ByteArrayOutputStream();
            for (int read; (read = stream.read(buffer)) != -1; ) data.write(buffer, 0, read);
            bundled = data.toByteArray();
        }
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        byte[] expected = digest.digest(bundled);
        File root = context.getCodeCacheDir();
        File target = new File(root, "arena-" + BuildConfig.VERSION_CODE + ".apk");
        if (target.isFile() && !target.canWrite() &&
                MessageDigest.isEqual(expected, digest.digest(Files.readAllBytes(target.toPath())))) {
            return target;
        }
        File staged = File.createTempFile("arena-", ".apk", root);
        try {
            try (FileOutputStream output = new FileOutputStream(staged)) {
                if (!staged.setReadOnly()) throw new IOException("A Arena preparada precisa estar em modo somente leitura.");
                output.write(bundled);
            }
            Files.move(staged.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE,
                StandardCopyOption.REPLACE_EXISTING);
            return target;
        } finally {
            staged.delete();
        }
    }
}

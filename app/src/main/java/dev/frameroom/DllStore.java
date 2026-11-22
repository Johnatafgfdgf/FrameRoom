package dev.frameroom;

import android.content.Context;
import android.net.Uri;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;

/** User-supplied DLL only. Does not execute or extract LSFG shaders. */
public final class DllStore {
    private static final long MAX_LENGTH = 256L * 1024 * 1024;
    private DllStore() {}
    public static File location(Context context) { return new File(context.getFilesDir(), "Lossless.dll"); }

    public static long importFrom(Context context, Uri uri) throws Exception {
        File temporary = File.createTempFile("lossless-", ".tmp", context.getFilesDir());
        long written = 0;
        try {
            try (InputStream in = context.getContentResolver().openInputStream(uri);
                 FileOutputStream out = new FileOutputStream(temporary)) {
                if (in == null) throw new IllegalArgumentException("Não foi possível abrir o arquivo.");
                byte[] bytes = new byte[64 * 1024];
                int n;
                while ((n = in.read(bytes)) != -1) {
                    if (n == 0) continue;
                    written += n;
                    if (written > MAX_LENGTH) throw new IllegalArgumentException("DLL excede 256 MiB.");
                    out.write(bytes, 0, n);
                }
                out.getFD().sync();
            }
            PeValidator.inspect(temporary);
            // Both paths are under filesDir, so move does not cross filesystems.
            try {
                Files.move(temporary.toPath(), location(context).toPath(),
                        StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
            } catch (java.nio.file.AtomicMoveNotSupportedException ignored) {
                Files.move(temporary.toPath(), location(context).toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
            return written;
        } finally { temporary.delete(); }
    }

    public static String fileReport(Context context) {
        File dll = location(context);
        if (!dll.isFile()) return "Nenhuma DLL importada.";
        try {
            PeValidator.Result header = PeValidator.inspect(dll);
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            try (InputStream in = Files.newInputStream(dll.toPath())) {
                byte[] buffer = new byte[64 * 1024];
                int len;
                while ((len = in.read(buffer)) != -1) digest.update(buffer, 0, len);
            }
            StringBuilder sha = new StringBuilder();
            for (byte b : digest.digest()) sha.append(String.format("%02x", b & 0xff));
            return "PE DLL válido: " + header.machineName() + "\n"
                    + "Tamanho: " + dll.length() / 1024 + " KiB\n"
                    + "SHA-256: " + sha + "\n"
                    + "Atenção: cabeçalho PE válido NÃO confirma que é Lossless.dll ou que possui os shaders corretos.\n"
                    + "Motor LSFG: NÃO integrado.";
        } catch (Exception e) { return "DLL armazenada, mas inválida: " + e.getMessage(); }
    }
}

package dev.frameroom;

import android.content.Context;
import android.net.Uri;
import android.os.Build;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.DigestInputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Local ROM package store for FrameRoom GameOS.
 * Importing a .vrom only stages the guest image; it does not claim that the
 * Android guest is bootable or that any game has access to the FG compositor.
 */
final class GameOsRomStore {
    private static final long MAX_ARCHIVE_BYTES = 8L * 1024 * 1024 * 1024;
    private static final long MIN_IMAGE_BYTES = 1024 * 1024;
    private static final int COPY_BUFFER = 64 * 1024;
    static final String ROM_NAME = "gameos.vrom";

    private GameOsRomStore() {}

    static File rom(Context context) {
        return new File(new File(context.getFilesDir(), "gameos"), ROM_NAME);
    }

    static boolean supportedAbi() {
        for (String abi : Build.SUPPORTED_64_BIT_ABIS)
            if ("arm64-v8a".equals(abi)) return true;
        return false;
    }

    static String summary(Context context) {
        File file = rom(context);
        if (!supportedAbi()) return "GameOS requer um aparelho ARM64.";
        if (!file.isFile()) return "Nenhuma imagem Android importada. Escolha uma ROM Waydroid ARM64 (.vrom).";
        return String.format(Locale.ROOT, "ROM importada: %.1f MB. Ainda falta integrar e validar o runtime Android convidado.", 
            file.length() / (1024d * 1024d));
    }

    static String importRom(Context context, Uri source) throws IOException {
        if (!supportedAbi()) throw new IOException("Este aparelho não oferece ABI ARM64.");
        File target = rom(context);
        File directory = target.getParentFile();
        if (directory == null || (!directory.isDirectory() && !directory.mkdirs()))
            throw new IOException("Não foi possível preparar o armazenamento do GameOS.");

        File pending = File.createTempFile("rom-", ".pending", directory);
        boolean moved = false;
        try {
            String hash = copyWithHash(context, source, pending);
            validateArchive(pending);
            Files.move(pending.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
            moved = true;
            context.getSharedPreferences("gameos", Context.MODE_PRIVATE).edit()
                .putString("rom_sha256", hash).apply();
            return "ROM validada e guardada no armazenamento privado do FrameRoom. SHA-256: " + hash;
        } finally {
            if (!moved && pending.exists()) pending.delete();
        }
    }

    private static String copyWithHash(Context context, Uri source, File target) throws IOException {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            try (InputStream sourceStream = context.getContentResolver().openInputStream(source)) {
                if (sourceStream == null) throw new IOException("Não foi possível abrir a ROM selecionada.");
                try (DigestInputStream input = new DigestInputStream(new BufferedInputStream(sourceStream), digest);
                     BufferedOutputStream out = new BufferedOutputStream(new FileOutputStream(target))) {
                    byte[] buffer = new byte[COPY_BUFFER];
                    long count = 0;
                    int length;
                    while ((length = input.read(buffer)) != -1) {
                        count += length;
                        if (count > MAX_ARCHIVE_BYTES) throw new IOException("A ROM excede 8 GiB.");
                        out.write(buffer, 0, length);
                    }
                    if (count < MIN_IMAGE_BYTES) throw new IOException("A ROM parece incompleta.");
                }
            }
            StringBuilder result = new StringBuilder();
            for (byte value : digest.digest()) result.append(String.format(Locale.ROOT, "%02x", value & 0xff));
            return result.toString();
        } catch (NoSuchAlgorithmException missing) {
            throw new IOException("SHA-256 indisponível.", missing);
        }
    }

    private static void validateArchive(File source) throws IOException {
        try (ZipFile zip = new ZipFile(source)) {
            ZipEntry manifest = zip.getEntry("manifest.json");
            ZipEntry system = zip.getEntry("system.img");
            ZipEntry vendor = zip.getEntry("vendor.img");
            if (manifest == null || manifest.isDirectory() || manifest.getSize() <= 0 || manifest.getSize() > 1024 * 1024)
                throw new IOException("ROM inválida: manifest.json ausente ou inválido.");
            if (system == null || system.isDirectory() || system.getSize() < MIN_IMAGE_BYTES)
                throw new IOException("ROM inválida: system.img ausente ou incompleto.");
            if (vendor == null || vendor.isDirectory() || vendor.getSize() < MIN_IMAGE_BYTES)
                throw new IOException("ROM inválida: vendor.img ausente ou incompleto.");
            if (zip.size() > 10000) throw new IOException("ROM inválida: arquivos demais.");

            // Read and CRC-check only the small manifest. The large ext4 images
            // are not extracted on the phone until a real guest backend exists.
            try (InputStream stream = zip.getInputStream(manifest)) {
                byte[] bytes = new byte[(int) manifest.getSize()];
                int cursor = 0;
                while (cursor < bytes.length) {
                    int read = stream.read(bytes, cursor, bytes.length - cursor);
                    if (read < 0) throw new IOException("Manifest incompleto.");
                    cursor += read;
                }
                try {
                    org.json.JSONObject metadata = new org.json.JSONObject(
                        new String(bytes, java.nio.charset.StandardCharsets.UTF_8));
                    if (metadata.length() == 0) throw new IOException("Manifest vazio.");
                } catch (org.json.JSONException invalid) {
                    throw new IOException("Manifest JSON inválido.", invalid);
                }
            }
        }
    }
}

import dev.frameroom.PeValidator;
import java.io.File;
import java.nio.file.Files;

/** Runs on Java 17, without Android dependencies. */
public class PeValidatorTest {
    private static int passed;
    private static void expectFailure(byte[] bytes, String name) throws Exception {
        File f = File.createTempFile("frameroom-pe-", ".dll");
        try {
            Files.write(f.toPath(), bytes);
            boolean rejected = false;
            try { PeValidator.inspect(f); } catch (java.io.IOException ok) { rejected = true; }
            if (!rejected) throw new AssertionError("Aceitou PE inválido: " + name);
            passed++;
        } finally { Files.deleteIfExists(f.toPath()); }
    }
    private static void assertTrue(boolean condition, String name) {
        if (!condition) throw new AssertionError(name);
        passed++;
    }
    public static void main(String[] args) throws Exception {
        byte[] valid = new byte[512];
        valid[0] = 'M'; valid[1] = 'Z'; valid[0x3c] = (byte) 0x80;
        valid[0x80] = 'P'; valid[0x81] = 'E';
        valid[0x84] = 0x64; valid[0x85] = (byte) 0x86; // AMD64
        valid[0x94] = (byte) 0xf0; // optional header size
        valid[0x97] = 0x20; // DLL characteristics
        valid[0x98] = 0x0b; valid[0x99] = 0x02; // PE32+
        File f = File.createTempFile("frameroom-pe-", ".dll");
        try {
            Files.write(f.toPath(), valid);
            PeValidator.Result r = PeValidator.inspect(f);
            assertTrue(r.machine == 0x8664 && r.isDll && r.pe32Plus, "PE64 válido");
        } finally { Files.deleteIfExists(f.toPath()); }
        byte[] wrongMagic = valid.clone(); wrongMagic[0] = 'X';
        expectFailure(wrongMagic, "MZ ausente");
        byte[] wrongPe = valid.clone(); wrongPe[0x80] = 'Z';
        expectFailure(wrongPe, "PE ausente");
        byte[] wrongDll = valid.clone(); wrongDll[0x97] = 0;
        expectFailure(wrongDll, "não DLL");
        byte[] wrongOffset = valid.clone(); wrongOffset[0x3c] = (byte) 0xff;
        wrongOffset[0x3d] = (byte) 0xff;
        expectFailure(wrongOffset, "offset inválido");
        byte[] cut = new byte[60];
        expectFailure(cut, "truncado");
        System.out.println("PeValidator: " + passed + " testes passaram.");
    }
}

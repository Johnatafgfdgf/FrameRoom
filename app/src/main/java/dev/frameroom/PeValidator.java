package dev.frameroom;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;

/** Header validation only. A valid PE DLL is NOT proof of LSFG compatibility. */
public final class PeValidator {
    private PeValidator() {}

    public static final class Result {
        public final int machine;
        public final boolean isDll;
        public final boolean pe32Plus;
        Result(int machine, boolean isDll, boolean pe32Plus) {
            this.machine = machine;
            this.isDll = isDll;
            this.pe32Plus = pe32Plus;
        }
        public String machineName() {
            if (machine == 0x8664) return "Windows x64";
            if (machine == 0x14c) return "Windows x86";
            if (machine == 0xaa64) return "Windows ARM64";
            return "0x" + Integer.toHexString(machine);
        }
    }
    private static int shortLE(RandomAccessFile file) throws IOException {
        return file.readUnsignedByte() | (file.readUnsignedByte() << 8);
    }
    private static long intLE(RandomAccessFile file) throws IOException {
        return (long) file.readUnsignedByte()
                | ((long) file.readUnsignedByte() << 8)
                | ((long) file.readUnsignedByte() << 16)
                | ((long) file.readUnsignedByte() << 24);
    }
    public static Result inspect(File file) throws IOException {
        try (RandomAccessFile raf = new RandomAccessFile(file, "r")) {
            if (raf.length() < 0x80) throw new IOException("Arquivo muito pequeno para uma DLL PE.");
            if (raf.readUnsignedByte() != 'M' || raf.readUnsignedByte() != 'Z')
                throw new IOException("Assinatura MZ não encontrada.");
            raf.seek(0x3c);
            long pe = intLE(raf);
            if (pe < 0x40 || pe > raf.length() - 26)
                throw new IOException("Offset PE inválido.");
            raf.seek(pe);
            if (raf.readUnsignedByte() != 'P' || raf.readUnsignedByte() != 'E'
                    || raf.readUnsignedByte() != 0 || raf.readUnsignedByte() != 0)
                throw new IOException("Assinatura PE inválida.");
            int machine = shortLE(raf);
            raf.seek(pe + 20);
            int optionalSize = shortLE(raf);
            int characteristics = shortLE(raf);
            if (optionalSize < 2 || pe + 24L + optionalSize > raf.length())
                throw new IOException("Cabeçalho opcional PE inconsistente.");
            int magic = shortLE(raf);
            if (magic != 0x10b && magic != 0x20b)
                throw new IOException("Formato PE32/PE32+ não reconhecido.");
            if ((characteristics & 0x2000) == 0)
                throw new IOException("O arquivo PE não está marcado como DLL.");
            return new Result(machine, true, magic == 0x20b);
        }
    }
}

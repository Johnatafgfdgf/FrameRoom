package dev.frameroom;

public final class NativeProbe {
    static { System.loadLibrary("lsfgprobe"); }
    private NativeProbe() {}
    public static native String deviceReport();
    public static native String bufferReport();
}

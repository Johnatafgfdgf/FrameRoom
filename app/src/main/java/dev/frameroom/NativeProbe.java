package dev.frameroom;

public final class NativeProbe {
    static { System.loadLibrary("lsfgprobe"); }
    private NativeProbe() {}
    public static native String deviceReport();
    public static native String bufferReport();
    public static native String bloodframeReport(String dllPath);
    /** Executes a synthetic 2x LSFG batch; outputs are NOT displayed. */
    public static native String frameGenBatchReport(String dllPath);
}

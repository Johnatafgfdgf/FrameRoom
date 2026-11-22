package dev.frameroom.sdk;

/** Contract for a co-signed APK that opts into frame generation in FrameRoom. */
public interface GameScene {
    int MAX_RECTANGLES = 32;
    int FIELDS_PER_RECTANGLE = 7;

    /**
     * Called on the host's render thread for each original frame.
     * Fill x, y, width, height, red, green, blue for each normalized rectangle.
     * Return its rectangle count. Values must be finite and colors use SDR [0, 1].
     */
    int render(long monotonicTimeNs, float[] rectangles);

    /** Viewport coordinates normalized to [0, 1]. Called on the UI thread. */
    void touch(float x, float y, int action);

    /** Optional short instruction shown over the player's controls. */
    default String controlsLabel() { return ""; }

    /** Called on the UI thread. Rendering can finish one in-progress invocation after pause. */
    default void resume() {}
    default void pause() {}
    default void close() {}
}

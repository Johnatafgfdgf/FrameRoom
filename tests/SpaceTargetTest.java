package dev.frameroom;

public final class SpaceTargetTest {
    public static void main(String[] args) {
        check("com.example.game_2", true);
        check("com.example2.Arena", true);
        check(null, false);
        check("", false);
        check("package:com.example.game", false);
        check("com/example.game", false);
        check("com.example.game?other", false);
        check("com..example", false);
        check("com.example.", false);
        check("com." + "a".repeat(252), false);
        System.out.println("PASS: 10 clone package identifier checks");
    }

    private static void check(String value, boolean valid) {
        if (SpaceTarget.validPackage(value) != valid) throw new AssertionError("Unexpected app ID validation for " + value);
    }
}

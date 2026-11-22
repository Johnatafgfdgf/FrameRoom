package dev.frameroom;

import java.util.regex.Pattern;

final class SpaceTarget {
    private static final Pattern PACKAGE = Pattern.compile("[A-Za-z][A-Za-z0-9_]*(?:\\.[A-Za-z][A-Za-z0-9_]*)+");

    private SpaceTarget() {}

    static boolean validPackage(String value) {
        return value != null && value.length() <= 255 && PACKAGE.matcher(value).matches();
    }
}

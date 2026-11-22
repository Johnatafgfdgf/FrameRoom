package dev.frameroom;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

final class Ui {
    static final int BG = 0xff101418;
    static final int PANEL = 0xff1b2228;
    static final int OUTLINE = 0xff354047;
    static final int TEXT = 0xfff4f6f4;
    static final int MUTED = 0xffafbabe;
    static final int ACCENT = 0xffc2f575;
    static final int INK = 0xff142010;
    static final int AMBER = 0xffffbc79;

    private Ui() {}

    static int dp(Context context, int value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }
    static LinearLayout column(Context context) {
        LinearLayout layout = new LinearLayout(context);
        layout.setOrientation(LinearLayout.VERTICAL);
        return layout;
    }
    static LinearLayout row(Context context) {
        LinearLayout layout = new LinearLayout(context);
        layout.setOrientation(LinearLayout.HORIZONTAL);
        layout.setGravity(Gravity.CENTER_VERTICAL);
        return layout;
    }
    static LinearLayout screen(Context context) {
        LinearLayout layout = column(context);
        layout.setBackgroundColor(BG);
        layout.setOnApplyWindowInsetsListener((view, insets) -> {
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() |
                    WindowInsets.Type.displayCutout() | WindowInsets.Type.ime());
                view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            } else view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets;
        });
        return layout;
    }
    static GradientDrawable shape(Context context, int color, int radius, int stroke) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(context, radius));
        if (stroke != 0) drawable.setStroke(dp(context, 1), stroke);
        return drawable;
    }
    static TextView text(Context context, CharSequence label, int size, int color, boolean bold) {
        TextView text = new TextView(context);
        text.setText(label);
        text.setTextColor(color);
        text.setTextSize(size);
        text.setTypeface(Typeface.create(bold ? "sans-serif-medium" : "sans-serif", Typeface.NORMAL));
        return text;
    }
    static Button button(Context context, String label, boolean primary, Runnable action) {
        Button button = new Button(context);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextColor(primary ? INK : TEXT);
        button.setTextSize(14);
        button.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        button.setMinHeight(dp(context, 48));
        button.setMinimumHeight(dp(context, 48));
        button.setPadding(dp(context, 13), dp(context, 8), dp(context, 13), dp(context, 8));
        button.setBackground(new RippleDrawable(ColorStateList.valueOf(0x33ffffff),
                shape(context, primary ? ACCENT : PANEL, 12, primary ? 0 : OUTLINE), null));
        button.setOnClickListener(view -> action.run());
        return button;
    }
    static void pad(View view, int left, int top, int right, int bottom) {
        Context context = view.getContext();
        view.setPadding(dp(context, left), dp(context, top), dp(context, right), dp(context, bottom));
    }
    static void space(LinearLayout parent, int height) {
        parent.addView(new View(parent.getContext()), new LinearLayout.LayoutParams(1, dp(parent.getContext(), height)));
    }
    static ScrollView scrolling(Context context, LinearLayout body) {
        ScrollView scroll = new ScrollView(context);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        scroll.addView(body, new ScrollView.LayoutParams(-1, -2));
        return scroll;
    }
    static EditText search(Context context, String hint) {
        EditText edit = new EditText(context);
        edit.setTextColor(TEXT);
        edit.setHintTextColor(MUTED);
        edit.setTextSize(15);
        edit.setSingleLine(true);
        edit.setHint(hint);
        edit.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        edit.setBackground(shape(context, PANEL, 12, OUTLINE));
        pad(edit, 14, 12, 14, 12);
        return edit;
    }
    static void tabWeight(LinearLayout parent, View child, int margin) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, -2, 1);
        params.rightMargin = dp(parent.getContext(), margin);
        parent.addView(child, params);
    }
}

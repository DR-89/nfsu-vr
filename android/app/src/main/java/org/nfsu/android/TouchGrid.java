package org.nfsu.android;

/** Shared grid math for button centers; the whole control stays on screen. */
final class TouchGrid {
    static final float STEP = 20;

    private TouchGrid() { }

    static float snap(float value, float minimum, float maximum, float step) {
        float first = (float) Math.ceil(minimum / step) * step;
        float last = (float) Math.floor(maximum / step) * step;
        if (first > last) return Math.max(minimum, Math.min(maximum, value));
        float nearest = Math.round(value / step) * step;
        return Math.max(first, Math.min(last, nearest));
    }
}

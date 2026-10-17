package org.nfsu.android;

/** One free-space pointer owns the hold. Pedal/steering pointers stay independent. */
final class ScreenHoldGesture {
    static final int DELAY_MS = 500;
    private int pointer = -1;
    private float startX, startY;
    private boolean active, rejected;

    boolean begin(int id, float x, float y) {
        if (pointer >= 0) return false;
        pointer = id;
        startX = x;
        startY = y;
        active = rejected = false;
        return true;
    }
    boolean owns(int id) { return pointer == id; }
    boolean activate() {
        if (pointer < 0 || rejected) return false;
        active = true;
        return true;
    }
    boolean move(float x, float y, float tolerance) {
        // Once recognized, keep looking back until release, even if the finger moves.
        return active || (Math.abs(x-startX) <= tolerance && Math.abs(y-startY) <= tolerance);
    }
    void reject() { rejected = true; active = false; }
    void cancel() { pointer = -1; active = false; }
}

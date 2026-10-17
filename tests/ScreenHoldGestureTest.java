package org.nfsu.android;

public final class ScreenHoldGestureTest {
    public static void main(String[] args) {
        ScreenHoldGesture hold = new ScreenHoldGesture();
        if (hold.activate()) throw new AssertionError("Idle gesture activated");
        if (!hold.begin(4, 100, 100) || hold.begin(7, 100, 100))
            throw new AssertionError("Pointer ownership broken");
        if (!hold.owns(4) || hold.owns(7)) throw new AssertionError("Wrong pointer");
        if (hold.move(120, 100, 8)) throw new AssertionError("Swipe accepted as hold");
        hold.reject();
        if (hold.activate() || !hold.owns(4)) throw new AssertionError("Rejected swipe lost ownership");
        hold.cancel();
        if (hold.activate() || hold.owns(4)) throw new AssertionError("Release did not cancel");
        hold.begin(7, 100, 100);
        if (!hold.move(104, 104, 8) || !hold.activate() || !hold.move(300, 300, 8))
            throw new AssertionError("Held lookback must survive movement");
        hold.cancel();
        if (hold.activate()) throw new AssertionError("Cancelled timer reactivated gesture");
        System.out.println("Screen hold ownership, movement and release: OK");
    }
}

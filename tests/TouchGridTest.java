package org.nfsu.android;

public final class TouchGridTest {
    private static void equal(float expected, float actual) {
        if (Math.abs(expected-actual) > 0.001f)
            throw new AssertionError(expected + " != " + actual);
    }
    public static void main(String[] args) {
        equal(120, TouchGrid.snap(113, 20, 940, 20));
        equal(120, TouchGrid.snap(125, 43, 917, 20)); // Different-size buttons align.
        equal(40, TouchGrid.snap(-50, 23, 937, 20)); // First on-screen grid node.
        equal(920, TouchGrid.snap(990, 23, 937, 20));
        equal(180, TouchGrid.snap(169.5f, 30, 1410, 30)); // Density/viewport scaling.
        equal(5, TouchGrid.snap(4, 5, 5, 20)); // No grid node fits: keep it on screen.
        System.out.println("Touch grid alignment, scaling and edge constraints: OK");
    }
}

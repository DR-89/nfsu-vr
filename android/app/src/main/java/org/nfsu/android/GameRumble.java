package org.nfsu.android;

import android.content.Context;
import android.os.VibrationEffect;
import android.os.Vibrator;

/** UI-thread owner of the device fallback for Xbox motor feedback. */
final class GameRumble {
    private final Vibrator vibrator;
    private boolean active;
    private int requestedAmplitude;
    private int playingAmplitude;

    GameRumble(Context context) {
        vibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
    }

    void setSpeeds(int left, int right) {
        int strength = Math.max(left, right);
        requestedAmplitude = strength == 0 ? 0 : Math.max(1, (strength * 255 + 32767) / 65535);
        update();
    }

    void setActive(boolean value) {
        active = value;
        update();
    }

    private void update() {
        if (vibrator == null || !vibrator.hasVibrator()) return;
        int amplitude = active ? requestedAmplitude : 0;
        if (amplitude != 0 && !vibrator.hasAmplitudeControl())
            amplitude = VibrationEffect.DEFAULT_AMPLITUDE;
        if (amplitude == playingAmplitude) return;
        playingAmplitude = amplitude;
        if (amplitude == 0) {
            vibrator.cancel();
        } else {
            // USB sends only speed changes, so continue until the next command.
            vibrator.vibrate(VibrationEffect.createWaveform(
                    new long[] {0, 1000}, new int[] {0, amplitude}, 0));
        }
    }
}

package org.nfsu.android;

import java.io.File;
import android.net.Uri;
import android.content.Context;
import android.os.Bundle;
import android.view.Surface;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.SurfaceHolder;
import org.libsdl.app.SDLSurface;
import org.libsdl.app.SDLActivity;

/** SDL hosts the game, controller input and audio on Android. */
public final class GameActivity extends SDLActivity {
    // Activity is the sole owner of system bars. SDL owns the surface/events.
    private final Runnable fullscreen = new Runnable() {
        @Override
        public void run() {
            if (isFinishing() || isDestroyed()) return;
            Window window = getWindow();
            window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.systemBars());
            }
        }
    };

    private void restoreFullscreen() {
        View decor = getWindow().getDecorView();
        decor.removeCallbacks(fullscreen);
        // Run after SDL's initial window-style command and focus processing.
        decor.post(fullscreen);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        restoreFullscreen();
    }

    @Override
    protected void onResume() {
        super.onResume();
        restoreFullscreen();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) restoreFullscreen();
    }

    private static native void nativeSurfaceChanged(Surface surface, int width, int height);

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new SDLSurface(context) {
            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                nativeSurfaceChanged(holder.getSurface(), width, height);
                super.surfaceChanged(holder, format, width, height);
                if (GameActivity.this.hasWindowFocus()) restoreFullscreen();
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                nativeSurfaceChanged(null, 0, 0);
                super.surfaceDestroyed(holder);
            }
        };
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "nfsu2_recomp" };
    }

    @Override
    protected String[] getArguments() {
        File root = getExternalFilesDir(null);
        if (root == null) {
            root = getFilesDir();
        }
        String selected = getSharedPreferences("game", MODE_PRIVATE).getString("tree", null);
        if (selected == null) throw new IllegalStateException("Папка игры не выбрана");
        SafAccess.configure(getContentResolver(), Uri.parse(selected));
        SafAccess.bindNative();
        return new String[] { "/saf-game", root.getAbsolutePath() };
    }
}

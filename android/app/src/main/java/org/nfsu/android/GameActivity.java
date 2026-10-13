package org.nfsu.android;

import java.io.File;
import android.net.Uri;
import org.libsdl.app.SDLActivity;

/** SDL hosts the game, controller input and audio on Android. */
public final class GameActivity extends SDLActivity {
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

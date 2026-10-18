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
    private android.app.Dialog portCredits;

    // Called through JNI on the guest thread; all window work belongs to UI.
    public void showPortCredits() {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                if (isFinishing() || isDestroyed()
                        || (portCredits != null && portCredits.isShowing())) return;
                raceControls.cancelInput();
                portCredits = PortCreditsDialog.show(GameActivity.this);
                portCredits.setOnDismissListener(new android.content.DialogInterface.OnDismissListener() {
                    @Override public void onDismiss(android.content.DialogInterface dialog) {
                        portCredits = null;
                        restoreFullscreen();
                    }
                });
            }
        });
    }

    @Override protected void onDestroy() {
        if (portCredits != null) {
            portCredits.dismiss();
            portCredits = null;
        }
        super.onDestroy();
    }

    private RaceControlsView raceControls;
    private TouchLayoutStore touchLayoutStore;
    private android.view.View editorToolbar;
    private TouchPresetController touchPresets;

    private File gameDirectory() {
        File directory = getExternalFilesDir(null);
        return directory != null ? directory : getFilesDir();
    }

    private void openTouchEditor() {
        if (editorToolbar != null) return;
        raceControls.beginEditing();
        android.widget.LinearLayout toolbar = new android.widget.LinearLayout(this);
        toolbar.setOrientation(android.widget.LinearLayout.VERTICAL);
        toolbar.setPadding(12, 4, 12, 4);
        toolbar.setBackgroundColor(0xEE102F40);
        android.widget.TextView title = new android.widget.TextView(this);
        title.setText("Перетащите кнопки • панель тоже можно двигать");
        title.setPadding(8, 8, 8, 8);
        title.setOnTouchListener(new View.OnTouchListener() {
            private float offsetX, offsetY;
            @Override public boolean onTouch(View view, android.view.MotionEvent event) {
                if (event.getActionMasked() == android.view.MotionEvent.ACTION_DOWN) {
                    offsetX = event.getRawX() - toolbar.getX();
                    offsetY = event.getRawY() - toolbar.getY();
                } else if (event.getActionMasked() == android.view.MotionEvent.ACTION_MOVE) {
                    toolbar.setX(Math.max(0, Math.min(mLayout.getWidth() - toolbar.getWidth(),
                            event.getRawX() - offsetX)));
                    toolbar.setY(Math.max(0, Math.min(mLayout.getHeight() - toolbar.getHeight(),
                            event.getRawY() - offsetY)));
                }
                return true;
            }
        });
        title.setTextColor(android.graphics.Color.WHITE);
        toolbar.addView(title);
        android.widget.LinearLayout actions = new android.widget.LinearLayout(this);
        toolbar.addView(actions);
        addEditorButton(actions, "Сохранить", new View.OnClickListener() {
            @Override public void onClick(View view) {
                try {
                    touchLayoutStore.save(raceControls.getLayout());
                    closeTouchEditor(true);
                } catch (java.io.IOException exception) {
                    android.util.Log.e("TouchLayout", "Cannot save layout", exception);
                    android.widget.Toast.makeText(GameActivity.this,
                            "Не удалось сохранить раскладку", android.widget.Toast.LENGTH_LONG).show();
                }
            }
        });
        addEditorButton(actions, "Сброс", new View.OnClickListener() {
            @Override public void onClick(View view) { raceControls.resetLayout(); }
        });
        addEditorButton(actions, "Отмена", new View.OnClickListener() {
            @Override public void onClick(View view) { closeTouchEditor(false); }
        });
        android.content.SharedPreferences editorPreferences =
                getSharedPreferences("touch-editor", MODE_PRIVATE);
        android.widget.CheckBox snap = new android.widget.CheckBox(this);
        snap.setText("Привязка к сетке");
        snap.setTextColor(android.graphics.Color.WHITE);
        snap.setButtonTintList(android.content.res.ColorStateList.valueOf(0xFF55BCD8));
        snap.setChecked(editorPreferences.getBoolean("snap_to_grid", false));
        raceControls.setSnapToGrid(snap.isChecked());
        snap.setOnCheckedChangeListener(new android.widget.CompoundButton.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(android.widget.CompoundButton button, boolean checked) {
                raceControls.setSnapToGrid(checked);
                editorPreferences.edit().putBoolean("snap_to_grid", checked).apply();
            }
        });
        toolbar.addView(snap);
        android.widget.TextView sizeLabel = new android.widget.TextView(this);
        sizeLabel.setTextColor(android.graphics.Color.WHITE);
        toolbar.addView(sizeLabel);
        android.widget.SeekBar sizeSlider = new android.widget.SeekBar(this);
        sizeSlider.setMax(Math.round((TouchLayout.MAX_SIZE - TouchLayout.MIN_SIZE)*100));
        toolbar.addView(sizeSlider);
        Runnable updateSize = new Runnable() {
            @Override public void run() {
                int selected = raceControls.getSelectedButton();
                sizeSlider.setEnabled(selected >= 0);
                sizeSlider.setProgress(Math.round((raceControls.getSelectedSize() - TouchLayout.MIN_SIZE)*100));
                sizeLabel.setText(selected < 0 ? "Выберите кнопку для изменения размера"
                        : TouchLayout.IDS[selected] + " • " + Math.round(raceControls.getSelectedSize()*100) + "%");
            }
        };
        raceControls.setSelectionChanged(updateSize);
        sizeSlider.setOnSeekBarChangeListener(new android.widget.SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(android.widget.SeekBar slider, int progress, boolean fromUser) {
                if (fromUser) {
                    raceControls.resizeSelected(TouchLayout.MIN_SIZE + progress/100f);
                    updateSize.run();
                }
            }
            @Override public void onStartTrackingTouch(android.widget.SeekBar slider) { }
            @Override public void onStopTrackingTouch(android.widget.SeekBar slider) { }
        });
        updateSize.run();
        touchPresets = new TouchPresetController(this, raceControls, gameDirectory(), updateSize);
        addEditorButton(actions, "Пресеты", new View.OnClickListener() {
            @Override public void onClick(View view) { touchPresets.show(); }
        });
        editorToolbar = toolbar;
        android.widget.RelativeLayout.LayoutParams params = new android.widget.RelativeLayout.LayoutParams(
                android.view.ViewGroup.LayoutParams.WRAP_CONTENT,
                android.view.ViewGroup.LayoutParams.WRAP_CONTENT);
        params.addRule(android.widget.RelativeLayout.CENTER_HORIZONTAL);
        mLayout.addView(toolbar, params);
    }

    private void addEditorButton(android.widget.LinearLayout parent, String text,
            View.OnClickListener listener) {
        android.widget.Button button = new android.widget.Button(this);
        button.setText(text);
        button.setOnClickListener(listener);
        parent.addView(button);
    }

    private void closeTouchEditor(boolean save) {
        if (editorToolbar == null) return;
        mLayout.removeView(editorToolbar);
        editorToolbar = null;
        touchPresets = null;
        raceControls.setSelectionChanged(null);
        raceControls.endEditing(save);
    }

    @Override protected void onActivityResult(int request, int result, android.content.Intent data) {
        if (touchPresets != null && touchPresets.onActivityResult(request, result, data)) return;
        super.onActivityResult(request, result, data);
    }

    @Override public void onBackPressed() {
        if (editorToolbar != null) closeTouchEditor(false);
        else super.onBackPressed();
    }

    @Override public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (editorToolbar != null) {
            if (event.getAction() == android.view.KeyEvent.ACTION_UP
                    && event.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK) closeTouchEditor(false);
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
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
        touchLayoutStore = new TouchLayoutStore(gameDirectory());
        raceControls = new RaceControlsView(this);
        raceControls.setLayout(touchLayoutStore.load());
        raceControls.setEditorRequest(new Runnable() {
            @Override public void run() { openTouchEditor(); }
        });
        mLayout.addView(raceControls, new android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.MATCH_PARENT));
        restoreFullscreen();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (raceControls != null) raceControls.resumeControls();
        restoreFullscreen();
    }

    @Override
    protected void onPause() {
        // A document picker is part of the editor session, not an abandoned edit.
        if (touchPresets == null || !touchPresets.isPickingDocument()) closeTouchEditor(false);
        if (raceControls != null) raceControls.pauseControls();
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) restoreFullscreen();
        else if (raceControls != null) raceControls.cancelInput();
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
        File root = gameDirectory();
        String selected = getSharedPreferences("game", MODE_PRIVATE).getString("tree", null);
        if (selected == null) throw new IllegalStateException("Папка игры не выбрана");
        SafAccess.configure(getContentResolver(), Uri.parse(selected));
        SafAccess.bindNative();
        return new String[] { "/saf-game", root.getAbsolutePath() };
    }
}

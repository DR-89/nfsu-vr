package org.nfsu.android;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Selects the user's disc folder and starts the native game. */
public final class LauncherActivity extends Activity {
    private static final int PICK_GAME = 1;
    private final ExecutorService folderChecker = Executors.newSingleThreadExecutor();
    private TextView status;
    private Button chooseButton;
    private Button playButton;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        int padding = (int) (24 * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);
        TextView title = new TextView(this);
        title.setText("Need for Speed Underground");
        title.setTextSize(24);
        layout.addView(title);
        TextView instructions = new TextView(this);
        instructions.setText("Выберите распакованную Xbox-версию: папку с default.xbe, CARS, TRACKS и остальными файлами.\n\nФайлы остаются в выбранной папке, в том числе на карте памяти. Управление — встроенный геймпад. Сохранения хранятся отдельно в приложении.");
        layout.addView(instructions);
        chooseButton = new Button(this);
        chooseButton.setText("Выбрать папку игры");
        chooseButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                startActivityForResult(intent, PICK_GAME);
            }
        });
        layout.addView(chooseButton);
        playButton = new Button(this);
        playButton.setText("Играть");
        playButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                startActivity(new Intent(LauncherActivity.this, GameActivity.class));
            }
        });
        layout.addView(playButton);
        status = new TextView(this);
        layout.addView(status);
        setContentView(layout);
        refresh();
    }

    private File storageRoot() {
        File root = getExternalFilesDir(null);
        return root == null ? getFilesDir() : root;
    }

    private void refresh() {
        String tree = getSharedPreferences("game", MODE_PRIVATE).getString("tree", null);
        playButton.setEnabled(tree != null);
        status.setText(tree == null ? "Выберите папку с файлами игры."
            : "Выбранная папка: " + Uri.parse(tree).getLastPathSegment()
                + "\nСохранения и журнал: " + storageRoot().getAbsolutePath());
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_GAME || resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        final Uri tree = data.getData();

        chooseButton.setEnabled(false);
        playButton.setEnabled(false);
        status.setText("Проверка выбранной папки…");
        folderChecker.execute(new Runnable() {
            @Override
            public void run() {
                String error = null;
                try {
                    getContentResolver().takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    SafAccess.configure(getContentResolver(), tree);
                    SafAccess.validate();
                    if (!getSharedPreferences("game", MODE_PRIVATE).edit().putString("tree", tree.toString()).commit())
                        throw new IOException("Не удалось сохранить выбранную папку");
                } catch (IOException | RuntimeException exception) {
                    error = exception.getMessage();
                }
                final String result = error;
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        if (isDestroyed()) {
                            return;
                        }
                        chooseButton.setEnabled(true);
                        refresh();
                        if (result != null) {
                            status.setText("Не удалось выбрать папку: " + result);
                        }
                    }
                });
            }
        });
    }

    @Override
    protected void onDestroy() {
        folderChecker.shutdownNow();
        super.onDestroy();
    }
}

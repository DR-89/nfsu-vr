package org.nfsu.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.DialogInterface;
import android.content.Intent;
import android.net.Uri;
import android.widget.EditText;
import android.widget.Toast;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.List;

/** Preset dialogs and document exchange; GameActivity only forwards lifecycle/results. */
final class TouchPresetController {
    private static final int IMPORT = 7101, EXPORT = 7102;
    private final Activity activity;
    private final RaceControlsView controls;
    private final TouchPresetStore store;
    private final Runnable layoutChanged;
    private boolean pickingDocument;
    private TouchLayout exportLayout;

    TouchPresetController(Activity activity, RaceControlsView controls, File directory,
            Runnable layoutChanged) {
        this.activity = activity;
        this.controls = controls;
        this.store = new TouchPresetStore(directory);
        this.layoutChanged = layoutChanged;
    }

    boolean isPickingDocument() { return pickingDocument; }

    void show() {
        new AlertDialog.Builder(activity).setTitle("Пресеты управления")
                .setItems(new String[] {"Сохранить под именем", "Загрузить", "Импорт XML",
                        "Экспорт XML", "Удалить"}, new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface dialog, int which) {
                        if (which == 0) askName(controls.getLayout());
                        else if (which == 1) choosePreset(false);
                        else if (which == 2) pickDocument(false);
                        else if (which == 3) pickDocument(true);
                        else choosePreset(true);
                    }
                }).setNegativeButton("Закрыть", null).show();
    }

    private void message(String text) {
        Toast.makeText(activity, text, Toast.LENGTH_LONG).show();
    }

    private void apply(TouchLayout layout) {
        controls.setLayout(layout);
        layoutChanged.run();
    }

    private void askName(final TouchLayout layout) {
        EditText input = new EditText(activity);
        input.setSingleLine(true);
        input.setHint("Например: Планшет");
        AlertDialog dialog = new AlertDialog.Builder(activity).setTitle("Имя пресета")
                .setView(input).setPositiveButton("Сохранить", null)
                .setNegativeButton("Отмена", null).create();
        dialog.setOnShowListener(new DialogInterface.OnShowListener() {
            @Override public void onShow(DialogInterface shown) {
                dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(new android.view.View.OnClickListener() {
                    @Override public void onClick(android.view.View view) {
                        try {
                            String name = TouchPresetStore.validateName(input.getText().toString());
                            if (store.exists(name)) {
                                new AlertDialog.Builder(activity).setTitle("Перезаписать «" + name + "»?")
                                        .setPositiveButton("Перезаписать", new DialogInterface.OnClickListener() {
                                            @Override public void onClick(DialogInterface confirmation, int which) {
                                                if (save(name, layout)) dialog.dismiss();
                                            }
                                        }).setNegativeButton("Отмена", null).show();
                            } else if (save(name, layout)) dialog.dismiss();
                        } catch (IllegalArgumentException exception) {
                            input.setError(exception.getMessage());
                        }
                    }
                });
            }
        });
        dialog.show();
    }

    private boolean save(String name, TouchLayout layout) {
        try {
            store.save(name, layout);
            message("Пресет сохранён: " + name);
            return true;
        } catch (IOException exception) {
            error("Не удалось сохранить пресет", exception);
            return false;
        }
    }

    private void choosePreset(boolean deleting) {
        final List<String> names;
        try {
            names = store.list();
        } catch (IOException exception) {
            error("Не удалось прочитать список пресетов", exception);
            return;
        }
        if (names.isEmpty()) { message("Сохранённых пресетов пока нет"); return; }
        new AlertDialog.Builder(activity).setTitle(deleting ? "Удалить пресет" : "Загрузить пресет")
                .setItems(names.toArray(new String[0]), new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface dialog, int which) {
                        String name = names.get(which);
                        if (deleting) {
                            new AlertDialog.Builder(activity).setTitle("Удалить «" + name + "»?")
                                    .setPositiveButton("Удалить", new DialogInterface.OnClickListener() {
                                        @Override public void onClick(DialogInterface confirmation, int button) {
                                            try { store.delete(name); }
                                            catch (IOException exception) { error("Не удалось удалить пресет", exception); }
                                        }
                                    }).setNegativeButton("Отмена", null).show();
                        } else {
                            try { apply(store.load(name)); }
                            catch (IOException exception) { error("Пресет повреждён или недоступен", exception); }
                        }
                    }
                }).setNegativeButton("Отмена", null).show();
    }

    private void pickDocument(boolean exporting) {
        Intent intent = new Intent(exporting ? Intent.ACTION_CREATE_DOCUMENT : Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // Providers frequently label shared XML files as text/plain or octet-stream.
        // Validate the actual XML instead of excluding valid presets by MIME type.
        intent.setType(exporting ? "text/xml" : "*/*");
        if (exporting) intent.putExtra(Intent.EXTRA_TITLE, "touch-controls.xml");
        exportLayout = exporting ? controls.getLayout() : null;
        pickingDocument = true;
        try {
            activity.startActivityForResult(intent, exporting ? EXPORT : IMPORT);
        } catch (ActivityNotFoundException exception) {
            pickingDocument = false;
            exportLayout = null;
            error("Системный выбор файлов недоступен", exception);
        }
    }

    boolean onActivityResult(int request, int result, Intent data) {
        if (request != IMPORT && request != EXPORT) return false;
        boolean expected = pickingDocument;
        pickingDocument = false;
        TouchLayout snapshot = exportLayout;
        exportLayout = null;
        if (!expected || result != Activity.RESULT_OK || data == null || data.getData() == null) return true;
        Uri uri = data.getData();
        try {
            if (request == IMPORT) {
                TouchLayout imported;
                try (InputStream input = activity.getContentResolver().openInputStream(uri)) {
                    if (input == null) throw new IOException("Cannot open document");
                    imported = TouchLayout.read(input);
                }
                apply(imported);
                askName(imported); // Cancel keeps the preview; Save adds it to the library.
            } else if (snapshot != null) {
                try (OutputStream output = activity.getContentResolver().openOutputStream(uri, "wt")) {
                    if (output == null) throw new IOException("Cannot write document");
                    snapshot.write(output);
                }
                message("Пресет экспортирован");
            }
        } catch (IOException | SecurityException exception) {
            error(request == IMPORT ? "Не удалось импортировать пресет" : "Не удалось экспортировать пресет", exception);
        }
        return true;
    }

    private void error(String text, Exception exception) {
        android.util.Log.w("TouchPresets", text, exception);
        message(text);
    }
}

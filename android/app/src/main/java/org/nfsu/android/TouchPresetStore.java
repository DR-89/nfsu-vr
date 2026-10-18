package org.nfsu.android;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Named layouts share the active layout codec and atomic persistence. */
final class TouchPresetStore {
    private final File directory;

    TouchPresetStore(File gameDirectory) {
        directory = new File(gameDirectory, "touch-presets");
    }

    static String validateName(String value) {
        String name = value.trim();
        if (name.isEmpty() || name.length() > 48
                || name.getBytes(StandardCharsets.UTF_8).length > 180
                || name.startsWith(".") || name.endsWith(".")) {
            throw new IllegalArgumentException("Введите имя длиной от 1 до 48 символов");
        }
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            if (Character.isISOControl(c) || "/\\:*?\"<>|".indexOf(c) >= 0) {
                throw new IllegalArgumentException("Имя содержит недопустимые символы");
            }
        }
        return name;
    }

    private TouchLayoutStore file(String name) {
        return new TouchLayoutStore(directory, validateName(name) + ".xml");
    }

    List<String> list() throws IOException {
        if (!directory.exists()) return new ArrayList<String>();
        File[] files = directory.listFiles();
        if (files == null) throw new IOException("Cannot list touch presets");
        List<String> names = new ArrayList<String>();
        for (File file : files) {
            String filename = file.getName();
            // AtomicFile may leave only its recoverable backup after a crash.
            if (filename.endsWith(".xml.bak")) filename = filename.substring(0, filename.length()-4);
            if (!filename.endsWith(".xml") || !file.isFile()) continue;
            String name = filename.substring(0, filename.length()-4);
            try {
                if (validateName(name).equals(name) && !names.contains(name)) names.add(name);
            } catch (IllegalArgumentException ignored) { }
        }
        Collections.sort(names, String.CASE_INSENSITIVE_ORDER);
        return names;
    }

    boolean exists(String name) {
        String filename = validateName(name) + ".xml";
        return new File(directory, filename).exists() || new File(directory, filename + ".bak").exists();
    }

    void save(String name, TouchLayout layout) throws IOException {
        validateName(name);
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create preset directory");
        file(name).save(layout);
    }

    TouchLayout load(String name) throws IOException { return file(name).loadChecked(); }

    void delete(String name) throws IOException { file(name).delete(); }
}

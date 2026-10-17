package org.nfsu.android;

import java.io.File;
import java.nio.file.Files;
import java.util.List;

/** Host checks for preset names/discovery. AtomicFile writes need Android. */
public final class TouchPresetStoreTest {
    private static void rejected(String name) {
        try {
            TouchPresetStore.validateName(name);
            throw new AssertionError("Accepted unsafe name: " + name);
        } catch (IllegalArgumentException expected) { }
    }

    public static void main(String[] args) throws Exception {
        if (!"Планшет 10".equals(TouchPresetStore.validateName("  Планшет 10  ")))
            throw new AssertionError("Unicode names or trimming broken");
        rejected("");
        rejected("  ");
        rejected("../outside");
        rejected("folder/file");
        rejected("folder\\file");
        rejected("name\nnext");
        rejected(".hidden");
        rejected("name.");
        rejected("a:b");
        rejected(new String(new char[49]).replace('\0', 'a'));
        File root = Files.createTempDirectory("nfsu-presets-").toFile();
        File directory = new File(root, "touch-presets");
        try {
            TouchPresetStore store = new TouchPresetStore(root);
            if (!store.list().isEmpty()) throw new AssertionError("Missing directory must be empty");
            if (!directory.mkdir()) throw new AssertionError("Cannot create fixture");
            Files.write(new File(directory, "Планшет.xml").toPath(), new byte[0]);
            Files.write(new File(directory, "Alpha.xml").toPath(), new byte[0]);
            Files.write(new File(directory, "Alpha.xml.bak").toPath(), new byte[0]);
            Files.write(new File(directory, "Recovered.xml.bak").toPath(), new byte[0]);
            Files.write(new File(directory, "partial.xml.new").toPath(), new byte[0]);
            Files.write(new File(directory, ".hidden.xml").toPath(), new byte[0]);
            if (!new File(directory, "directory.xml").mkdir()) throw new AssertionError("Cannot create fixture directory");
            List<String> names = store.list();
            if (names.size() != 3 || !"Alpha".equals(names.get(0))
                    || !names.contains("Планшет") || !names.contains("Recovered"))
                throw new AssertionError("Invalid preset discovery: " + names);
            if (!store.exists("Recovered") || store.exists("Missing"))
                throw new AssertionError("Backup recovery or existence check broken");
            System.out.println("Preset names, sorting, discovery and backup handling: OK");
        } finally {
            File[] files = directory.listFiles();
            if (files != null) for (File file : files) Files.delete(file.toPath());
            Files.deleteIfExists(directory.toPath());
            Files.delete(root.toPath());
        }
    }
}

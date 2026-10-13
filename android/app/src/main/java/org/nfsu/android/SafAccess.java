package org.nfsu.android;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/** Read-only document tree. Metadata is cached; game files stay on the selected storage. */
final class SafAccess {
    private static ContentResolver resolver;
    private static Uri tree;
    private static final Map<String, Entry> entries = new HashMap<String, Entry>();
    private static final Map<String, String[]> directories = new HashMap<String, String[]>();

    private static final class Entry {
        final String id;
        final boolean directory;
        final long size;
        Entry(String id, boolean directory, long size) {
            this.id = id;
            this.directory = directory;
            this.size = size;
        }
    }

    private SafAccess() { }

    static synchronized void configure(ContentResolver contentResolver, Uri selectedTree) {
        resolver = contentResolver;
        tree = selectedTree;
        entries.clear();
        directories.clear();
        entries.put("", new Entry(DocumentsContract.getTreeDocumentId(tree), true, 0));
    }

    static native void bindNative();

    private static String key(String path) {
        return path.toLowerCase(Locale.ROOT);
    }

    private static Entry resolve(String path) throws IOException {
        if (path.startsWith("/") || path.contains("\\")) {
            throw new IOException("Недопустимый путь");
        }
        String[] components = path.split("/");
        String current = "";
        for (String component : components) {
            if (component.isEmpty()) continue;
            if (component.equals(".") || component.equals("..")) throw new IOException("Недопустимый путь");
            loadDirectory(current);
            current = current.isEmpty() ? key(component) : current + "/" + key(component);
            if (!entries.containsKey(current)) throw new IOException("Нет файла: " + path);
        }
        Entry entry = entries.get(current);
        if (entry == null) throw new IOException("Нет файла: " + path);
        return entry;
    }

    private static String[] loadDirectory(String path) throws IOException {
        path = key(path);
        String[] cached = directories.get(path);
        if (cached != null) return cached;
        Entry parent = entries.get(path);
        if (parent == null || !parent.directory) throw new IOException("Нет папки: " + path);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent.id);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE};
        java.util.List<String> names = new java.util.ArrayList<String>();
        try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
            if (cursor == null) throw new IOException("Не удалось прочитать папку");
            while (cursor.moveToNext()) {
                String name = cursor.getString(1);
                if (name == null || name.contains("/") || name.contains("\\")) continue;
                String child = path.isEmpty() ? key(name) : path + "/" + key(name);
                entries.put(child, new Entry(cursor.getString(0),
                    DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2)), cursor.getLong(3)));
                names.add(name);
            }
        }
        String[] result = names.toArray(new String[0]);
        directories.put(path, result);
        return result;
    }

    // Called by native worker threads. All metadata access uses this class's monitor.
    static synchronized int openFd(String path) throws IOException {
        Entry entry = resolve(path);
        if (entry.directory) throw new IOException("Это папка: " + path);
        Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, entry.id);
        try (ParcelFileDescriptor descriptor = resolver.openFileDescriptor(document, "r")) {
            if (descriptor == null) throw new IOException("Не удалось открыть " + path);
            return descriptor.detachFd();
        }
    }

    static synchronized long[] stat(String path) throws IOException {
        Entry entry = resolve(path);
        return new long[] {entry.directory ? 1 : 0, entry.size};
    }

    static synchronized String[] list(String path) throws IOException {
        resolve(path);
        return loadDirectory(key(path));
    }

    static synchronized void validate() throws IOException {
        Entry executable = resolve("default.xbe");
        Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, executable.id);
        byte[] header = new byte[0x180];
        try (InputStream input = resolver.openInputStream(document)) {
            if (input == null) throw new IOException("Не удалось открыть default.xbe");
            int position = 0;
            while (position < header.length) {
                int count = input.read(header, position, header.length - position);
                if (count < 0) throw new IOException("default.xbe обрезан");
                position += count;
            }
        }
        int entry = (header[0x128] & 255) | ((header[0x129] & 255) << 8)
            | ((header[0x12a] & 255) << 16) | ((header[0x12b] & 255) << 24);
        if (header[0] != 'X' || header[1] != 'B' || header[2] != 'E' || header[3] != 'H'
            || (entry ^ 0xA8FC57AB) != 0x00164356)
            throw new IOException("Нужна поддерживаемая Xbox-сборка NFS Underground");
        String[] folders = {"CARS", "FRONTEND", "GLOBAL", "LANGUAGES", "MOVIES", "SOUND", "TRACKS"};
        for (String folder : folders) {
            if (!resolve(folder).directory) throw new IOException("Нет папки " + folder);
        }
    }
}

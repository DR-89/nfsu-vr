package org.nfsu.android;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

public final class TouchLayoutTest {
    private static TouchLayout read(String text) throws IOException {
        return TouchLayout.read(new ByteArrayInputStream(text.getBytes(StandardCharsets.UTF_8)));
    }

    private static void invalid(String text) throws Exception {
        try {
            read(text);
            throw new AssertionError("Accepted invalid layout");
        } catch (IOException expected) { }
    }

    public static void main(String[] args) throws Exception {
        TouchLayout layout = new TouchLayout();
        layout.setPosition(1, 0.81f, 0.72f);
        layout.setSize(1, 1.5f);
        layout.setSize(0, 0.5f); // Size is independent of a custom position.
        TouchLayout copy = layout.copy();
        copy.setSize(1, 2f);
        if (layout.getSize(1) != 1.5f) throw new AssertionError("Size copy aliases original");
        copy.setPosition(1, 0.1f, 0.2f);
        if (layout.getX(1) != 0.81f) throw new AssertionError("Copy aliases original");
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        layout.write(output);
        String xml = output.toString("UTF-8");
        TouchLayout restored = read(xml);
        if (restored.getX(1) != 0.81f || restored.getY(1) != 0.72f
                || restored.hasPosition(0)) throw new AssertionError("Round trip changed layout");
        if (restored.getSize(1) != 1.5f || restored.getSize(0) != 0.5f)
            throw new AssertionError("Round trip changed button size");
        TouchLayout legacy = read(xml.replaceAll(" size=\"[^\"]*\"", ""));
        if (legacy.getSize(1) != 1f) throw new AssertionError("Legacy size must default to 1");
        invalid(xml.replace("size=\"1.5\"", "size=\"NaN\""));
        invalid(xml.replace("size=\"1.5\"", "size=\"2.1\""));
        invalid(xml.replace("size=\"1.5\"", "size=\"0\""));
        invalid(xml.replace("0.81", "NaN"));
        invalid(xml.replace("0.81", "Infinity"));
        invalid(xml.replace("0.81", "1.1"));
        invalid(xml.replace("id=\"brake\"", "id=\"gas\""));
        invalid(xml.replace("version=\"1\"", "version=\"2\""));
        invalid("<!DOCTYPE a [<!ENTITY e SYSTEM 'file:///etc/passwd'>]>" + xml);
        invalid(xml.substring(0, xml.indexOf("<button")) + "</touch-controls>");
        invalid(new String(new char[20000]));
        System.out.println("Touch layout XML: OK");
    }
}

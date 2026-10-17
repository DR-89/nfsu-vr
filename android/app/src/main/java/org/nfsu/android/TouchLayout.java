package org.nfsu.android;

import java.io.InputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import javax.xml.parsers.DocumentBuilderFactory;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.NodeList;

/** Normalized button centers; NaN means the original, aspect-aware position. */
final class TouchLayout {
    static final float MIN_SIZE = 0.5f, MAX_SIZE = 2.0f;
    static final String[] IDS = {
        "menu", "gas", "brake", "handbrake", "shift-up", "shift-down",
        "nitro", "left", "right", "camera"
    };
    private final float[] sizes = new float[IDS.length];
    private final float[] x = new float[IDS.length];
    private final float[] y = new float[IDS.length];

    TouchLayout() {
        for (int i = 0; i < IDS.length; i++) {
            x[i] = y[i] = Float.NaN;
            sizes[i] = 1;
        }
    }

    TouchLayout copy() {
        TouchLayout result = new TouchLayout();
        System.arraycopy(sizes, 0, result.sizes, 0, sizes.length);
        System.arraycopy(x, 0, result.x, 0, x.length);
        System.arraycopy(y, 0, result.y, 0, y.length);
        return result;
    }

    float getSize(int index) { return sizes[index]; }
    void setSize(int index, float size) {
        if (!Float.isFinite(size) || size < MIN_SIZE || size > MAX_SIZE)
            throw new IllegalArgumentException("Invalid button size");
        sizes[index] = size;
    }

    boolean hasPosition(int index) { return !Float.isNaN(x[index]); }
    float getX(int index) { return x[index]; }
    float getY(int index) { return y[index]; }

    void setPosition(int index, float centerX, float centerY) {
        if (!Float.isFinite(centerX) || !Float.isFinite(centerY)
                || centerX < 0 || centerX > 1 || centerY < 0 || centerY > 1) {
            throw new IllegalArgumentException("Button position outside screen");
        }
        x[index] = centerX;
        y[index] = centerY;
    }

    static TouchLayout read(InputStream input) throws IOException {
        try {
            DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();
            // Reject DTDs before parsing: Android's DOM factory does not support
            // the desktop Xerces security features consistently.
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buffer = new byte[1024];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (bytes.size() + count > 16384) throw new IOException("Layout too large");
                bytes.write(buffer, 0, count);
            }
            String xml = new String(bytes.toByteArray(), StandardCharsets.UTF_8);
            if (xml.indexOf("<!DOCTYPE") >= 0 || xml.indexOf('\u0000') >= 0) {
                throw new IOException("DTDs and non-UTF-8 layouts are unsupported");
            }
            factory.setExpandEntityReferences(false);
            Document document = factory.newDocumentBuilder().parse(new ByteArrayInputStream(xml.getBytes(StandardCharsets.UTF_8)));
            Element root = document.getDocumentElement();
            if (!"touch-controls".equals(root.getTagName())
                    || !"1".equals(root.getAttribute("version"))) {
                throw new IOException("Unsupported touch layout");
            }
            TouchLayout layout = new TouchLayout();
            boolean[] seen = new boolean[IDS.length];
            NodeList buttons = root.getElementsByTagName("button");
            if (buttons.getLength() < 9 || buttons.getLength() > IDS.length) throw new IOException("Missing buttons");
            for (int n = 0; n < buttons.getLength(); n++) {
                Element button = (Element) buttons.item(n);
                int index = -1;
                for (int i = 0; i < IDS.length; i++) {
                    if (IDS[i].equals(button.getAttribute("id"))) index = i;
                }
                if (index < 0 || seen[index]) throw new IOException("Invalid button id");
                seen[index] = true;
                if (button.hasAttribute("size"))
                    layout.setSize(index, Float.parseFloat(button.getAttribute("size")));
                if (!"true".equals(button.getAttribute("default"))) {
                    layout.setPosition(index, Float.parseFloat(button.getAttribute("x")),
                            Float.parseFloat(button.getAttribute("y")));
                }
            }
            // Version 1 predates the camera button. Require the original nine;
            // older layouts get the new camera button at its default position.
            for (int i = 0; i < 9; i++) {
                if (!seen[i]) throw new IOException("Missing button: " + IDS[i]);
            }
            return layout;
        } catch (IOException exception) {
            throw exception;
        } catch (Exception exception) {
            throw new IOException("Invalid touch layout", exception);
        }
    }

    void write(OutputStream output) throws IOException {
        StringBuilder xml = new StringBuilder("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<touch-controls version=\"1\">\n");
        for (int i = 0; i < IDS.length; i++) {
            xml.append("  <button id=\"").append(IDS[i]).append("\" size=\"").append(sizes[i]);
            if (hasPosition(i)) {
                xml.append("\" x=\"").append(x[i]).append("\" y=\"").append(y[i]);
            } else {
                xml.append("\" default=\"true");
            }
            xml.append("\"/>\n");
        }
        xml.append("</touch-controls>\n");
        output.write(xml.toString().getBytes(StandardCharsets.UTF_8));
    }
}

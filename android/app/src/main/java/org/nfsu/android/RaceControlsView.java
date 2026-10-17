package org.nfsu.android;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.SparseIntArray;
import android.view.MotionEvent;
import android.view.View;

/** Racing HUD and multi-pointer input. Empty screen space belongs to SDL. */
final class RaceControlsView extends View {
    private static final float GAS_ICON_WIDTH = 23;
    private static final float GAS_ICON_HEIGHT = 65;
    private static final float PEDAL_ICON_TOP = 15;
    private static final float GAS_TOUCH_PADDING = 14;
    private static final String[] LABELS = {
        "MENU", "GAS", "BRAKE", "", "", "", "", "", "", ""
    };
    private final RectF[] bounds = new RectF[LABELS.length];
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path symbol = new Path();
    private final SparseIntArray pointers = new SparseIntArray();
    private TouchLayout layout = new TouchLayout();
    private TouchLayout originalLayout;
    private Runnable editorRequest;
    private boolean editing;
    private int dragPointer = -1;
    private int dragButton = -1;
    private int selectedButton = -1;
    private Runnable selectionChanged;
    void setSelectionChanged(Runnable listener) { selectionChanged = listener; }
    int getSelectedButton() { return selectedButton; }
    float getSelectedSize() { return selectedButton < 0 ? 1 : layout.getSize(selectedButton); }
    void resizeSelected(float size) {
        if (!editing || selectedButton < 0) return;
        RectF bounds = this.bounds[selectedButton];
        layout.setPosition(selectedButton, bounds.centerX()/getWidth(), bounds.centerY()/getHeight());
        layout.setSize(selectedButton, size);
        arrangeButtons(getWidth(), getHeight());
        invalidate();
    }
    private float dragOffsetX, dragOffsetY;

    private static native boolean nativeTakeEditorRequest();
    private static native void nativeSetEditing(boolean editing);

    void setEditorRequest(Runnable request) { editorRequest = request; }
    void setLayout(TouchLayout value) {
        layout = value.copy();
        arrangeButtons(getWidth(), getHeight());
        invalidate();
    }
    TouchLayout getLayout() { return layout.copy(); }
    void beginEditing() {
        cancelInput();
        selectedButton = -1;
        originalLayout = layout.copy();
        editing = true;
        nativeSetEditing(true);
        invalidate();
    }
    void endEditing(boolean save) {
        if (!editing) return;
        editing = false;
        dragPointer = dragButton = -1;
        if (!save) setLayout(originalLayout);
        originalLayout = null;
        nativeSetEditing(false);
        cancelInput();
    }
    void resetLayout() {
        setLayout(new TouchLayout());
        if (selectionChanged != null) selectionChanged.run();
    }

    private boolean holdEnabled;
    private final ScreenHoldGesture screenHold = new ScreenHoldGesture();
    private static native boolean nativeHoldEnabled();
    private static native void nativeSetHold(boolean down);
    private final Runnable activateHold = new Runnable() {
        @Override public void run() {
            if (resumed && holdEnabled && !editing && screenHold.activate()) nativeSetHold(true);
        }
    };

    private boolean racing;
    private boolean resumed;
    private int held;
    private float scale;

    private static native boolean nativeVisible();
    private static native void nativeSetButtons(int mask);
    private static native void nativeCancel();

    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            if (!resumed) return;
            if (nativeTakeEditorRequest() && !editing && editorRequest != null) editorRequest.run();
            boolean canHold = nativeHoldEnabled();
            if (holdEnabled && !canHold) cancelInput();
            holdEnabled = canHold;
            boolean visible = nativeVisible();
            if (visible != racing) {
                cancelInput();
                racing = visible;
                invalidate();
            }
            postDelayed(this, 100);
        }
    };

    RaceControlsView(Context context) {
        super(context);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        for (int i = 0; i < bounds.length; i++) {
            bounds[i] = new RectF();
        }
    }

    void resumeControls() {
        resumed = true;
        removeCallbacks(refresh);
        post(refresh);
    }

    void pauseControls() {
        resumed = false;
        removeCallbacks(refresh);
        cancelInput();
        racing = false;
        invalidate();
    }

    void cancelInput() {
        removeCallbacks(activateHold);
        screenHold.cancel();
        nativeSetHold(false);
        pointers.clear();
        held = 0;
        nativeCancel();
        invalidate();
    }

    @Override protected void onDetachedFromWindow() {
        pauseControls();
        super.onDetachedFromWindow();
    }

    private void panel(int index, float x, float y, float width, float height) {
        RectF r = bounds[index];
        r.set(x, y, x + width, y + height);

    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        cancelInput();
        arrangeButtons(w, h);
    }

    private void arrangeButtons(int w, int h) {
        if (w <= 0 || h <= 0) return;
        // Fit both 16:9 and wider screens; keep the central road unobstructed.
        scale = Math.min(w / 960f, h / 540f);
        float s = scale;
        float edge = 18 * s;
        float bottom = h - edge;
        panel(0, edge, edge, 108*s, 38*s);
        panel(9, w - edge - 64*s, edge, 64*s, 50*s);
        panel(7, edge, bottom - 78*s, 86*s, 78*s);
        panel(8, edge + 98*s, bottom - 78*s, 86*s, 78*s);
        panel(5, edge, bottom - 140*s, 86*s, 50*s);
        panel(4, edge + 98*s, bottom - 140*s, 86*s, 50*s);
        panel(1, w - edge - 90*s, bottom - 132*s, 90*s, 132*s);
        panel(2, w - edge - 184*s, bottom - 90*s, 82*s, 90*s);
        panel(3, w - edge - 184*s, bottom - 154*s, 82*s, 52*s);
        panel(6, w - edge - 90*s, bottom - 198*s, 90*s, 54*s);
        for (int i = 0; i < bounds.length; i++) {
            RectF bounds = this.bounds[i];
            float size = layout.getSize(i);
            float width = Math.min(w, bounds.width()*size);
            float height = Math.min(h, bounds.height()*size);
            panel(i, bounds.centerX()-width/2, bounds.centerY()-height/2, width, height);
            moveButton(i, bounds.centerX(), bounds.centerY());
            if (layout.hasPosition(i)) moveButton(i, layout.getX(i)*w, layout.getY(i)*h);
        }
    }

    private void moveButton(int index, float centerX, float centerY) {
        RectF r = bounds[index];
        float width = r.width(), height = r.height();
        float x = Math.max(0, Math.min(getWidth() - width, centerX - width/2));
        float y = Math.max(0, Math.min(getHeight() - height, centerY - height/2));
        panel(index, x, y, width, height);
    }

    @Override protected void onDraw(Canvas canvas) {
        if (!racing && !editing) return;
        if (editing) canvas.drawColor(0xBB000000);
        float originalScale = scale;
        for (int i = 0; i < bounds.length; i++) {
            scale = originalScale * layout.getSize(i);
            float s = scale;
            RectF r = bounds[i];
            boolean down = (held & (1 << i)) != 0 || (editing && selectedButton == i);
            prepareIconPaint(down);
            paint.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD_ITALIC));
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setTextSize(15 * s);
            if (i == 9) {
                drawCamera(canvas, r);
            } else if (i == 3) {
                drawHandbrake(canvas, r);
            } else if (i == 6) {
                drawNitro(canvas, r);
            } else if (i == 4 || i == 5) {
                drawShiftPaddle(canvas, r, i == 4);
            } else if (i == 7 || i == 8) {
                float x = r.centerX();
                float y = r.centerY();
                float direction = i == 7 ? -1 : 1;
                symbol.reset();
                symbol.moveTo(x + direction*17*s, y);
                symbol.lineTo(x - direction*3*s, y - 22*s);
                symbol.lineTo(x - direction*16*s, y - 22*s);
                symbol.lineTo(x + direction*4*s, y);
                symbol.lineTo(x - direction*16*s, y + 22*s);
                symbol.lineTo(x - direction*3*s, y + 22*s);
                symbol.close();
                canvas.drawPath(symbol, paint);
            } else if (i == 1 || i == 2) {
                float top = r.top + PEDAL_ICON_TOP*s;
                float pedalWidth = (i == 1 ? GAS_ICON_WIDTH : 38)*s;
                float pedalHeight = (i == 1 ? GAS_ICON_HEIGHT : 30)*s;
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(3*s);
                canvas.drawRoundRect(r.centerX() - pedalWidth/2, top,
                        r.centerX() + pedalWidth/2, top + pedalHeight, 3*s, 3*s, paint);
                for (int line = 1; line <= 3; line++) {
                    float y = top + pedalHeight*line/4;
                    canvas.drawLine(r.centerX() - pedalWidth/2 + 4*s, y,
                            r.centerX() + pedalWidth/2 - 4*s, y, paint);
                }
                paint.setStyle(Paint.Style.FILL);
            } else {
                canvas.drawText(LABELS[i], r.centerX(),
                        r.centerY() - (paint.ascent() + paint.descent())/2, paint);
            }
        }
        paint.clearShadowLayer();
        scale = originalScale;
    }

    /** Highlight the glyph silhouette, never the rectangular touch target. */
    private void prepareIconPaint(boolean down) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(down ? Color.WHITE : 0xBFFFFFFF);
        paint.clearShadowLayer();
        if (down) paint.setShadowLayer(5 * scale, 0, 0, 0xEE50D9F2);
    }

    /** Compact mirrored paddles share one silhouette; the sign names the action. */
    private void drawShiftPaddle(Canvas canvas, RectF bounds, boolean shiftUp) {
        canvas.save();
        canvas.translate(bounds.centerX(), bounds.centerY());
        canvas.scale(scale, scale);
        canvas.translate(shiftUp ? -5 : 5, 0);
        canvas.scale(shiftUp ? -1 : 1, 1);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(2.5f);
        paint.setStrokeJoin(Paint.Join.ROUND);
        paint.setStrokeCap(Paint.Cap.ROUND);
        symbol.reset();
        symbol.moveTo(0, -20);
        symbol.cubicTo(-9, -20, -21, -7, -21, 3);
        symbol.quadTo(-21, 9, -15, 13);
        symbol.lineTo(-8, 18);
        symbol.quadTo(-6, 20, -3, 20);
        symbol.lineTo(2, 20);
        symbol.quadTo(5, 20, 5, 17);
        symbol.lineTo(5, 12);
        symbol.quadTo(5, 9, 1, 6);
        symbol.quadTo(-5, 2, -1, -5);
        symbol.lineTo(5, -14);
        symbol.quadTo(8, -20, 0, -20);
        symbol.close();
        canvas.drawPath(symbol, paint);
        // Symbols remain clear at the original 86 x 50 logical touch size.
        canvas.drawLine(-11, -6, -1, -6, paint);
        if (shiftUp) canvas.drawLine(-6, -11, -6, -1, paint);
        paint.setStyle(Paint.Style.FILL);
        paint.setStrokeJoin(Paint.Join.MITER);
        paint.setStrokeCap(Paint.Cap.BUTT);
        canvas.restore();
    }

    private void drawCamera(Canvas canvas, RectF bounds) {
        canvas.save();
        canvas.translate(bounds.centerX(), bounds.centerY());
        canvas.scale(scale, scale);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(2.5f);
        canvas.drawRoundRect(-20, -11, 20, 13, 3, 3, paint);
        canvas.drawCircle(0, 1, 7, paint);
        symbol.reset();
        symbol.moveTo(-10, -11);
        symbol.lineTo(-6, -17);
        symbol.lineTo(6, -17);
        symbol.lineTo(10, -11);
        canvas.drawPath(symbol, paint);
        paint.setStyle(Paint.Style.FILL);
        canvas.drawCircle(14, -5, 2, paint);
        canvas.restore();
    }

    /** Raised lever and console base, following the supplied handbrake symbol. */
    private void drawHandbrake(Canvas canvas, RectF bounds) {
        canvas.save();
        canvas.translate(bounds.centerX(), bounds.centerY() - 2 * scale);
        canvas.scale(scale, scale);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(2.8f);
        paint.setStrokeJoin(Paint.Join.ROUND);
        paint.setStrokeCap(Paint.Cap.ROUND);
        // Console base and the curved boot around the lever pivot.
        canvas.drawRect(-27, 11, 27, 16, paint);
        symbol.reset();
        symbol.moveTo(-22, 11);
        symbol.cubicTo(-21, 0, -16, -4, -8, -4);
        symbol.lineTo(-1, -4);
        symbol.quadTo(5, -4, 6, 2);
        symbol.lineTo(9, 11);
        canvas.drawPath(symbol, paint);
        // Shaft and thicker grip point upward to the right.
        symbol.reset();
        symbol.moveTo(-3, -4);
        symbol.lineTo(16, -23);
        symbol.lineTo(23, -16);
        symbol.lineTo(6, 1);
        symbol.moveTo(10, -17);
        symbol.lineTo(17, -10);
        symbol.moveTo(19, -20);
        symbol.lineTo(23, -24);
        canvas.drawPath(symbol, paint);
        paint.setStyle(Paint.Style.FILL);
        paint.setStrokeJoin(Paint.Join.MITER);
        paint.setStrokeCap(Paint.Cap.BUTT);
        canvas.restore();
    }

    /** Slanted N2O cylinder with valve and a small flame-shaped speed accent. */
    private void drawNitro(Canvas canvas, RectF bounds) {
        canvas.save();
        canvas.translate(bounds.centerX(), bounds.centerY() - 2 * scale);
        canvas.scale(scale, scale);
        symbol.reset();
        symbol.moveTo(2, -11);
        symbol.cubicTo(10, -19, 18, -17, 26, -20);
        symbol.quadTo(20, -15, 18, -12);
        symbol.quadTo(26, -14, 30, -11);
        symbol.cubicTo(22, -10, 21, -4, 12, -5);
        symbol.quadTo(16, -8, 12, -10);
        symbol.quadTo(8, -6, 2, -11);
        canvas.drawPath(symbol, paint);
        canvas.rotate(25, -9, 0);
        // Cylinder shoulders, neck and T valve; transparent interior.
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(2.4f);
        paint.setStrokeJoin(Paint.Join.ROUND);
        symbol.reset();
        symbol.moveTo(-18, 13);
        symbol.lineTo(-18, -5);
        symbol.quadTo(-18, -10, -13, -12);
        symbol.lineTo(-13, -16);
        symbol.lineTo(-5, -16);
        symbol.lineTo(-5, -12);
        symbol.quadTo(0, -10, 0, -5);
        symbol.lineTo(0, 13);
        symbol.quadTo(0, 16, -3, 16);
        symbol.lineTo(-15, 16);
        symbol.quadTo(-18, 16, -18, 13);
        symbol.close();
        canvas.drawPath(symbol, paint);
        canvas.drawLine(-9, -16, -9, -20, paint);
        canvas.drawLine(-16, -20, -2, -20, paint);
        canvas.drawLine(-17, 10, -1, 10, paint);
        paint.setStyle(Paint.Style.FILL);
        paint.setTextSize(9);
        canvas.drawText("N₂O", -9, 4, paint);
        paint.setStrokeJoin(Paint.Join.MITER);
        canvas.restore();
    }

    private int hit(float x, float y) {
        for (int i = 0; i < bounds.length; i++) {
            RectF r = bounds[i];
            if (!r.contains(x, y)) continue;
            if (i == 1) {
                // The old panel included space for a caption. Match the pedal
                // silhouette now, keeping finger tolerance and custom scaling.
                float s = scale * layout.getSize(i);
                float halfWidth = (GAS_ICON_WIDTH/2 + GAS_TOUCH_PADDING)*s;
                float top = r.top + (PEDAL_ICON_TOP - GAS_TOUCH_PADDING)*s;
                float bottom = r.top + (PEDAL_ICON_TOP + GAS_ICON_HEIGHT + GAS_TOUCH_PADDING)*s;
                if (Math.abs(x - r.centerX()) > halfWidth || y < top || y > bottom) continue;
            }
            return i;
        }
        return -1;
    }

    private boolean editTouch(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            dragPointer = event.getPointerId(0);
            dragButton = hit(event.getX(), event.getY());
            selectedButton = dragButton;
            if (selectionChanged != null) selectionChanged.run();
            invalidate();
            if (dragButton >= 0) {
                dragOffsetX = event.getX() - bounds[dragButton].centerX();
                dragOffsetY = event.getY() - bounds[dragButton].centerY();
            }
        } else if (action == MotionEvent.ACTION_MOVE && dragButton >= 0) {
            int pointer = event.findPointerIndex(dragPointer);
            if (pointer >= 0) {
                moveButton(dragButton, event.getX(pointer) - dragOffsetX,
                        event.getY(pointer) - dragOffsetY);
                RectF r = bounds[dragButton];
                layout.setPosition(dragButton, r.centerX()/getWidth(), r.centerY()/getHeight());
                invalidate();
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL
                || (action == MotionEvent.ACTION_POINTER_UP
                    && event.getPointerId(event.getActionIndex()) == dragPointer)) {
            dragPointer = dragButton = -1;
        }
        return true;
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (editing) return editTouch(event);
        if (!resumed || (!racing && !holdEnabled)) return false;
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        if (action == MotionEvent.ACTION_DOWN) {
            if (racing && hit(event.getX(), event.getY()) >= 0) {
                pointers.clear();
            } else if (!holdEnabled) return false;
        }
        if (action == MotionEvent.ACTION_CANCEL) {
            cancelInput();
            return true;
        }
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            if (holdEnabled && (!racing || hit(event.getX(index), event.getY(index)) < 0)
                    && screenHold.begin(event.getPointerId(index), event.getX(index), event.getY(index))) {
                postDelayed(activateHold, ScreenHoldGesture.DELAY_MS);
            }
        }
        for (int i = 0; i < event.getPointerCount(); i++) {
            int id = event.getPointerId(i);
            if (screenHold.owns(id)) {
                boolean released = (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) && i == index;
                if (released || !screenHold.move(event.getX(i), event.getY(i),
                        android.view.ViewConfiguration.get(getContext()).getScaledTouchSlop())) {
                    if (released) screenHold.cancel();
                    else screenHold.reject();
                    removeCallbacks(activateHold);
                    nativeSetHold(false);
                }
                continue;
            }
            if ((action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP)
                    && i == index) {
                pointers.delete(id);
            } else {
                // Sliding between buttons changes the held control immediately.
                pointers.put(id, racing ? hit(event.getX(i), event.getY(i)) : -1);
            }
        }
        held = 0;
        for (int i = 0; i < pointers.size(); i++) {
            int button = pointers.valueAt(i);
            if (button >= 0) held |= 1 << button;
        }
        nativeSetButtons(held);
        invalidate();
        return true;
    }
}

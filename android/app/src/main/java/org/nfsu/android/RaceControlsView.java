package org.nfsu.android;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.SparseIntArray;
import android.view.MotionEvent;
import android.view.View;

/** Racing HUD and multi-pointer input. Empty screen space belongs to SDL. */
final class RaceControlsView extends View {
    private final RectF[] bounds = new RectF[TouchLayout.IDS.length];
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final TouchControlArtwork artwork;
    private final SparseIntArray pointers = new SparseIntArray();
    private TouchLayout layout = new TouchLayout();
    private TouchLayout originalLayout;
    private Runnable editorRequest;
    private boolean editing;
    private boolean snapToGrid;

    void setSnapToGrid(boolean enabled) {
        snapToGrid = enabled;
        if (editing && enabled) snapAllButtons();
        invalidate();
    }

    private void moveEditedButton(int index, float x, float y) {
        if (getWidth() <= 0 || getHeight() <= 0) return;
        RectF r = bounds[index];
        if (snapToGrid) {
            float step = TouchGrid.STEP * scale;
            x = TouchGrid.snap(x, r.width()/2, getWidth()-r.width()/2, step);
            y = TouchGrid.snap(y, r.height()/2, getHeight()-r.height()/2, step);
        }
        moveButton(index, x, y);
        layout.setPosition(index, r.centerX()/getWidth(), r.centerY()/getHeight());
    }

    private void snapAllButtons() {
        for (int i = 0; i < bounds.length; i++) {
            RectF r = bounds[i];
            moveEditedButton(i, r.centerX(), r.centerY());
        }
    }
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

    private int buttonMask;
    private static native int nativeButtonMask();
    private boolean buttonAvailable(int index) {
        return editing || (buttonMask & (1 << index)) != 0;
    }
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
            int available = nativeButtonMask();
            if (buttonMask != available) {
                cancelInput();
                buttonMask = available;
                invalidate();
            }
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
        artwork = new TouchControlArtwork(context.getResources());
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
        panel(5, edge, bottom - 174*s, 86*s, 88*s);
        panel(4, edge + 98*s, bottom - 174*s, 86*s, 88*s);
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
        if (editing && snapToGrid) snapAllButtons();
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
        if (editing) {
            canvas.drawColor(0xBB000000);
            if (snapToGrid) drawGrid(canvas);
        }
        for (int i = 0; i < bounds.length; i++) {
            if (!buttonAvailable(i)) continue;
            boolean down = (held & (1 << i)) != 0 || (editing && selectedButton == i);
            artwork.draw(canvas, i, bounds[i], down);
        }
    }

    private void drawGrid(Canvas canvas) {
        float step = TouchGrid.STEP * scale;
        if (step <= 0) return;
        paint.clearShadowLayer();
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(1);
        paint.setColor(0x343E9CB8);
        for (float x = 0; x < getWidth(); x += step)
            canvas.drawLine(x, 0, x, getHeight(), paint);
        for (float y = 0; y < getHeight(); y += step)
            canvas.drawLine(0, y, getWidth(), y, paint);
    }

    private int hit(float x, float y) {
        for (int i = 0; i < bounds.length; i++) {
            if (!buttonAvailable(i)) continue;
            RectF r = bounds[i];
            if (!r.contains(x, y)) continue;
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
                moveEditedButton(dragButton, event.getX(pointer) - dragOffsetX,
                        event.getY(pointer) - dragOffsetY);
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

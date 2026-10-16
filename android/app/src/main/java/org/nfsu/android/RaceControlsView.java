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
    private static final String[] LABELS = {
        "MENU", "GAS", "BRAKE", "", "SHIFT +", "SHIFT -", "", "", ""
    };
    private final RectF[] bounds = new RectF[LABELS.length];
    private final Path[] panels = new Path[LABELS.length];
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path symbol = new Path();
    private final SparseIntArray pointers = new SparseIntArray();
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
            panels[i] = new Path();
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
        float cut = 10 * scale;
        Path p = panels[index];
        p.reset();
        p.moveTo(r.left + cut, r.top);
        p.lineTo(r.right, r.top);
        p.lineTo(r.right, r.bottom - cut);
        p.lineTo(r.right - cut, r.bottom);
        p.lineTo(r.left, r.bottom);
        p.lineTo(r.left, r.top + cut);
        p.close();
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        cancelInput();
        // Fit both 16:9 and wider screens; keep the central road unobstructed.
        scale = Math.min(w / 960f, h / 540f);
        float s = scale;
        float edge = 18 * s;
        float bottom = h - edge;
        panel(0, edge, edge, 108*s, 38*s);
        panel(7, edge, bottom - 78*s, 86*s, 78*s);
        panel(8, edge + 98*s, bottom - 78*s, 86*s, 78*s);
        panel(5, edge, bottom - 140*s, 86*s, 50*s);
        panel(4, edge + 98*s, bottom - 140*s, 86*s, 50*s);
        panel(1, w - edge - 90*s, bottom - 132*s, 90*s, 132*s);
        panel(2, w - edge - 184*s, bottom - 90*s, 82*s, 90*s);
        panel(3, w - edge - 184*s, bottom - 154*s, 82*s, 52*s);
        panel(6, w - edge - 90*s, bottom - 198*s, 90*s, 54*s);
    }

    @Override protected void onDraw(Canvas canvas) {
        if (!racing) return;
        float s = scale;
        for (int i = 0; i < bounds.length; i++) {
            RectF r = bounds[i];
            boolean down = (held & (1 << i)) != 0;
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(down ? 0xC0357188 : 0x88102F40);
            canvas.drawPath(panels[i], paint);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(down ? 0xEE58E5EB : 0xBB009AA9);
            canvas.drawRect(r.left + 7*s, r.bottom - 8*s,
                    r.left + 24*s, r.bottom - 4*s, paint);
            paint.setColor(down ? Color.WHITE : 0xDDB6DCEE);
            paint.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD_ITALIC));
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setTextSize(15 * s);
            if (i == 3) {
                drawHandbrake(canvas, r);
            } else if (i == 6) {
                drawNitro(canvas, r);
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
                float top = r.top + 15*s;
                float pedalWidth = (i == 1 ? 23 : 38)*s;
                float pedalHeight = (i == 1 ? 65 : 30)*s;
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
                canvas.drawText(LABELS[i], r.centerX(), r.bottom - 17*s, paint);
            } else {
                canvas.drawText(LABELS[i], r.centerX(),
                        r.centerY() - (paint.ascent() + paint.descent())/2, paint);
            }
        }
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
            if (bounds[i].contains(x, y)) return i;
        }
        return -1;
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (!resumed || !racing) return false;
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        if (action == MotionEvent.ACTION_DOWN) {
            if (hit(event.getX(), event.getY()) < 0) return false;
            pointers.clear();
        }
        if (action == MotionEvent.ACTION_CANCEL) {
            cancelInput();
            return true;
        }
        for (int i = 0; i < event.getPointerCount(); i++) {
            int id = event.getPointerId(i);
            if ((action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP)
                    && i == index) {
                pointers.delete(id);
            } else {
                // Sliding between buttons changes the held control immediately.
                pointers.put(id, hit(event.getX(i), event.getY(i)));
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

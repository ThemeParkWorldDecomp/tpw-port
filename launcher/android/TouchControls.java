package org.tpwport.android;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PointF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.text.TextUtils;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import java.util.ArrayList;
import java.util.List;

final class TouchControls extends FrameLayout {
    private final ThemeParkActivity game;
    private final ControlSettings settings;
    private final List<View> toolbar = new ArrayList<>(), building = new ArrayList<>();
    private final Button ctrl, shift;
    private final Button gear, resetLayout;
    private final CameraStick stick;
    private boolean editing, parkActive;
    private int modifiers;
    private View dragging;
    private int dragFinger = -1;
    private float dragX, dragY;

    TouchControls(ThemeParkActivity game, ControlSettings settings) {
        super(game);
        this.game = game;
        this.settings = settings;
        setClipChildren(false);

        button(building, "buy", game.getString(R.string.buy), game.getString(R.string.buy_rides), () -> game.press(KeyEvent.KEYCODE_B));
        button(building, "rotate_left", "↺", game.getString(R.string.rotate_left), () -> game.rotate(-1));
        button(building, "rotate_right", "↻", game.getString(R.string.rotate_right), () -> game.rotate(1));
        ctrl = button(building, "ctrl", "Ctrl", game.getString(R.string.hold_ctrl), () -> toggleModifier(1));
        shift = button(building, "shift", "Shift", game.getString(R.string.hold_shift), () -> toggleModifier(2));
        button(building, "map", game.getString(R.string.map), game.getString(R.string.show_map), () -> game.press(KeyEvent.KEYCODE_SPACE));

        button(toolbar, "zoom_out", "−", game.getString(R.string.zoom_out), () -> game.zoom(-1));
        button(toolbar, "zoom_in", "+", game.getString(R.string.zoom_in), () -> game.zoom(1));

        stick = new CameraStick();
        stick.setTag("stick");
        addView(stick, new LayoutParams(dp(120), dp(120)));
        gear = button(null, null, "⚙", game.getString(R.string.control_settings), () -> {
            if (editing) finishLayoutEdit();
            else game.showControlSettings();
        });
        resetLayout = button(null, null, game.getString(R.string.reset_layout), game.getString(R.string.restore_layout), () -> {
            settings.resetPositions();
            requestLayout();
        });
        updateVisibility();
    }

    private int dp(float size) { return Math.round(size * getResources().getDisplayMetrics().density); }

    private GradientDrawable pill(int fill) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(fill);
        shape.setCornerRadius(dp(16));
        shape.setStroke(dp(1), 0x90BBDEF1);
        return shape;
    }

    private Button button(List<View> row, String id, String text, String description, Runnable action) {
        Button button = new Button(game);
        button.setText(text);
        button.setContentDescription(description);
        button.setAllCaps(false);
        button.setTextColor(0xFFF4FAFF);
        button.setTextSize(14);
        button.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        button.setSingleLine(true);
        button.setEllipsize(TextUtils.TruncateAt.END);
        button.setPadding(dp(7), 0, dp(7), 0);
        button.setMinimumWidth(dp(48));
        button.setMinWidth(dp(48));
        button.setFocusable(false);
        button.setSoundEffectsEnabled(false);
        StateListDrawable background = new StateListDrawable();
        background.addState(new int[] {android.R.attr.state_pressed}, pill(0xEC194353));
        background.addState(new int[] {android.R.attr.state_selected}, pill(0xE620687E));
        background.addState(new int[0], pill(0xBB142433));
        button.setBackground(background);
        button.setOnClickListener(view -> action.run());
        button.setTag(id);
        if (row != null) row.add(button);
        addView(button, new LayoutParams(LayoutParams.WRAP_CONTENT, dp(48)));
        return button;
    }

    @Override
    protected void onLayout(boolean changed, int leftEdge, int top, int rightEdge, int bottom) {
        int width = rightEdge - leftEdge, height = bottom - top;
        int stickWidth = stick.getMeasuredWidth();
        int stickX = width - stickWidth - dp(20);
        int stickY = height / 5;
        layoutRow(toolbar, width, stickX + stickWidth / 2, stickY + stick.getMeasuredHeight() + dp(4));
        layoutRow(building, width, width / 2, height - dp(76));
        if (stick.getVisibility() == VISIBLE) place(stick, stickX, stickY, stickWidth);
        place(gear, width / 4, dp(10), gear.getMeasuredWidth());
        if (editing) place(resetLayout, dp(12), dp(10), resetLayout.getMeasuredWidth());
    }

    private void layoutRow(List<View> row, int width, int center, int y) {
        int total = 0, count = 0;
        for (View view : row) if (view.getVisibility() == VISIBLE) { total += view.getMeasuredWidth(); ++count; }
        int spacing = dp(6);
        int available = Math.max(1, width - (count + 1) * spacing);
        float scale = total > available ? (float) available / total : 1;
        int rowWidth = (int) (total * scale) + Math.max(0, count - 1) * spacing;
        int x = clamp(center - rowWidth / 2, Math.max(0, width - rowWidth));
        for (View view : row) {
            if (view.getVisibility() != VISIBLE) continue;
            int cellWidth = Math.max(1, (int) (view.getMeasuredWidth() * scale));
            place(view, x, y, cellWidth);
            x += cellWidth + spacing;
        }
    }

    private void place(View view, int x, int y, int width) {
        int height = view.getMeasuredHeight();
        int spanX = Math.max(0, getWidth() - width), spanY = Math.max(0, getHeight() - height);
        PointF saved = view.getTag() instanceof String ? settings.position((String) view.getTag()) : null;
        int left = clamp(saved != null ? Math.round(saved.x * spanX) : x, spanX);
        int top = clamp(saved != null ? Math.round(saved.y * spanY) : y, spanY);
        view.layout(left, top, left + width, top + height);
    }

    private static int clamp(int n, int end) { return Math.max(0, Math.min(n, end)); }

    void updateVisibility() {
        boolean camera = editing || (settings.cameraVisible() && parkActive);
        for (View view : toolbar) view.setVisibility(camera ? VISIBLE : GONE);
        for (View view : building) view.setVisibility(editing || (camera && settings.buildingVisible()) ? VISIBLE : GONE);
        stick.setVisibility(camera ? VISIBLE : GONE);
        resetLayout.setVisibility(editing ? VISIBLE : GONE);
        requestLayout();
    }

    void setParkActive(boolean active) {
        if (parkActive == active) return;
        parkActive = active;
        if (!active) {
            cancelStick();
            clearModifiers();
        }
        updateVisibility();
    }

    void toggle() {
        boolean visible = !settings.cameraVisible();
        settings.setCameraVisible(visible);
        updateVisibility();
        if (!visible) {
            cancelStick();
            clearModifiers();
        }
    }

    void toggleBuilding() {
        boolean visible = !settings.buildingVisible();
        settings.setBuildingVisible(visible);
        updateVisibility();
        if (!visible) clearModifiers();
    }

    private void toggleModifier(int bit) { setModifiers(modifiers ^ bit); }

    void clearModifiers() { setModifiers(0); }

    private void setModifiers(int next) {
        modifiers = next;
        ctrl.setSelected((modifiers & 1) != 0);
        shift.setSelected((modifiers & 2) != 0);
        game.setOverlayModifiers(modifiers);
    }

    void cancelStick() { stick.reset(); }

    boolean ownsPoint(float x, float y) {
        for (int i = getChildCount() - 1; i >= 0; --i) if (inside(getChildAt(i), x, y)) return true;
        return false;
    }

    boolean editingLayout() { return editing; }

    void beginLayoutEdit() {
        editing = true;
        gear.setText(game.getString(R.string.done));
        gear.setContentDescription(game.getString(R.string.finish_move_controls));
        updateVisibility();
    }

    void finishLayoutEdit() {
        editing = false;
        dragging = null;
        dragFinger = -1;
        gear.setText("⚙");
        gear.setContentDescription(game.getString(R.string.control_settings));
        updateVisibility();
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent event) {
        if (!editing) return false;
        float x = event.getX(), y = event.getY();
        return event.getActionMasked() == MotionEvent.ACTION_DOWN && !inside(gear, x, y) && !inside(resetLayout, x, y);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!editing) return super.onTouchEvent(event);
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            for (int i = getChildCount() - 1; i >= 0; --i) {
                View view = getChildAt(i);
                if (view.getTag() == null || !inside(view, event.getX(), event.getY())) continue;
                dragging = view;
                dragFinger = event.getPointerId(0);
                dragX = event.getX() - view.getLeft();
                dragY = event.getY() - view.getTop();
                break;
            }
        }
        if (dragging == null) return true;
        if (action == MotionEvent.ACTION_CANCEL) {
            dragging = null;
            dragFinger = -1;
            requestLayout();
            return true;
        }
        int index = event.findPointerIndex(dragFinger);
        if (index < 0) return true;
        int width = dragging.getWidth(), height = dragging.getHeight();
        int spanX = Math.max(0, getWidth() - width), spanY = Math.max(0, getHeight() - height);
        int x = clamp(Math.round(event.getX(index) - dragX), spanX);
        int y = clamp(Math.round(event.getY(index) - dragY), spanY);
        dragging.layout(x, y, x + width, y + height);
        if ((action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) && event.getActionIndex() == index) {
            settings.savePosition((String) dragging.getTag(), x / (float) Math.max(1, spanX), y / (float) Math.max(1, spanY));
            dragging = null;
            dragFinger = -1;
            performClick();
        }
        return true;
    }

    @Override public boolean performClick() { return super.performClick(); }

    private boolean inside(View view, float x, float y) {
        return view.getVisibility() == VISIBLE && x >= view.getLeft() && x < view.getRight() &&
               y >= view.getTop() && y < view.getBottom();
    }

    private final class CameraStick extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final String label = game.getString(R.string.camera);
        private int finger = -1;
        private float x, y;

        CameraStick() {
            super(game);
            setContentDescription(game.getString(R.string.camera_stick));
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float center = getWidth() / 2f;
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0x82142433);
            canvas.drawCircle(center, center, center - dp(4), paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(dp(1.5f));
            paint.setColor(0x90BBDEF1);
            canvas.drawCircle(center, center, center - dp(4), paint);
            canvas.drawLine(center - dp(37), center, center + dp(37), center, paint);
            canvas.drawLine(center, center - dp(37), center, center + dp(37), paint);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(finger < 0 ? 0xA5BBDEF1 : 0xE1BBDEF1);
            canvas.drawCircle(center + x * dp(35), center + y * dp(35), dp(18), paint);
            paint.setColor(0xFFF4FAFF);
            paint.setTextSize(dp(10));
            paint.setTextAlign(Paint.Align.CENTER);
            canvas.drawText(label, center, center + dp(49), paint);
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) finger = event.getPointerId(0);
            if (action == MotionEvent.ACTION_CANCEL ||
                ((action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) &&
                 event.getPointerId(event.getActionIndex()) == finger)) {
                reset();
                return true;
            }
            int pointer = event.findPointerIndex(finger);
            if (pointer < 0) return true;
            x = (event.getX(pointer) - getWidth() / 2f) / dp(35);
            y = (event.getY(pointer) - getHeight() / 2f) / dp(35);
            float length = Math.max(1, (float) Math.hypot(x, y));
            x /= length;
            y /= length;
            game.setOnScreenStick(x, y);
            invalidate();
            return true;
        }

        void reset() {
            finger = -1;
            x = y = 0;
            game.setOnScreenStick(0, 0);
            invalidate();
        }
    }
}
